/**
 * @file L028_MP4.ino
 * @brief ESP32-S3 MP4 音视频同步播放器
 * @note 
 * - 核心业务：MP4 解复用 (minimp4) + H.264 软件解码 (esp_h264) + AAC 解码 (ESP8266Audio)
 * - 多核调度：Core 0 负责文件读取、音视频解码与音量控制；Core 1 独立负责 LCD 刷新。
 * - A/V 同步：通过 FreeRTOS EventGroup 实现同步屏障，确保音视频同时起步。
 */
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
// driver/i2s.h intentionally removed - AudioOutputI2S (ESP8266Audio) manages I2S
#include <Arduino.h>
#include <FFat.h>
#include "freertos/event_groups.h"

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"
#include "esp_h264_dec.h"

extern "C" esp_h264_err_t esp_h264_dec_sw_new(const esp_h264_dec_cfg_t *cfg, esp_h264_dec_handle_t *dec);

#include "AudioGeneratorAAC.h"
#include "AudioOutputI2S.h"
#include "AudioFileSource.h"

struct AudioMsg {
    uint8_t *data;
    size_t size;
    bool eos;
};
QueueHandle_t audio_queue = NULL;

// ESP32 S3 N16R8

// ==================== 1. 硬件引脚定义 ====================
// [原有] MAX98357A I2S Speaker (Output) -> I2S_NUM_0
#define I2S_SPK_DOUT      40
#define I2S_SPK_BCLK      41
#define I2S_SPK_LRC       42


// SDMMC 4-bit (ESP32-S3)
#define SD_D0 5
#define SD_D1 4
#define SD_D2 16
#define SD_D3 15
#define SD_CLK 7
#define SD_CMD 6

// LCD SPI (ST7735)
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   10
#define PIN_NUM_DC   13
#define PIN_NUM_RST  14

// [新增] 旋转编码器 (Rotary Encoder)
#define ENCODER_CLK  1   // CLK 引脚
#define ENCODER_DT   3   // DT 引脚

// [新增] UART1 接收 (异步通信)
#define UART_RX      44  // RX 引脚
#define UART_TX      43  // TX 引脚
#define UART_BAUDRATE 9600

// ==================== 2. 参数配置 ====================
#define IMG_WIDTH  160
#define IMG_HEIGHT 128
#define FRAME_SIZE 40960         // 160*128*2
#define SPI_SPEED 40000000       

// 帧率锁定配置
#define TARGET_FPS      15
#define FRAME_INTERVAL_US  (1000000 / TARGET_FPS)
#define PRELOAD_FRAMES  (TARGET_FPS * 2)  // 缓存2秒后开始播放

// 扬声器音频配置
#define I2S_SPK_NUM     I2S_NUM_0
#define SAMPLE_RATE     44100
#define I2S_BUFFER_SIZE (1024 * 8)

// 缓存与文件配置
#define MAX_BUFFER_COUNT 50
#define FLASH_AUDIO_PATH "/audio.raw"
#define SD_AUDIO_PATH    "/sdcard/audio.raw"
#define MAX_AUDIO_COPY_SIZE (10 * 1024 * 1024)

// 音量设置
#define AUDIO_VOLUME  0.5f 

// ==================== 3. 全局对象 ====================
volatile spi_device_handle_t spi_handle = NULL;
uint32_t actual_buffer_count = 0; 
uint8_t* video_buffers[MAX_BUFFER_COUNT]; 
uint16_t* dma_buffer = NULL;          
QueueHandle_t frame_queue = NULL;     
QueueHandle_t empty_queue = NULL;     

// A/V 同步屏障：音频和 LCD 同时就绪
#define SYNC_BIT_LCD   (1 << 0)   // LCD 预加载完成
#define SYNC_BIT_AUDIO (1 << 1)   // 音频解码器就绪
#define SYNC_BITS_ALL  (SYNC_BIT_LCD | SYNC_BIT_AUDIO)
EventGroupHandle_t sync_event = NULL;

