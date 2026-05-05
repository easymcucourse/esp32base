#include "h264_decode.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_h264_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define IMG_WIDTH  160
#define IMG_HEIGHT 128

extern QueueHandle_t empty_queue;
extern QueueHandle_t frame_queue;

extern esp_h264_err_t esp_h264_dec_sw_new(const esp_h264_dec_cfg_t *cfg,
                                          esp_h264_dec_handle_t *dec);

static int int_min(int a, int b) {
    return (a < b) ? a : b;
}

static int int_max(int a, int b) {
    return (a > b) ? a : b;
}

/**
 * 【视频口播对应：第一步，寻找数据边界（路标）】
 * 查找H.264 Annex B裸流格式的起始码 (0x000001 或 0x00000001)。
 * 这就像是数据流里的路标，程序靠寻找这些起始码来精准切分出一个个独立的 NAL 单元。
 * @param buf 缓冲数据
 * @param from 起始搜索位置
 * @param len 缓冲数据总长度
 * @param start_len [输出] 起始码的长度(3或4)
 * @return 起始码在缓冲中的索引，未找到返回-1
 */
static int find_start_code(const uint8_t *buf, int from, int len, int *start_len) {
    for (int i = from; i + 3 < len; i++) {
        if (buf[i] == 0 && buf[i + 1] == 0) {
            if (buf[i + 2] == 1) {
                *start_len = 3;
                return i;
            }
            if (buf[i + 2] == 0 && buf[i + 3] == 1) {
                *start_len = 4;
                return i;
            }
        }
    }
    return -1;
}

/**
 * 从Annex B格式的NAL数据中提取NAL单元类型
 * @param nal NAL数据流
 * @param nal_len NAL数据流长度
 * @return NAL单元类型(0-31)，解析失败返回-1
 */
static int nal_type_from_annexb(const uint8_t *nal, int nal_len) {
    int sc_len = 0;
    int start = find_start_code(nal, 0, nal_len, &sc_len);
    if (start < 0 || start + sc_len >= nal_len) return -1;
    return nal[start + sc_len] & 0x1f;
}

/**
 * 【视频口播对应：第二步，过滤减负】
 * 判断NAL单元是否需要送入解码器。
 * 拦截无关数据（如SEI附加信息），只放行SPS、PPS参数集以及真正的核心画面切片（IDR/Non-IDR）。
 * 把无关数据扔掉，极大减轻ESP32的CPU负担。
 */
static int should_decode_nal(const uint8_t *nal, int nal_len) {
    int nal_type = nal_type_from_annexb(nal, nal_len);

    switch (nal_type) {
    case 1: // non-IDR slice (普通画面切片)
    case 5: // IDR slice (关键帧画面切片)
    case 7: // SPS (序列参数集)
    case 8: // PPS (图像参数集)
        return 1; // 放行
    default:
        return 0; // 拦截丢弃
    }
}

/**
 * 【视频口播对应：第三步，送入解码器】
 * 将提取并过滤好的干净NAL单元，送入乐鑫的H.264软件解码API进行解码。
 * 解码器在内部不断组装切片，一旦攒齐并成功解出一帧完整的画面，就会触发 frame_cb 回调函数输出图像数据。
 * @param decoder 解码器句柄
 * @param nal NAL数据首地址
 * @param nal_len NAL数据长度
 * @param frame_cb 解码成功输出完整帧时的回调函数
 * @param user_ctx 用户自定义上下文，透传给回调
 * @return 本次调用输出的有效帧数(通常是0或1)
 */
static int decode_nal(esp_h264_dec_handle_t decoder,
                      uint8_t *nal,
                      int nal_len,
                      h264_frame_cb_t frame_cb,
                      void *user_ctx) {
    if (!decoder || !nal || nal_len <= 0) return 0;
    if (!should_decode_nal(nal, nal_len)) return 0;

    int output_frames = 0;
    esp_h264_dec_in_frame_t in_frame = {0};
    in_frame.raw_data.buffer = nal;
    in_frame.raw_data.len = nal_len;
    esp_h264_dec_out_frame_t out_frame = {0};

    while (in_frame.raw_data.len > 0) {
        // 调用底层 API 解码
        esp_h264_err_t err = esp_h264_dec_process(decoder, &in_frame, &out_frame);
        if (err != ESP_H264_ERR_OK || in_frame.consume == 0) break;

        in_frame.raw_data.buffer += in_frame.consume;
        in_frame.raw_data.len -= in_frame.consume;

        // 如果攒齐了一帧完整的画面，触发回调（注：这里出来的是 I420 格式）
        if (out_frame.out_size > 0 && out_frame.outbuf) {
            if (frame_cb) frame_cb(out_frame.outbuf, out_frame.out_size, user_ctx);
            output_frames++;
        }
    }

    return output_frames;
}

