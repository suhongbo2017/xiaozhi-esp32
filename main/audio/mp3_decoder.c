#include "mp3_decoder.h"
#include "minimp3.h"
#include <stdlib.h>
#include <string.h>

void* mp3_decoder_create() {
    mp3dec_t* dec = (mp3dec_t*)malloc(sizeof(mp3dec_t));
    if (dec) {
        mp3dec_init(dec);
    }
    return dec;
}

int mp3_decoder_decode(void* dec, const uint8_t* data, int data_size, int16_t* pcm, Mp3FrameInfo* info) {
    mp3dec_frame_info_t minfo;
    int samples = mp3dec_decode_frame((mp3dec_t*)dec, data, data_size, pcm, &minfo);
    if (info) {
        info->channels = minfo.channels;
        info->hz = minfo.hz;
        info->samples = samples;
        info->frame_bytes = minfo.frame_bytes;
    }
    return samples;
}

void mp3_decoder_destroy(void* dec) {
    if (dec) free(dec);
}