// [新增] 旋转编码器音量控制
volatile float current_volume = AUDIO_VOLUME;  // 当前音量 (0.0 ~ 1.0)
volatile int encoder_state = 0;  // 编码器状态
volatile uint32_t last_encoder_time = 0;  // 去抖动用
SemaphoreHandle_t volume_mutex = NULL;  // 音量访问互斥锁     
volatile int32_t raw_encoder_count = 0; // [新增] 原始编码器计数，由ISR更新
 
// ==================== 4. LCD 底层驱动 (保持不变) ====================
void IRAM_ATTR lcd_spi_pre_cb(spi_transaction_t *t) {
    gpio_set_level((gpio_num_t)PIN_NUM_DC, (int)t->user);
}

void lcd_cmd(const uint8_t cmd) {
    if(!spi_handle) return;
    spi_transaction_t t = {0}; t.length=8; t.tx_buffer=&cmd; t.user=(void*)0;
    spi_device_polling_transmit(spi_handle, &t);
}

void lcd_data(const uint8_t *data, int len) {
    if(!spi_handle) return;
    spi_transaction_t t = {0}; t.length=(uint32_t)len*8; t.tx_buffer=data; t.user=(void*)1;
    spi_device_polling_transmit(spi_handle, &t);
}

void init_st7735() {
    gpio_set_direction((gpio_num_t)PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_NUM_RST, 0); delay(50);
    gpio_set_level((gpio_num_t)PIN_NUM_RST, 1); delay(120);

    lcd_cmd(0x01); delay(120); 
    lcd_cmd(0x11); delay(120); 
    lcd_cmd(0xB1); const uint8_t fr[] = {0x00, 0x01, 0x01}; lcd_data(fr, 3);
    lcd_cmd(0x36); uint8_t m = 0xA0; lcd_data(&m, 1); 
    lcd_cmd(0x3A); uint8_t c = 0x05; lcd_data(&c, 1); 
    lcd_cmd(0x29); 
}

// ==================== [新增] 旋转编码器初始化与中断处理 ====================
// 编码器 CLK 引脚中断处理
void IRAM_ATTR encoder_clk_isr(void* arg) {
    uint32_t now = millis();
    if (now - last_encoder_time < 5) return;  // 去抖动 5ms
    last_encoder_time = now;
    
    int clk_level = gpio_get_level((gpio_num_t)ENCODER_CLK);
    int dt_level = gpio_get_level((gpio_num_t)ENCODER_DT);
    
    // Gray code 解码
    if (clk_level != encoder_state) {
        if (clk_level != dt_level) {
            // 顺时针 - 增大音量
            raw_encoder_count++;
        } else {
            // 逆时针 - 减小音量
            raw_encoder_count--;
        }
        encoder_state = clk_level;
    }
}

// 初始化旋转编码器
void init_rotary_encoder() {
    // 配置 GPIO
    gpio_set_direction((gpio_num_t)ENCODER_CLK, GPIO_MODE_INPUT);
    gpio_set_direction((gpio_num_t)ENCODER_DT, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)ENCODER_CLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)ENCODER_DT, GPIO_PULLUP_ONLY);
    
    // 配置中断
    gpio_set_intr_type((gpio_num_t)ENCODER_CLK, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add((gpio_num_t)ENCODER_CLK, encoder_clk_isr, NULL);
    
    Serial.println("Rotary Encoder Initialized (Volume Control)");
}

// ==================== 6. AudioFileSourceQueue & 音频播放任务 ====================
// A custom AudioFileSource that reads from a FreeRTOS queue instead of a file.
// This allows ESP8266Audio's AAC decoder to consume raw AAC frames in real-time.
class AudioFileSourceQueue : public AudioFileSource {
public:
    AudioFileSourceQueue(QueueHandle_t q) : queue(q), buf_offset(0), is_eos(false) {
        current_msg.data = NULL;
        current_msg.size = 0;
        current_msg.eos = false;
    }
    ~AudioFileSourceQueue() { if (current_msg.data) free(current_msg.data); }

