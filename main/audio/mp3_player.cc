#include "mp3_player.h"
#include "mp3_decoder.h"
#include "board.h"
#include "audio_codec.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <vector>

#define TAG "MP3_PLAYER"
#define DECODE_BUF_SIZE (16 * 1024)

Mp3Player::~Mp3Player() {
    Stop();
}

void Mp3Player::Stop() {
    playing_ = false;
}

static void mp3_player_task(void *pvParameters) {
    std::string* url_ptr = static_cast<std::string*>(pvParameters);
    std::string url = *url_ptr;
    delete url_ptr;
    
    ESP_LOGI(TAG, "Playing: %s", url.c_str());
    
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    auto network = board.GetNetwork();
    if (!codec || !network) {
        ESP_LOGE(TAG, "No codec or network");
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    // HTTP download
    auto http = network->CreateHttp(3);
    if (!http || !http->Open("GET", url.c_str())) {
        ESP_LOGE(TAG, "HTTP failed");
        if (http) { http->Close(); delete http; }
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "HTTP %d", http->GetStatusCode());
        http->Close(); delete http;
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    size_t total = http->GetBodyLength();
    ESP_LOGI(TAG, "Downloading %zu bytes...", total);
    
    std::vector<uint8_t> mp3_data(total);
    size_t read = 0;
    while (read < total && Mp3Player::GetInstance().IsPlaying()) {
        int ret = http->Read((char*)(mp3_data.data() + read), total - read);
        if (ret <= 0) break;
        read += ret;
    }
    http->Close();
    delete http;
    
    if (read == 0) {
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    mp3_data.resize(read);
    ESP_LOGI(TAG, "Downloaded %zu bytes, decoding...", read);
    
    // Decode and play
    void* dec = mp3_decoder_create();
    if (!dec) {
        Mp3Player::GetInstance().Stop();
        vTaskDelete(NULL);
        return;
    }
    
    std::vector<int16_t> pcm(DECODE_BUF_SIZE);
    size_t offset = 0;
    int frames = 0;
    
    while (offset < mp3_data.size() && Mp3Player::GetInstance().IsPlaying()) {
        Mp3FrameInfo info;
        int samples = mp3_decoder_decode(dec, mp3_data.data() + offset,
                                          mp3_data.size() - offset,
                                          pcm.data(), &info);
        
        if (samples <= 0 || info.frame_bytes == 0) {
            offset++;
            continue;
        }
        
        pcm.resize(samples * info.channels);
        codec->OutputData(pcm);
        pcm.resize(DECODE_BUF_SIZE);
        
        offset += info.frame_bytes;
        frames++;
        
        if (frames % 10 == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    mp3_decoder_destroy(dec);
    ESP_LOGI(TAG, "Done, %d frames", frames);
    Mp3Player::GetInstance().Stop();
    vTaskDelete(NULL);
}

bool Mp3Player::PlayUrl(const std::string& url) {
    if (playing_) {
        Stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    current_url_ = url;
    playing_ = true;
    
    std::string* url_copy = new std::string(url);
    TaskHandle_t task;
    if (xTaskCreate(mp3_player_task, "mp3", 8*1024, url_copy, 5, &task) != pdPASS) {
        delete url_copy;
        playing_ = false;
        return false;
    }
    return true;
}