/**
 * 【视频口播对应：第四步，数据流水线调度】
 * 完整读取并解码一个H.264裸流文件。建立一条“边读边解”的高效流水线。
 * @param path H.264裸流文件在SD卡/SPIFFS中的路径
 * @param read_buffer_size 文件一次性读取的块大小
 * @param nal_buffer_size 单个NAL单元的最大允许缓存大小
 * @param frame_cb 解码出完整帧时的回调处理函数
 * @param user_ctx 用户上下文透传
 * @param stats [输出] 解码过程的统计数据
 * @return 解码结果状态码
 */
h264_decode_result_t h264_decode_file_once(const char *path,
                                           size_t read_buffer_size,
                                           size_t nal_buffer_size,
                                           h264_frame_cb_t frame_cb,
                                           void *user_ctx,
                                           h264_decode_stats_t *stats) {
    if (stats) {
        stats->decoded_chunks = 0;
        stats->output_frames = 0;
    }

    // 打开流媒体文件
    FILE *f = fopen(path, "rb");
    if (!f) return H264_DECODE_OPEN_FAILED;

    // 初始化ESP H.264软件解码器配置，指定输出为I420格式 (后续在回调中转换为RGB565)
    esp_h264_dec_cfg_t dec_cfg = {
        .pic_type = ESP_H264_RAW_FMT_I420,
    };
    esp_h264_dec_handle_t decoder = NULL;
    esp_h264_err_t err = esp_h264_dec_sw_new(&dec_cfg, &decoder);
    if (err != ESP_H264_ERR_OK || !decoder) {
        fclose(f);
        return H264_DECODE_DECODER_CREATE_FAILED;
    }

    err = esp_h264_dec_open(decoder);
    if (err != ESP_H264_ERR_OK) {
        esp_h264_dec_del(decoder);
        fclose(f);
        return H264_DECODE_DECODER_OPEN_FAILED;
    }

    // 【流水线核心】：强制在外部PSRAM中开辟两个巨大的缓存区：读取缓存与NAL缓存
    // 这避免了将几十兆文件一次性加载耗尽内部 SRAM
    uint8_t *read_buf = (uint8_t *)heap_caps_malloc(read_buffer_size + nal_buffer_size,
                                                    MALLOC_CAP_SPIRAM);
    uint8_t *nal_buf = (uint8_t *)heap_caps_malloc(nal_buffer_size, MALLOC_CAP_SPIRAM);
    if (!read_buf || !nal_buf) {
        if (read_buf) heap_caps_free(read_buf);
        if (nal_buf) heap_caps_free(nal_buf);
        esp_h264_dec_close(decoder);
        esp_h264_dec_del(decoder);
        fclose(f);
        return H264_DECODE_ALLOC_FAILED;
    }

    int buffered = 0;
    int decoded_chunks = 0;
    int output_frames = 0;

    // 循环读取文件内容，一边读取一小块数据流，一边拆分NAL，整个过程如流水线般严丝合缝
    while (1) {
        // 1. 每次从 SD 卡读取一小块数据流
        size_t got = fread(read_buf + buffered, 1, read_buffer_size, f);
        int total = buffered + (int)got;
        int sc_len = 0;
        
        // 2. 高速扫描缓存，寻找 NAL 单元的起始码（路标）
        int nal_start = find_start_code(read_buf, 0, total, &sc_len);

        if (nal_start < 0) {
            if ((size_t)total > nal_buffer_size) total = 0;
            int keep = int_min(total, 4);
            memmove(read_buf, read_buf + int_max(0, total - 4), keep);
            buffered = keep;
        } else {
            int pos = nal_start + sc_len;
            while (1) {
                int next_sc_len = 0;
                // 找到下一个起始码，从而界定出一个完整的 NAL 单元
                int next = find_start_code(read_buf, pos, total, &next_sc_len);
                if (next < 0) break;

                int nal_len = next - nal_start;
                // 3. 切分出独立的 NAL 单元，送进解码 API 处理
                if (nal_len > 0 && (size_t)nal_len <= nal_buffer_size) {
                    memcpy(nal_buf, read_buf + nal_start, nal_len);
                    output_frames += decode_nal(decoder, nal_buf, nal_len, frame_cb, user_ctx);
                    decoded_chunks++;
                }

                nal_start = next;
                pos = next + next_sc_len;
            }

            // 清理已处理的数据，为读取下一块腾出空间
            buffered = total - nal_start;
            if (buffered > 0 && (size_t)buffered <= nal_buffer_size) {
                memmove(read_buf, read_buf + nal_start, buffered);
            } else {
                buffered = 0;
            }
        }

        // 文件读取完毕，处理最后一块残留数据
        if (got == 0) {
            if (buffered > 4 && (size_t)buffered <= nal_buffer_size &&
                should_decode_nal(read_buf, buffered)) {
                memcpy(nal_buf, read_buf, buffered);
                output_frames += decode_nal(decoder, nal_buf, buffered, frame_cb, user_ctx);
            }
            break;
        }
    }

    if (stats) {
        stats->decoded_chunks = decoded_chunks;
        stats->output_frames = output_frames;
    }

    // 释放资源
    heap_caps_free(read_buf);
    heap_caps_free(nal_buf);
    esp_h264_dec_close(decoder);
    esp_h264_dec_del(decoder);
    fclose(f);
    return H264_DECODE_OK;
}

