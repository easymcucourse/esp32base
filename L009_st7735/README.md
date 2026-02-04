# 📺 ESP32 中文显示实战：ST7735 屏幕驱动教学

欢迎来到 **轻松易学嵌入式** 频道！本实验将带你攻克 ESP32 中文显示的难点。我们将使用 **ST7735** 1.44"/1.8" TFT 彩色显示屏，配合强大的 `efont` 字库，在 ESP32 上实现流畅的中文文本显示。

📺 **配套视频教程**:

[![ESP32 ST7735 中文显示实战](https://img.youtube.com/vi/F5jrQzgIDLw/maxresdefault.jpg)](https://www.youtube.com/watch?v=F5jrQzgIDLw)

*点击上方图片直接跳转到 YouTube 观看*

---

## 🛠️ 实验目标：在彩色屏幕上显示中文

ST7735 是一款非常流行的 SPI 接口彩色屏。常见的 Arduino 库通常只支持 ASCII 字符（英文和数字）。本实验通过集成 efont 字体库，解决了中文字符显示的问题，让你能够为你的项目添加更友好的用户界面。

### 1. 🔍 核心技术点

*   **SPI 总线通信**: 
    使用 ESP32 的硬件 SPI 接口驱动屏幕。相比 I2C，SPI 具有更高的传输速率，刷屏更流畅。
*   **Adafruit_GFX & Adafruit_ST7735 库**: 
    这是驱动 ST7735 屏幕的黄金组合，提供了丰富的绘图 API（画点、画线、填充等）。
*   **efont 字体库集成**: 
    利用 `efont` 库将 UTF-8 编码的汉字转换为 16x16 的位图点阵，实现真正的中文字体渲染。
*   **printEfont 自定义函数**: 
    代码中实现了一个高度集成的 `printEfont` 函数，支持字体缩放、颜色设置、背景色填充以及自动换行逻辑。

### 2. ⚡ 硬件连接说明

| ST7735 屏幕引脚 | 功能 | ESP32-C3 引脚 (GPIO) |
| :--- | :--- | :--- |
| **GND** | 地 | GND |
| **VCC** | 电源 | 3.3V / 5V |
| **SCL / SCK** | SPI 时钟 | GPIO 6 |
| **SDA / MOSI** | SPI 数据 | GPIO 7 |
| **RES / RST** | 复位 | GPIO 8 |
| **DC / RS** | 数据/命令选择 | GPIO 9 |
| **CS** | 片选 | GPIO 10 |
| **BLK / LED** | 背光 | 3.3V / 5V (或推荐接 3.3V) |

---

## 🚀 实验步骤

1.  **硬件连线**: 参照上方的接线表，将 ST7735 屏幕连接到 ESP32 开发板。请务必确认 SPI 引脚是否正确。
2.  **安装必要库**:
    *   在 Arduino IDE 库管理器中搜索并安装 `Adafruit ST7735 and ST7789 Library`。
    *   安装 `Adafruit GFX Library`。
    *   安装 `efont` 库（或者确保项目目录下包含 `efont.h` 相关文件）。
3.  **上传代码**: 打开 `L009_st7735.ino`，点击上传。
4.  **观察现象**: 屏幕上应显示绿色的“Youtube频道: 轻松易学嵌入式”和红色的放大版“SPI演示”。

---

## ⚠️ 常见问题 (FAQ)

1.  **屏幕白屏或黑屏**:
    *   检查接线是否松动。
    *   确认背景灯 (BLK/LED) 是否供电。
    *   尝试在 `tft.initR()` 中更换初始化参数，如 `INITR_GREENTAB` 或 `INITR_REDTAB`。
2.  **颜色反转**: 
    如果显示的颜色与预期相反（例如红色变青色），请在 `setup` 中尝试调用 `tft.invertDisplay(true);`。
3.  **汉字乱码**:
    确保你的 `.ino` 文件编码为 **UTF-8**。
4.  **显示偏移**:
    不同厂商的 ST7735 屏幕可能存在几像素的位移，可以通过代码中 `tft.setCursor()` 手动微调位置。

---

## 🌟 关注我们

如果您觉得这个教程对您有帮助，请订阅我们的频道，获取更多实战干货：
👉 **[YouTube: 轻松易学嵌入式](https://www.youtube.com/@%E8%BD%BB%E6%9D%BE%E6%98%93%E5%AD%A6%E5%B5%8C%E5%85%A5%E5%BC%8F)**

让我们一起轻松玩转嵌入式！
