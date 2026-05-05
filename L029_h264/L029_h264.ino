#include <Arduino.h>
#include <SD_MMC.h>
#include "FS.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "h264_decode.h"
#include "AudioGeneratorAAC.h"
#include "AudioOutputI2S.h"
#include "AudioFileSourceFS.h"

// ESP32-S3 N16R8
// MAX98357A I2S Speaker
#define I2S_SPK_DOUT 40
#define I2S_SPK_BCLK 41
#define I2S_SPK_LRC  42

// SDMMC 4-bit
#define SD_D0  5
#define SD_D1  4
#define SD_D2  16
#define SD_D3  15
#define SD_CLK 7
#define SD_CMD 6

// ST7735 LCD SPI
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   10
#define PIN_NUM_DC   13
#define PIN_NUM_RST  14

#define IMG_WIDTH  160
#define IMG_HEIGHT 128
#define FRAME_SIZE (IMG_WIDTH * IMG_HEIGHT * 2)
#define SPI_SPEED  40000000

#define TARGET_FPS        30
#define FRAME_INTERVAL_US (1000000 / TARGET_FPS)
#define MAX_BUFFER_COUNT  30
#define NAL_BUFFER_SIZE   (128 * 1024)
#define READ_BUFFER_SIZE  (64 * 1024)

// ffmpeg -i input.mp4 -vn -c:a aac -ar 22050 -ac 1 -b:a 64k test.aac
#define SD_AUDIO_PATH "/test.aac"
// ffmpeg -i input.mp4 -an -vf "scale=160:128,fps=30,format=yuv420p" -c:v libx264 -profile:v baseline -level 3.0 -x264-params "bframes=0:cabac=0:ref=1:keyint=30:min-keyint=30:scenecut=0:repeat-headers=1" -b:v 350k -maxrate 350k -bufsize 700k -f h264 video.h264
#define SD_VIDEO_PATH "/sdcard/video.h264"

AudioGeneratorAAC *aac = NULL;
AudioFileSourceFS *audio_file = NULL;
AudioOutputI2S *audio_out = NULL;

volatile spi_device_handle_t spi_handle = NULL;
QueueHandle_t frame_queue = NULL;
QueueHandle_t empty_queue = NULL;
uint8_t *video_buffers[MAX_BUFFER_COUNT] = {};
uint32_t actual_buffer_count = 0;
uint16_t *dma_buffer = NULL;

void IRAM_ATTR lcd_spi_pre_cb(spi_transaction_t *t) {
    gpio_set_level((gpio_num_t)PIN_NUM_DC, (int)t->user);
}

void lcd_cmd(uint8_t cmd) {
    if (!spi_handle) return;
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    t.user = (void *)0;
    spi_device_polling_transmit(spi_handle, &t);
}

void lcd_data(const uint8_t *data, int len) {
    if (!spi_handle || len <= 0) return;
    spi_transaction_t t = {};
    t.length = (uint32_t)len * 8;
    t.tx_buffer = data;
    t.user = (void *)1;
    spi_device_polling_transmit(spi_handle, &t);
}

void init_st7735() {
    gpio_set_direction((gpio_num_t)PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_NUM_RST, 0);
    delay(50);
    gpio_set_level((gpio_num_t)PIN_NUM_RST, 1);
    delay(120);

    lcd_cmd(0x01);
    delay(120);
    lcd_cmd(0x11);
    delay(120);
    lcd_cmd(0xB1);
    const uint8_t fr[] = {0x00, 0x01, 0x01};
    lcd_data(fr, sizeof(fr));
    lcd_cmd(0x36);
    uint8_t madctl = 0xA0;
    lcd_data(&madctl, 1);
    lcd_cmd(0x3A);
    uint8_t color_mode = 0x05;
    lcd_data(&color_mode, 1);
    lcd_cmd(0x29);
}



