#include <Arduino.h>
#include <SD_MMC.h>
#include "AudioGeneratorAAC.h"
#include "AudioOutputI2S.h"
#include "AudioFileSource.h"

// ==================== 1. 硬件引脚定义 ====================
// MAX98357A I2S Speaker (Output)
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

// ==================== 2. 封装 SDMMC 文件源 ====================
// 为了确保兼容性并避免引用冲突，我们自己封装一个继承自 AudioFileSource 的 SD_MMC 播放源
class AudioFileSourceSDMMC : public AudioFileSource {
  public:
    AudioFileSourceSDMMC(const char *filename) {
        f = SD_MMC.open(filename, FILE_READ);
    }
    virtual ~AudioFileSourceSDMMC() { if (f) f.close(); }
    virtual uint32_t read(void *data, uint32_t len) override {
        if (!f) return 0;
        return f.read((uint8_t*)data, len);
    }
    virtual bool seek(int32_t pos, int dir) override {
        if (!f) return false;
        return f.seek(pos, (SeekMode)dir);
    }
    virtual bool close() override { if (f) f.close(); return true; }
    virtual bool isOpen() override { return f; }
    virtual uint32_t getSize() override { return f ? f.size() : 0; }
    virtual uint32_t getPos() override { return f ? f.position() : 0; }
  private:
    File f;
};

// ==================== 3. 全局播放器对象 ====================
AudioGeneratorAAC *aac = NULL;
AudioFileSourceSDMMC *file = NULL;
AudioOutputI2S *out = NULL;

//

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- ESP32-S3 AAC 单文件解码播放 ---");

    // 1. 初始化 SD_MMC
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    // 第一个参数挂载点，第二个 false(代表4-bit)，第三个 true(挂载失败时格式化)
    if (!SD_MMC.begin("/sdcard", false, true)) {
        Serial.println("[Error] SD Card Mount Failed!");
        return;
    }
    Serial.println("[Info] SD Card Mounted successfully.");

    // 2. 初始化 I2S 音频输出
    out = new AudioOutputI2S();
    // 设置自定义引脚：BCLK, LRC, DOUT
    out->SetPinout(I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DOUT);
    // 设置音量 (0.0 ~ 4.0，可以按需调节)
    out->SetGain(0.5); 
    
    // 3. 打开 AAC 文件并开始解码
    // 请确保 SD 卡根目录下有 test.aac 文件
    // 转换命令
    // ffmpeg -i input.mp3 -c:a aac -b:a 128k test.aac
    Serial.println("[Info] Opening /test.aac ...");
    file = new AudioFileSourceSDMMC("/test.aac");
    aac = new AudioGeneratorAAC();

    if (file->isOpen()) {
        aac->begin(file, out);
        Serial.println("[Info] Start playing AAC...");
    } else {
        Serial.println("[Error] Failed to open /test.aac ! 请检查文件是否存在。");
    }
}

void loop() {
    // aac->loop() 负责不断的拉取数据解码并输出
    if (aac && aac->isRunning()) {
        if (!aac->loop()) {
            aac->stop();
            Serial.println("[Info] AAC Playback finished. Restarting loop...");
            
            // 重新释放并打开文件，实现循环播放
            delete file;
            file = new AudioFileSourceSDMMC("/test.aac");
            aac->begin(file, out);
        }
    }
}
