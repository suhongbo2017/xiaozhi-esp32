#include "mp3_player.h"
#include "board.h"
#include "audio_codec.h"
#include "minimp3.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <vector>

#define TAG "MP3_PLAYER"
#define MP3_PLAYER_TASK_PRIORITY 5
#define MP3_PLAYER_TASK_STACK_SIZE (8 * 1024)
#define MP3_DECODE_BUF_SIZE (16 * 1024)

Mp3Player::~Mp3Player() {
    Stop();
}

void Mp3Player::Stop() {
    playing_ = false;
}

static void mp3_decoder_task(void *pvParameters) {
    std::string* url_ptr = static_cast<std::string*>(pvParameters);
    std::string url = *url_ptr;
    delete url_ptr;
    
    ESP_LOGI(TAG, "Starting MP3 playback: %s", url.c_str());
    
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    auto network = board.GetNetwork();
    if (!codec || !network) {
        ESP_LOGE(TAG, "Failed to get codec or network");
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    // Create HTTP connection
    auto http = network->CreateHttp(3);
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    if (!http->Open("GET", url.c_str())) {
        ESP_LOGE(TAG, "Failed to open URL: %s", url.c_str());
        delete http;
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "HTTP status: %d", http->GetStatusCode());
        http->Close();
        delete http;
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    size_t content_length = http->GetBodyLength();
    ESP_LOGI(TAG, "MP3 size: %zu bytes", content_length);
    
    // Read entire MP3 into buffer
    std::vector<uint8_t> mp3_data(content_length);
    size_t total_read = 0;
    while (total_read < content_length && Mp3Player::GetInstance().IsPlaying()) {
        int ret = http->Read((char*)(mp3_data.data() + total_read), content_length - total_read);
        if (ret < 0) {
            ESP_LOGE(TAG, "HTTP read error: %d", ret);
            break;
        }
        if (ret == 0) break;
        total_read += ret;
    }
    http->Close();
    delete http;
    
    if (total_read == 0) {
        ESP_LOGE(TAG, "No data downloaded");
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    mp3_data.resize(total_read);
    ESP_LOGI(TAG, "Downloaded %zu bytes, starting decode", mp3_data.size());
    
    // Initialize MP3 decoder
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);
    
    // Decode and play MP3 frames
    std::vector<int16_t> pcm(MP3_DECODE_BUF_SIZE);
    size_t offset = 0;
    int frames = 0;
    
    while (offset < mp3_data.size() && Mp3Player::GetInstance().IsPlaying()) {
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&mp3d, mp3_data.data() + offset, 
                                          mp3_data.size() - offset, 
                                          pcm.data(), &info);
        
        if (samples <= 0 || info.frame_bytes == 0) {
            // Try to skip to next sync word
            offset++;
            continue;
        }
        
        // Send decoded PCM data to audio codec
        pcm.resize(samples * info.channels);
        codec->OutputData(pcm);
        pcm.resize(MP3_DECODE_BUF_SIZE);
        
        offset += info.frame_bytes;
        frames++;
        
        // Small yield to prevent watchdog timeout
        if (frames % 10 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    
    ESP_LOGI(TAG, "MP3 playback finished, decoded %d frames", frames);
    Mp3Player::GetInstance().Stop();
    vTaskDelete(NULL);
}

bool Mp3Player::PlayUrl(const std::string& url) {
    if (playing_) {
        ESP_LOGW(TAG, "Already playing, stopping first");
        Stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    current_url_ = url;
    playing_ = true;
    
    std::string* url_copy = new std::string(url);
    if (url_copy == nullptr) {
        playing_ = false;
        return false;
    }
    
    TaskHandle_t task_handle;
    BaseType_t ret = xTaskCreate(
        mp3_decoder_task,
        "mp3_player",
        MP3_PLAYER_TASK_STACK_SIZE,
        url_copy,
        MP3_PLAYER_TASK_PRIORITY,
        &task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MP3 player task");
        delete url_copy;
        playing_ = false;
        return false;
    }
    
    return true;
}
