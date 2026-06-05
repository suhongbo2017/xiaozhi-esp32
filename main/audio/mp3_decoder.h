#ifndef MP3_DECODER_H
#define MP3_DECODER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int channels;
    int hz;
    int samples;
    int frame_bytes;
} Mp3FrameInfo;

void* mp3_decoder_create();
int mp3_decoder_decode(void* dec, const uint8_t* data, int data_size, int16_t* pcm, Mp3FrameInfo* info);
void mp3_decoder_destroy(void* dec);

#ifdef __cplusplus
}
#endif

#endif // MP3_DECODER_H