void h264_decode_task(void *pvParameters) {
    while (1) {
        Serial.println("[Video] H264 playback started");
        h264_decode_stats_t stats = {};
        h264_decode_result_t result = h264_decode_file_once(SD_VIDEO_PATH,
                                                            READ_BUFFER_SIZE,
                                                            NAL_BUFFER_SIZE,
                                                            on_h264_frame,
                                                            NULL,
                                                            &stats);

        if (result == H264_DECODE_OPEN_FAILED) {
            Serial.printf("[Video] Failed to open %s\n", SD_VIDEO_PATH);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        if (result == H264_DECODE_DECODER_CREATE_FAILED) {
            Serial.println("[Video] H264 decoder create failed");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        if (result == H264_DECODE_DECODER_OPEN_FAILED) {
            Serial.println("[Video] H264 decoder open failed");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        if (result == H264_DECODE_ALLOC_FAILED) {
            Serial.println("[Video] Buffer allocation failed");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        Serial.printf("[Video] EOF, chunks: %d, frames: %d. Looping...\n",
                      stats.decoded_chunks,
                      stats.output_frames);
    }
}

void lcd_display_task(void *pvParameters) {
    uint8_t *frame = NULL;
    uint32_t frame_count = 0;
    uint32_t last_log_time = millis();

    while (1) {
        uint32_t frame_start_us = micros();

        if (xQueueReceive(frame_queue, &frame, portMAX_DELAY) == pdTRUE) {
            lcd_cmd(0x2A);
            uint8_t c[] = {0, 0, 0, IMG_WIDTH - 1};
            lcd_data(c, sizeof(c));
            lcd_cmd(0x2B);
            uint8_t r[] = {0, 0, 0, IMG_HEIGHT - 1};
            lcd_data(r, sizeof(r));
            lcd_cmd(0x2C);

            for (int i = 0; i < 4; i++) {
                size_t sz = IMG_WIDTH * 32 * 2;
                memcpy(dma_buffer, frame + i * sz, sz);

                spi_transaction_t t = {};
                t.length = sz * 8;
                t.tx_buffer = dma_buffer;
                t.user = (void *)1;
                spi_device_polling_transmit(spi_handle, &t);
            }

            xQueueSend(empty_queue, &frame, portMAX_DELAY);

            uint32_t used_us = micros() - frame_start_us;
            while ((micros() - frame_start_us) < FRAME_INTERVAL_US) {
                asm("nop");
            }

            frame_count++;
            uint32_t now = millis();
            if (now - last_log_time >= 1000) {
                Serial.printf("[Video] FPS: %u | cached: %u | load: %u%% | PSRAM: %u KB\n",
                              frame_count,
                              (unsigned)uxQueueMessagesWaiting(frame_queue),
                              (unsigned)((used_us * 100) / FRAME_INTERVAL_US),
                              (unsigned)(ESP.getFreePsram() / 1024));
                frame_count = 0;
                last_log_time = now;
            }
        }
    }
}

bool init_video_buffers() {
    for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
        video_buffers[i] = (uint8_t *)heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_SPIRAM);
        if (!video_buffers[i]) break;
        actual_buffer_count++;
    }

    dma_buffer = (uint16_t *)heap_caps_malloc(IMG_WIDTH * 32 * 2, MALLOC_CAP_DMA);
    frame_queue = xQueueCreate(actual_buffer_count, sizeof(uint8_t *));
    empty_queue = xQueueCreate(actual_buffer_count, sizeof(uint8_t *));

    if (actual_buffer_count < 4 || !dma_buffer || !frame_queue || !empty_queue) {
        Serial.println("[Video] Not enough memory for video buffers");
        return false;
    }

    for (uint32_t i = 0; i < actual_buffer_count; i++) {
        xQueueSend(empty_queue, &video_buffers[i], 0);
    }

    Serial.printf("[Video] Allocated %u frame buffers\n", actual_buffer_count);
    return true;
}

bool init_lcd_spi() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = PIN_NUM_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = PIN_NUM_CLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = FRAME_SIZE;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        Serial.printf("[LCD] spi_bus_initialize failed: 0x%x\n", ret);
        return false;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = SPI_SPEED;
    devcfg.spics_io_num = PIN_NUM_CS;
    devcfg.queue_size = 8;
    devcfg.pre_cb = lcd_spi_pre_cb;

    spi_device_handle_t temp_handle = NULL;
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &temp_handle);
    if (ret != ESP_OK) {
        Serial.printf("[LCD] spi_bus_add_device failed: 0x%x\n", ret);
        return false;
    }

    spi_handle = temp_handle;
    init_st7735();
    return true;
}

void start_aac() {
    Serial.printf("[Audio] Opening %s\n", SD_AUDIO_PATH);
    audio_file = new AudioFileSourceFS(SD_MMC, SD_AUDIO_PATH);
    aac = new AudioGeneratorAAC();

    if (audio_file->isOpen()) {
        aac->begin(audio_file, audio_out);
        Serial.println("[Audio] AAC playback started");
    } else {
        Serial.printf("[Audio] Failed to open %s\n", SD_AUDIO_PATH);
    }
}

void restart_aac() {
    if (aac) aac->stop();
    if (audio_file) {
        delete audio_file;
        audio_file = NULL;
    }
    start_aac();
}

void aac_play_task(void *pvParameters) {
    while (1) {
        if (aac && aac->isRunning()) {
            if (!aac->loop()) {
                Serial.println("[Audio] AAC finished, restarting");
                restart_aac();
            }
        } else if (aac) {
            restart_aac();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- ESP32-S3 AAC + H264 Player ---");

    if (!psramInit()) {
        Serial.println("[Error] PSRAM init failed");
        return;
    }

    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    if (!SD_MMC.begin("/sdcard", false, true)) {
        Serial.println("[Error] SD card mount failed");
        return;
    }
    Serial.println("[Info] SD card mounted");

    audio_out = new AudioOutputI2S();
    audio_out->SetPinout(I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DOUT);
    audio_out->SetRate(22050);
    audio_out->SetGain(0.5);
    start_aac();

    if (!init_video_buffers()) return;
    if (!init_lcd_spi()) return;

    xTaskCreatePinnedToCore(h264_decode_task, "H264Decode", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(lcd_display_task, "LCD", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(aac_play_task, "AACPlay", 8192, NULL, 6, NULL, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
