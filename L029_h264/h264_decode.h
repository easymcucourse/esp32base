#ifndef L029_H264_DECODE_H
#define L029_H264_DECODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*h264_frame_cb_t)(const uint8_t *i420_buf,
                                size_t i420_size,
                                void *user_ctx);

typedef struct {
    int decoded_chunks;
    int output_frames;
} h264_decode_stats_t;

typedef enum {
    H264_DECODE_OK = 0,
    H264_DECODE_OPEN_FAILED = -1,
    H264_DECODE_DECODER_CREATE_FAILED = -2,
    H264_DECODE_DECODER_OPEN_FAILED = -3,
    H264_DECODE_ALLOC_FAILED = -4,
} h264_decode_result_t;

h264_decode_result_t h264_decode_file_once(const char *path,
                                           size_t read_buffer_size,
                                           size_t nal_buffer_size,
                                           h264_frame_cb_t frame_cb,
                                           void *user_ctx,
                                           h264_decode_stats_t *stats);

void on_h264_frame(const uint8_t *i420_buf, size_t i420_size, void *user_ctx);

#ifdef __cplusplus
}
#endif

#endif