// ----------------------- 色彩转换与回调刷屏 -----------------------

/**
 * 【视频口播对应：RGB565格式转换核心】
 * 将单个像素从 YUV 空间转换到 RGB565。
 * 通过位运算和乘法，将亮度和色度数据拼凑成 16 位的 RGB 像素格式。
 */
static inline uint16_t yuv_to_rgb565(int y, int u, int v) {
    int r = y + ((v * 359) >> 8);
    int g = y - ((u * 88 + v * 183) >> 8);
    int b = y + ((u * 454) >> 8);

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    uint16_t rgb = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (rgb >> 8) | (rgb << 8);
}

static inline int mb_align(int dim) {
    return (dim + 15) & ~15;
}

/**
 * 【视频口播对应：第三步，I420转RGB565】
 * 视频解码出来的是 I420 格式，而我们的 ST7735 屏幕只认识 RGB565。
 * 所以利用这个函数做色彩空间转换，遍历每一个像素并转换。
 */
static void i420_to_rgb565(const uint8_t *i420_buf, uint8_t *rgb565_buf,
                           int width, int height) {
    uint16_t *rgb565 = (uint16_t *)rgb565_buf;
    int stride_w = mb_align(width);
    int stride_h = mb_align(height);

    const uint8_t *y_plane = i420_buf;
    const uint8_t *u_plane = i420_buf + stride_w * stride_h;
    const uint8_t *v_plane = u_plane + (stride_w / 2) * (stride_h / 2);
    int half_stride = stride_w / 2;

    for (int j = 0; j < height; j++) {
        const uint8_t *y_row = y_plane + j * stride_w;
        const uint8_t *u_row = u_plane + (j / 2) * half_stride;
        const uint8_t *v_row = v_plane + (j / 2) * half_stride;

        for (int i = 0; i < width; i++) {
            rgb565[j * width + i] =
                yuv_to_rgb565(y_row[i], u_row[i / 2] - 128, v_row[i / 2] - 128);
        }
    }
}

/**
 * 【视频口播对应：解码回调与 DMA 刷屏对接】
 * 当成功解出一帧完整画面时，解码核心会触发这个回调。
 * 我们在这里把转换好的 RGB565 画面压入队列，随后由显示任务推向 SPI DMA。
 */
void on_h264_frame(const uint8_t *i420_buf, size_t i420_size, void *user_ctx) {
    (void)i420_size;
    (void)user_ctx;

    uint8_t *frame_buf = NULL;
    // 从空闲队列获取一个缓存
    if (xQueueReceive(empty_queue, &frame_buf, portMAX_DELAY) == pdTRUE) {
        // 色彩空间转换
        i420_to_rgb565(i420_buf, frame_buf, IMG_WIDTH, IMG_HEIGHT);
        // 压入成品队列，交给显示任务去刷屏
        xQueueSend(frame_queue, &frame_buf, portMAX_DELAY);
    }
}
