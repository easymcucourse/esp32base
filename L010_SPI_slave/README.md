# 🔗 ESP32 SPI 异步通讯：从机端 (Slave) 

这是 SPI 通讯实验的 **从机端 (Slave)** 代码。在本实验中，从机使用 **ESP32-C3 Super Mini** 开发板，通过 `ESP32SPISlave` 库接收来自主机的 LED 控制指令。

📺 **配套视频教程**:

[![ESP32 SPI 通信实战](https://img.youtube.com/vi/I4Km6P9Wvh0/maxresdefault.jpg)](https://www.youtube.com/watch?v=I4Km6P9Wvh0)

---

## 🛠️ 从机功能说明

从机的主要任务是监听 SPI 总线，当被主机的 **CS (片选)** 信号选中时，接收主机发来的数据并根据数据内容执行相应的操作（控制 GPIO 8 上的 LED）。

### 1. 🔍 核心技术点

*   **ESP32SPISlave 库**: 
    由于 ESP32 的官方 Arduino SPI 库主要针对主机模式，本实验使用了第三方高性能库 `ESP32SPISlave` 来实现从机协议栈。
*   **全双工传输**: 
    使用 `slave.transfer(tx_buf, rx_buf, size)` 进行数据交换。从机可以在接收指令的同时，向主机回传状态数据（如本例中的 `0xA5`）。
*   **HSPI 模式**: 
    代码中配置了从机工作在 `HSPI` 模式下，并手动指定了时钟和数据引脚。

### 2. ⚡ 硬件连接 (从机侧)

| 信号线 | ESP32-C3 引脚 | 连接对象 (Master S3) |
| :--- | :--- | :--- |
| **SCLK** | GPIO 5 | Master SCLK (GPIO 9) |
| **MISO** | GPIO 6 | Master MISO (GPIO 10) |
| **MOSI** | GPIO 7 | Master MOSI (GPIO 11) |
| **CS** | **GPIO 21** | Master CSx (GPIO 4 或 5) |
| **GND** | GND | 必须与主机共地 |
| **LED** | GPIO 8 | 板载蓝色 LED |

---

## 🚀 使用技巧

1.  **库安装**: 
    确保在 Arduino IDE 中已安装 `ESP32SPISlave` 库。
2.  **多机部署**: 
    如果你有两台从机，它们可以使用相同的代码。唯一的区别在于硬件上它们的 **CS** 引脚应分别连接到主机的不同控制引脚。
3.  **调试**: 
    代码启动后，通过串口监视器（115200 波特率）可以看到 `"start spi slave"` 字样。如果 LED 不闪烁，请检查主机是否正确选中了该从机的 CS 引脚。

---

## 🌟 关联项目

*   [主机端代码 (Master)](../L010_SPI_master/)
*   [YouTube: 轻松易学嵌入式](https://www.youtube.com/@%E8%BD%BB%E6%9D%BE%E6%98%93%E5%AD%A6%E5%B5%8C%E5%85%A5%E5%BC%8F)
