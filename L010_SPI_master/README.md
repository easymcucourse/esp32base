# 🔗 ESP32 SPI 异步通讯实战：一拖多从机控制

欢迎来到 **轻松易学嵌入式** 频道！本实验将带你深入理解 SPI（Serial Peripheral Interface）协议。我们将使用一块 **ESP32-S3** 作为主机（Master），通过 SPI 总线同时控制两块 **ESP32-C3 Super Mini** 作为从机（Slave），实现精准的点对多控制。

📺 **配套视频教程**:

[![ESP32 SPI 通信实战](https://img.youtube.com/vi/I4Km6P9Wvh0/maxresdefault.jpg)](https://www.youtube.com/watch?v=I4Km6P9Wvh0)

*点击上方图片直接跳转到 YouTube 观看*

---

## 🛠️ 实验目标：掌握 SPI 引脚映射与一拖多逻辑

SPI 是一种高速、全双工、同步的通信协议。相比 I2C，它的速度更快，非常适合传输大量数据。在本实验中，我们将实现主机轮流向两个从机发送指令，控制它们板载 LED 的亮灭。

### 1. 🔍 核心技术点

*   **SPI 四线制协议**: 
    - **SCLK (Serial Clock)**: 时钟信号，由主机产生。
    - **MOSI (Master Out Slave In)**: 主机数据输出。
    - **MISO (Master In Slave Out)**: 从机数据输出。
    - **CS (Chip Select)**: 片选信号，用于选择通信的目标从机。
*   **标准事务处理 (SPI Transactions)**: 
    使用 `SPI.beginTransaction()` 和 `SPI.endTransaction()` 确保在多设备共享总线时的通信稳定性。
*   **ESP32SPISlave 库**: 
    由于 Arduino 默认 SPI 库仅支持主机模式，我们引入了 `ESP32SPISlave` 库使 C3 能够以从机模式工作。
*   **一拖多逻辑**: 
    多个从机共享 SCLK, MOSI, MISO 引脚，但每个从机拥有独立的 **CS** 引脚。

### 2. ⚡ 硬件连接说明

#### 主机 (ESP32-S3) 与 从机 (ESP32-C3) 连线

| 信号线 | 主机 (S3) 引脚 | 从机1 (C3) 引脚 | 从机2 (C3) 引脚 |
| :--- | :--- | :--- | :--- |
| **SCLK** | GPIO 9 | GPIO 5 | GPIO 5 |
| **MOSI** | GPIO 11 | GPIO 7 | GPIO 7 |
| **MISO** | GPIO 10 | GPIO 6 | GPIO 6 |
| **CS (选从机1)** | **GPIO 4** | **GPIO 21** | - |
| **CS (选从机2)** | **GPIO 5** | - | **GPIO 21** |
| **GND** | GND | GND | GND (必须共地) |

---

## 🚀 实验步骤

1.  **硬件连线**: 按照上表连接三块开发板。**注意：所有的 GND 必须连接在一起。**
2.  **安装从机库**: 
    在库管理器中搜索并安装 `ESP32SPISlave`。
3.  **上传从机代码**: 
    将 `L010_SPI_slave.ino` 分别上传到两块 **ESP32-C3** 开发板。
4.  **上传主机代码**: 
    将 `L010_SPI_master.ino` 上传到 **ESP32-S3** 开发板。
5.  **观察现象**: 
    两块 C3 的板载 LED 将按照主机发送的指令轮流闪烁。

---

## ⚠️ 常见问题 (FAQ)

1.  **无法通信 / 收到乱码**:
    *   检查 **GND** 是否已全部共地。
    *   检查 MOSI 和 MISO 是否接反（SPI 遵循 MOSI 接 MOSI, MISO 接 MISO）。
    *   确认 SPI 频率设置（本例为 1MHz），过长的杜邦线可能需要降低频率。
2.  **从机不响应**: 
    *   确认从机 CS 引脚是否连接到了对应的 Master 控制引脚。
    *   在 C3 平台上，确保使用了正确的 `HSPI` 或 `FSPI` 编号。
3.  **库冲突**:
    *   如果编译报错，请确保只安装了一个名为 `ESP32SPISlave` 的库。

---

## 🌟 关注我们

如果您觉得这个教程对您有帮助，请订阅我们的频道，获取更多实战干货：
👉 **[YouTube: 轻松易学嵌入式](https://www.youtube.com/@%E8%BD%BB%E6%9D%BE%E6%98%93%E5%AD%A6%E5%B5%8C%E5%85%A5%E5%BC%8F)**

让我们一起轻松玩转嵌入式！