    virtual uint32_t read(void *data, uint32_t len) override {
        uint32_t read_bytes = 0;
        uint8_t *dest = (uint8_t *)data;
        while (len > 0) {
            if (is_eos) break;
            if (current_msg.size == 0) {
                if (current_msg.data) { free(current_msg.data); current_msg.data = NULL; }
                if (xQueueReceive(queue, &current_msg, portMAX_DELAY)) {
                    if (current_msg.eos) { is_eos = true; break; }
                    buf_offset = 0;
                } else { break; }
            }
            uint32_t available = current_msg.size - buf_offset;
            uint32_t to_copy = (len < available) ? len : available;
            memcpy(dest, current_msg.data + buf_offset, to_copy);
            buf_offset += to_copy;
            dest += to_copy;
            read_bytes += to_copy;
            len -= to_copy;
            if (buf_offset >= current_msg.size) {
                free(current_msg.data);
                current_msg.data = NULL;
                current_msg.size = 0;
            }
        }
        return read_bytes;
    }
    virtual bool seek(int32_t pos, int dir) override { return false; }
    virtual bool close() override { return true; }
    virtual bool isOpen() override { return !is_eos; }
    virtual uint32_t getSize() override { return 0; }
    virtual uint32_t getPos() override { return 0; }

private:
    QueueHandle_t queue;
    AudioMsg current_msg;
    uint32_t buf_offset;
    bool is_eos;
};

void audio_pipeline_task(void *pvParameters) {
    Serial.println("Core 0: Audio Pipeline Task Started");
    
    AudioFileSourceQueue *file = new AudioFileSourceQueue(audio_queue);
    AudioGeneratorAAC *aac = new AudioGeneratorAAC();
    // numChannels=1: 单声道, buffSize=8192: DMA buffer ~93ms, 抗调度抖动
    AudioOutputI2S *out = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 1, 8192);
    
    out->SetPinout(I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DOUT);
    out->SetGain(current_volume);
    out->SetRate(22050);  // 降低采样率减少 I2S DMA 刷新频率和 CPU 负荷
    
    if (!aac->begin(file, out)) {
        Serial.println("AAC begin FAILED!");
        delete aac; delete out; delete file;
        vTaskDelete(NULL);
        return;
    }
    Serial.println("Audio: decoder ready, waiting for LCD...");
    
    // 音频就绪，等待 LCD 也就绪
    xEventGroupSetBits(sync_event, SYNC_BIT_AUDIO);
    xEventGroupWaitBits(sync_event, SYNC_BITS_ALL, pdFALSE, pdTRUE, portMAX_DELAY);
    Serial.println("Audio: GO! Starting playback.");
    
    {
        uint32_t decode_count = 0;
        uint32_t last_dbg_time = millis();
        while (1) {
            if (aac->isRunning()) {
                if (!aac->loop()) {
                    Serial.println("AAC loop() returned false -> EOF or error");
                    aac->stop();
                    break;
                }
                out->SetGain(current_volume);
                taskYIELD(); // yield to VideoRead on Core0

                decode_count++;
                if (millis() - last_dbg_time > 2000) {
                    Serial.printf("Audio: decoded %u loops, Vol: %.2f\n", decode_count, current_volume);
                    last_dbg_time = millis();
                }
            } else {
                vTaskDelay(1); // I2S DMA 满时自然阻塞，同时让出 Core0 给 VideoRead
                Serial.println("AAC not running. Breaking.");
                break;
            }
        }
    }
    
    delete aac;
    delete out;
    delete file;
    Serial.println("Audio pipeline task ended.");
    vTaskDelete(NULL);
}

