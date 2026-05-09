[中文版 README](README_CN.md)

# ESP32-S3 H.264 & AAC Video Player

## 1. Official Decoding Library Installation

This project requires the official Espressif decoding library (`Espressif_Multimedia_Official`) to be installed in your global Arduino libraries folder. **The Arduino IDE cannot link static libraries (`.a` files) if they are just located inside the sketch folder.**

### Installation Steps:

1. **Locate your Arduino libraries folder:**
   - Windows: `C:\Users\<YourUsername>\Documents\Arduino\libraries\`
   - Mac: `~/Documents/Arduino/libraries/`
   - Linux: `~/Arduino/libraries/`

2. **Copy the Library:**
   Copy the entire `Espressif_Multimedia_Official` folder from this project directory (`L029_h264/Espressif_Multimedia_Official`) and paste it directly into your Arduino `libraries` folder.

3. **Verify the Structure:**
   Ensure the path looks exactly like this:
   `.../Arduino/libraries/Espressif_Multimedia_Official/library.properties`

4. **Restart Arduino IDE:**
   Close and reopen the Arduino IDE. This is required so the IDE can detect the newly added library and correctly link its pre-compiled static libraries (`libesp_extractor.a`, `libtinyh264.a`, `libopenh264.a`) during compilation.

---

## 2. Project Overview

This project demonstrates how to play a video file containing an H.264 video stream and an AAC audio stream from an SD card on an ESP32-S3. 

It uses an ST7735 LCD display for video output via SPI DMA, and an I2S DAC (like MAX98357A) for audio output.

### Hardware Setup
- **Microcontroller:** ESP32-S3 (Tested on N16R8)
- **Audio:** MAX98357A I2S Amplifier
- **Storage:** SD Card (SDMMC 4-bit mode)
- **Display:** ST7735 SPI LCD (160x128)

### Media Preparation
You need to convert your video into separate `.h264` and `.aac` files using FFmpeg:

**Audio (AAC):**
```bash
ffmpeg -i input.mp4 -vn -c:a aac -ar 22050 -ac 1 -b:a 64k 002.aac
```

**Video (H.264 raw stream):**
```bash
ffmpeg -i input.mp4 -an -vf "scale=160:128,fps=30,format=yuv420p" -c:v libx264 -profile:v baseline -level 3.0 -x264-params "bframes=0:cabac=0:ref=1:keyint=30:min-keyint=30:scenecut=0:repeat-headers=1" -b:v 350k -maxrate 350k -bufsize 700k -f h264 002.h264
```
