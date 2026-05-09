[English Version](README.md)

# ESP32-S3 H.264 & AAC 视频播放器

## 1. 官方解码库安装说明

本项目依赖乐鑫官方的解码库 (`Espressif_Multimedia_Official`)，必须将其安装到全局的 Arduino 库文件夹中。**如果这些预编译的静态库文件（`.a` 文件）只放在项目目录下，Arduino IDE 无法正确进行链接。**

### 安装步骤：

1. **定位你的 Arduino 库文件夹：**
   - Windows: `C:\Users\<你的用户名>\Documents\Arduino\libraries\`
   - Mac: `~/Documents/Arduino/libraries/`
   - Linux: `~/Arduino/libraries/`

2. **复制库文件夹：**
   将本项目目录下的整个 `Espressif_Multimedia_Official` 文件夹（位于 `L029_h264/Espressif_Multimedia_Official`）直接复制并粘贴到上述的 Arduino `libraries` 文件夹中。

3. **确认目录结构：**
   请确保复制后的路径结构如下：
   `.../Arduino/libraries/Espressif_Multimedia_Official/library.properties`

4. **重启 Arduino IDE：**
   关闭并重新打开 Arduino IDE。这是必要步骤，以便 IDE 能检测到新添加的库，并在编译时正确链接其预编译的静态库（`libesp_extractor.a`, `libtinyh264.a`, `libopenh264.a`）。

---

## 2. 项目概览

本项目演示了如何使用 ESP32-S3 从 SD 卡播放包含 H.264 视频流和 AAC 音频流的媒体文件。

它通过 SPI DMA 将视频画面输出到 ST7735 LCD 显示屏，并通过 I2S DAC（如 MAX98357A）输出音频。

### 硬件连接
- **主控:** ESP32-S3 (在 N16R8 上测试)
- **音频:** MAX98357A I2S 功放模块
- **存储:** SD 卡模块 (SDMMC 4-bit 模式)
- **显示屏:** ST7735 SPI LCD (160x128 分辨率)

### 媒体文件准备
你需要使用 FFmpeg 将视频文件分离并转换为对应的 `.h264` 视频流和 `.aac` 音频文件：

**音频转换 (AAC):**
```bash
ffmpeg -i input.mp4 -vn -c:a aac -ar 22050 -ac 1 -b:a 64k 002.aac
```

**视频转换 (H.264 裸流):**
```bash
ffmpeg -i input.mp4 -an -vf "scale=160:128,fps=30,format=yuv420p" -c:v libx264 -profile:v baseline -level 3.0 -x264-params "bframes=0:cabac=0:ref=1:keyint=30:min-keyint=30:scenecut=0:repeat-headers=1" -b:v 350k -maxrate 350k -bufsize 700k -f h264 002.h264
```