// ==================== [新增] 任务：旋转编码器处理 ====================
void rotary_encoder_task(void *pvParameters) {
    int32_t prev_raw_encoder_count = 0;
    float local_current_volume = AUDIO_VOLUME; // 任务内部的音量副本，用于计算

    // 初始化 prev_raw_encoder_count 和 local_current_volume
    if (xSemaphoreTake(volume_mutex, portMAX_DELAY) == pdTRUE) {
        prev_raw_encoder_count = raw_encoder_count;
        local_current_volume = current_volume; // 同步初始音量
        xSemaphoreGive(volume_mutex);
    }

    Serial.println("Rotary Encoder Task Started.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50)); // 每 50ms 检查一次编码器状态

        int32_t current_raw_encoder_count_snapshot;
        if (xSemaphoreTake(volume_mutex, portMAX_DELAY) == pdTRUE) {
            current_raw_encoder_count_snapshot = raw_encoder_count;
            xSemaphoreGive(volume_mutex);
        } else {
            // 如果无法获取互斥锁，等待并重试
            continue;
        }

        if (current_raw_encoder_count_snapshot != prev_raw_encoder_count) {
            int32_t diff = current_raw_encoder_count_snapshot - prev_raw_encoder_count;

            local_current_volume += diff * 0.05f; // 每次“咔哒”声改变音量 0.05

            // 限制音量在 0.0 到 1.0 之间
            if (local_current_volume > 1.0f) local_current_volume = 1.0f;
            if (local_current_volume < 0.0f) local_current_volume = 0.0f;

            // 在互斥锁保护下更新全局 current_volume
            if (xSemaphoreTake(volume_mutex, portMAX_DELAY) == pdTRUE) {
                current_volume = local_current_volume;
                xSemaphoreGive(volume_mutex);
            }
            Serial.printf("[Encoder] Volume: %.0f%%\n", local_current_volume * 100.0f);
            prev_raw_encoder_count = current_raw_encoder_count_snapshot;
        }
    }
}

// ==================== 8. 任务：视频读取 (读取 SD) ====================
int mp4_read_cb(int64_t offset, void *buffer, size_t size, void *token) {
    FILE *f = (FILE *)token;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return 1;
    return (fread(buffer, 1, size, f) != size) ? 1 : 0;
}

static inline uint16_t yuv_to_rgb565(int y, int u, int v) {
    int r = y + ((v * 359) >> 8);
    int g = y - ((u * 88 + v * 183) >> 8);
    int b = y + ((u * 454) >> 8);

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    uint16_t rgb = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (rgb >> 8) | (rgb << 8); // bswap for SPI
}

static inline int mb_align(int dim) { return (dim + 15) & ~15; }

static void i420_to_rgb565(const uint8_t *i420_buf, uint8_t *rgb565_buf, int width, int height) {
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
            rgb565[j * width + i] = yuv_to_rgb565(
                y_row[i], u_row[i / 2] - 128, v_row[i / 2] - 128);
        }
    }
}

int build_annex_b_nal(uint8_t *dst, int capacity, const uint8_t *src, int size) {
    int dst_pos = 0;
    int src_pos = 0;

    while (src_pos < size) {
        if (src_pos + 4 > size) break;

        uint32_t nal_size = ((uint32_t)src[src_pos] << 24) |
                            ((uint32_t)src[src_pos + 1] << 16) |
                            ((uint32_t)src[src_pos + 2] << 8) |
                            ((uint32_t)src[src_pos + 3]);
        src_pos += 4;

        if ((int)(src_pos + nal_size) > size) break;
        if (dst_pos + 4 + (int)nal_size > capacity) break;

        dst[dst_pos++] = 0x00;
        dst[dst_pos++] = 0x00;
        dst[dst_pos++] = 0x00;
        dst[dst_pos++] = 0x01;

        memcpy(dst + dst_pos, src + src_pos, nal_size);
        dst_pos += nal_size;
        src_pos += nal_size;
    }
    return dst_pos;
}

void sd_file_read_task(void *pvParameters) {
    while (1) {
        FILE* f = fopen("/sdcard/video.mp4", "rb");
        if (!f) {
            Serial.println("Core 0: Failed to open video.mp4!");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        uint8_t* file_stream_buf = (uint8_t*)heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);
        if (file_stream_buf) {
            setvbuf(f, (char*)file_stream_buf, _IOFBF, 64 * 1024);
        }

        fseek(f, 0, SEEK_END);
        int64_t file_size = ftell(f);
        fseek(f, 0, SEEK_SET);

        MP4D_demux_t mp4;
        if (!MP4D_open(&mp4, mp4_read_cb, f, file_size)) {
            Serial.println("MP4D_open failed");
            fclose(f);
            if (file_stream_buf) heap_caps_free(file_stream_buf);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        int video_track = -1;
        int audio_track = -1;
        for (unsigned i = 0; i < mp4.track_count; i++) {
            if (mp4.track[i].handler_type == MP4D_HANDLER_TYPE_VIDE &&
                mp4.track[i].object_type_indication == MP4_OBJECT_TYPE_AVC) {
                video_track = i;
            }
            if (mp4.track[i].handler_type == MP4D_HANDLER_TYPE_SOUN &&
                mp4.track[i].object_type_indication == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3) {
                audio_track = i;
            }
        }

        if (video_track < 0) {
            Serial.println("No H.264 track");
            MP4D_close(&mp4);
            fclose(f);
            if (file_stream_buf) heap_caps_free(file_stream_buf);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        MP4D_track_t *v_tr = &mp4.track[video_track];
        unsigned total_frames = v_tr->sample_count;
        unsigned v_timescale = v_tr->timescale;

        unsigned total_audio_frames = 0;
        unsigned a_timescale = 0;
        unsigned a_samplerate = 44100;
        unsigned a_channels = 2;
        if (audio_track >= 0) {
            MP4D_track_t *a_tr = &mp4.track[audio_track];
            total_audio_frames = a_tr->sample_count;
            a_timescale = a_tr->timescale;
            a_samplerate = a_tr->SampleDescription.audio.samplerate_hz;
            a_channels = a_tr->SampleDescription.audio.channelcount;
            Serial.printf("Audio Track - Frames: %u, Timescale: %u, SR: %u Hz, Ch: %u\n", 
                          total_audio_frames, a_timescale, a_samplerate, a_channels);
        } else {
            Serial.println("No Audio Track Found in MP4!");
        }
        
        esp_h264_dec_cfg_t dec_cfg = {
            .pic_type = ESP_H264_RAW_FMT_I420,
        };
        esp_h264_dec_handle_t decoder = nullptr;
        esp_h264_err_t err = esp_h264_dec_sw_new(&dec_cfg, &decoder);
        if (err != ESP_H264_ERR_OK) {
            Serial.println("H264 decoder new failed");
            MP4D_close(&mp4);
            fclose(f);
            if (file_stream_buf) heap_caps_free(file_stream_buf);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        esp_h264_dec_open(decoder);
        
        uint8_t *read_buf = (uint8_t *)heap_caps_malloc(128 * 1024, MALLOC_CAP_SPIRAM);
        uint8_t *nal_buf  = (uint8_t *)heap_caps_malloc(128 * 1024, MALLOC_CAP_SPIRAM);
        
        if (!read_buf || !nal_buf) {
            Serial.println("Failed to alloc read/nal buf");
            esp_h264_dec_close(decoder);
            esp_h264_dec_del(decoder);
            MP4D_close(&mp4);
            fclose(f);
            if (file_stream_buf) heap_caps_free(file_stream_buf);
            if (read_buf) heap_caps_free(read_buf);
            if (nal_buf) heap_caps_free(nal_buf);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        int sps_bytes = 0, pps_bytes = 0;
        const void *sps = MP4D_read_sps(&mp4, video_track, 0, &sps_bytes);
        const void *pps = MP4D_read_pps(&mp4, video_track, 0, &pps_bytes);
        
        auto send_decoder = [&](const uint8_t *data, int size) {
            esp_h264_dec_in_frame_t in_frame = {};
            in_frame.raw_data.buffer = (uint8_t *)data;
            in_frame.raw_data.len = size;
            esp_h264_dec_out_frame_t out_frame = {};
            
            while (in_frame.raw_data.len > 0) {
                err = esp_h264_dec_process(decoder, &in_frame, &out_frame);
                if (err != ESP_H264_ERR_OK) break;
                if (in_frame.consume == 0) break;
                in_frame.raw_data.buffer += in_frame.consume;
                in_frame.raw_data.len -= in_frame.consume;
                
                if (out_frame.out_size > 0 && out_frame.outbuf) {
                    uint8_t *frame_buf = NULL;
                    if (xQueueReceive(empty_queue, &frame_buf, portMAX_DELAY)) {
                        i420_to_rgb565(out_frame.outbuf, frame_buf, IMG_WIDTH, IMG_HEIGHT);
                        xQueueSend(frame_queue, &frame_buf, portMAX_DELAY);
                    }
                }
            }
        };

        if (sps && sps_bytes > 0) {
            nal_buf[0]=0; nal_buf[1]=0; nal_buf[2]=0; nal_buf[3]=1;
            memcpy(nal_buf + 4, sps, sps_bytes);
            send_decoder(nal_buf, sps_bytes + 4);
        }
        if (pps && pps_bytes > 0) {
            nal_buf[0]=0; nal_buf[1]=0; nal_buf[2]=0; nal_buf[3]=1;
            memcpy(nal_buf + 4, pps, pps_bytes);
            send_decoder(nal_buf, pps_bytes + 4);
        }

        Serial.printf("Core 0: MP4 loop started. V-frames: %u, A-frames: %u\n", total_frames, total_audio_frames);
        
        // --- ADTS 采样率索引预计算 ---
        int sr_idx = 4; // default 44100
        {
            const int freqs[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
            for (int i = 0; i < 13; i++) {
                if (freqs[i] == (int)a_samplerate) { sr_idx = i; break; }
            }
        }
        
        // --- 音视频交织循环 (PTS 排序) ---
        // 音频帧多于视频帧时，将多帧音频送进队列等待消费
        unsigned v_sample = 0, a_sample = 0;
        while (v_sample < total_frames || a_sample < total_audio_frames) {
            int64_t v_pts = 0x7FFFFFFFFFFFFFFF, a_pts = 0x7FFFFFFFFFFFFFFF;
            unsigned v_bytes = 0, a_bytes = 0;
            MP4D_file_offset_t v_offset = 0, a_offset = 0;
            unsigned ts = 0, dur = 0;

            if (v_sample < total_frames) {
                v_offset = MP4D_frame_offset(&mp4, video_track, v_sample, &v_bytes, &ts, &dur);
                v_pts = (v_timescale > 0) ? (int64_t)ts * 1000000LL / v_timescale : 0;
            }
            if (a_sample < total_audio_frames) {
                a_offset = MP4D_frame_offset(&mp4, audio_track, a_sample, &a_bytes, &ts, &dur);
                a_pts = (a_timescale > 0) ? (int64_t)ts * 1000000LL / a_timescale : 0;
            }

            if (v_sample < total_frames && (v_pts <= a_pts || a_sample >= total_audio_frames)) {
                // 解码视频帧
                if (v_bytes > 0 && v_bytes <= 128 * 1024) {
                    fseek(f, v_offset, SEEK_SET);
                    if (fread(read_buf, 1, v_bytes, f) == v_bytes) {
                        int nal_size = build_annex_b_nal(nal_buf, 128 * 1024, read_buf, v_bytes);
                        if (nal_size > 0) send_decoder(nal_buf, nal_size);
                    }
                }
                v_sample++;
            } else {
                // 发送音频帧到队列
                if (a_bytes > 0 && a_bytes <= 16 * 1024) {
                    fseek(f, a_offset, SEEK_SET);
                    if (fread(read_buf, 1, a_bytes, f) == a_bytes) {
                        // 音频帧分配在内部 DRAM，避免 PSRAM SPI 延迟导致爆音
                        uint8_t *a_buf = (uint8_t *)malloc(a_bytes + 7);
                        if (a_buf) {
                            int len = a_bytes + 7;
                            a_buf[0] = 0xFF;
                            a_buf[1] = 0xF1;
                            a_buf[2] = ((2 - 1) << 6) | (sr_idx << 2) | (a_channels >> 2);
                            a_buf[3] = ((a_channels & 3) << 6) | ((len >> 11) & 3);
                            a_buf[4] = (len >> 3) & 0xFF;
                            a_buf[5] = ((len & 7) << 5) | 0x1F;
                            a_buf[6] = 0xFC;
                            memcpy(a_buf + 7, read_buf, a_bytes);
                            AudioMsg amsg = { a_buf, (size_t)len, false };
                            // portMAX_DELAY: 队列满时阻塞等待，不丢帧
                            if (xQueueSend(audio_queue, &amsg, portMAX_DELAY) != pdTRUE) {
                                free(a_buf); // 只有异常才丢
                            }
                        }
                    }
                }
                a_sample++;
            }
        }
        
        // EOS
        AudioMsg eos_msg = { NULL, 0, true };
        xQueueSend(audio_queue, &eos_msg, pdMS_TO_TICKS(500));
        
        esp_h264_dec_close(decoder);
        esp_h264_dec_del(decoder);
        MP4D_close(&mp4);
        fclose(f);
        heap_caps_free(file_stream_buf);
        heap_caps_free(read_buf);
        heap_caps_free(nal_buf);
        Serial.println("Core 0: Playback finished. Looping...");
    }
}

// ==================== 9. 任务：LCD 显示 (锁 24 FPS) ====================
void lcd_display_task(void *pvParameters) {
    uint8_t* frame = NULL;
    uint32_t frame_count = 0;
    uint32_t last_log_time = millis();
    
    uint32_t frame_start_us = 0;
    uint32_t work_duration_us = 0;

    uint32_t preload_target = min((uint32_t)PRELOAD_FRAMES, actual_buffer_count);
    Serial.printf("Core 1: Pre-loading %d frames (%.1f sec)...\n", preload_target, (float)preload_target / TARGET_FPS);

    while (uxQueueMessagesWaiting(frame_queue) < preload_target) {
        vTaskDelay(10);
    }
    Serial.println("Core 1: Preload done, waiting for audio decoder...");
    
    // LCD 就绪，等待音频也就绪
    xEventGroupSetBits(sync_event, SYNC_BIT_LCD);
    xEventGroupWaitBits(sync_event, SYNC_BITS_ALL, pdFALSE, pdTRUE, portMAX_DELAY);
    Serial.println("Core 1: GO! Video Playback Started!");

    while (1) {
        frame_start_us = micros();

        if (xQueueReceive(frame_queue, &frame, portMAX_DELAY)) {
            lcd_cmd(0x2A); uint8_t c[] = {0,0,0,IMG_WIDTH-1}; lcd_data(c, 4);
            lcd_cmd(0x2B); uint8_t r[] = {0,0,0,IMG_HEIGHT-1}; lcd_data(r, 4);
            lcd_cmd(0x2C);

            for (int i=0; i<4; i++) {
                size_t sz = IMG_WIDTH * 32 * 2;
                memcpy(dma_buffer, frame + i*sz, sz); 
                
                spi_transaction_t t = {0};
                t.length = sz * 8; 
                t.tx_buffer = dma_buffer; 
                t.user = (void*)1;
                spi_device_polling_transmit(spi_handle, &t);
            }
            
            xQueueSend(empty_queue, &frame, portMAX_DELAY);
            
            work_duration_us = micros() - frame_start_us;
            
            if (work_duration_us < FRAME_INTERVAL_US) {
                while ((micros() - frame_start_us) < FRAME_INTERVAL_US) {
                    asm("nop"); 
                }
            }

            frame_count++;
            uint32_t now = millis();
            if (now - last_log_time >= 1000) {
                UBaseType_t cached = uxQueueMessagesWaiting(frame_queue);
                Serial.printf("FPS: %d | Buf: %d/%d | Load: %d%% | FreeMem: %d KB\n", 
                              frame_count, cached, actual_buffer_count, 
                              (work_duration_us * 100) / FRAME_INTERVAL_US, 
                              ESP.getFreePsram()/1024);
                frame_count = 0;
                last_log_time = now;
            }
        }
    }
}

// ==================== 10. 系统初始化 ====================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- ESP32-S3 Hybrid Player + Mic Monitor ---");

    // 1. 初始化 PSRAM
    if (!psramInit()) { Serial.println("PSRAM Init Failed!"); return; }
    
    // 2. 初始化 I2S (扬声器 + 麦克风)

    // init_mic_i2s();  // [新增] 初始化麦克风 (暂时禁用以解决 I2S 驱动版本冲突)
    
    // 2.5 初始化旋转编码器和 GPIO ISR 服务
    volume_mutex = xSemaphoreCreateMutex();  // 创建音量互斥锁
    sync_event = xEventGroupCreate();        // 创建 A/V 同步屏障
    gpio_install_isr_service(0);  // 安装 GPIO ISR 服务
    init_rotary_encoder();  // [新增] 初始化旋转编码器

    // 3. 内存池分配
    Serial.println("Allocating video buffers...");
    Serial.flush();
    for(int i=0; i<MAX_BUFFER_COUNT; i++) {
        if (ESP.getFreePsram() < 2 * 1024 * 1024) break; 

        video_buffers[i] = (uint8_t*)heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_SPIRAM);
        if(video_buffers[i] == NULL) {
            Serial.printf("Failed at buffer %d\n", i);
            Serial.flush();
            break;
        }
        actual_buffer_count++;
    }
    Serial.printf(">>> Allocated %d Video Buffers <<<\n", actual_buffer_count);
    Serial.flush();
    if (actual_buffer_count < 10) { Serial.println("Not enough memory!"); return; }

    dma_buffer = (uint16_t*)heap_caps_malloc(IMG_WIDTH*32*2, MALLOC_CAP_DMA);

    // 4. 创建队列
    frame_queue = xQueueCreate(actual_buffer_count, sizeof(uint8_t*));
    empty_queue = xQueueCreate(actual_buffer_count, sizeof(uint8_t*));
    for(int i=0; i<actual_buffer_count; i++) xQueueSend(empty_queue, &video_buffers[i], 0);

    // 5. 挂载 SDMMC 卡 (4-bit Mode)
    esp_log_level_set("sdmmc_periph", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_common", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_init",   ESP_LOG_VERBOSE);
    esp_log_level_set("sdmmc_req", ESP_LOG_VERBOSE);
    Serial.print("Mounting SD Card... ");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_52M; 
    host.flags = SDMMC_HOST_FLAG_4BIT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = (gpio_num_t)SD_CLK; slot.cmd = (gpio_num_t)SD_CMD;
    slot.d0 = (gpio_num_t)SD_D0; slot.d1 = (gpio_num_t)SD_D1;
    slot.d2 = (gpio_num_t)SD_D2; slot.d3 = (gpio_num_t)SD_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 64 * 1024 
    };

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount_config, &card);
    if (ret != ESP_OK) { Serial.printf("Failed (0x%x)\n", ret); return; }
    Serial.println("OK");

    Serial.println("\n--- eMMC 硬件详情 ---");
    sdmmc_card_print_info(stdout, card); // 该函数会打印容量、速度、OCR/CID/CSD等

    // 6. 音频队列（200项缓冲，内容帧 malloc 在内部 DRAM）
    audio_queue = xQueueCreate(200, sizeof(AudioMsg));

    // 7. 初始化 LCD SPI
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = FRAME_SIZE
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    
    spi_device_interface_config_t devcfg = {0};
    devcfg.mode = 0; devcfg.clock_speed_hz = SPI_SPEED;
    devcfg.spics_io_num = PIN_NUM_CS; devcfg.queue_size = 20;
    devcfg.pre_cb = lcd_spi_pre_cb;
    
    spi_device_handle_t temp_handle; 
    spi_bus_add_device(SPI2_HOST, &devcfg, &temp_handle);
    spi_handle = temp_handle;

    init_st7735();

    // Core 分配:
    //   Core 0: VideoRead(5) + AudioPlay(6) + RotaryEncoder(2)
    //   Core 1: LCD(5) 独占，不会被音频占满导致视频延迟
    xTaskCreatePinnedToCore(sd_file_read_task,    "VideoRead",    8192,  NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(lcd_display_task,      "LCD",          4096,  NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(audio_pipeline_task,   "AudioPlay",    16384, NULL, 6, NULL, 0); // Core 0
    xTaskCreatePinnedToCore(rotary_encoder_task,   "RotaryEncoder",4096,  NULL, 2, NULL, 0);
}

void loop() { vTaskDelay(1000); }
