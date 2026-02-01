# 🚀 ESP32 实战：零基础入门到避坑系列教程

欢迎来到 **轻松易学嵌入式** 的官方代码库！本项目旨在通过一系列精心设计的实战案例，带你从零开始掌握 ESP32 开发，并避开那些常见的“坑”。

📺 **YouTube 频道**: [轻松易学嵌入式](https://www.youtube.com/watch?v=qLk-9de6BaQ)

---

## 📂 课程大纲与工程导航

本项目采用分阶段学习法，每个文件夹对应一个核心知识点或实战项目。

| 章节 | 课题名称 | 核心知识点 | 状态 |
| :--- | :--- | :--- | :--- |
| **L1** | [C++20 特性解析](./L1_cplusplus) | 现代 C++ 语法、三路比较、指定初始化 | ✅ 已同步 |
| **L1** | [内存与 NVS 持久化](./L1_memory) | 内存架构、Preferences (NVS) 读写 | ✅ 已同步 |
| **L2** | [数字输入输出 (GPIO)](./L2_digital) | 引脚浮空(Floating)、INPUT_PULLUP/DOWN | ✅ 已同步 |
| **L3** | [按键检测与消抖](./L3_button) | 边缘检测(Edge Detection)、软件消抖 | ✅ 已同步 |
| **L25** | [重力骰子 (Web 版)](./L25_dice_mpu6050) | MPU6050、DMP 数据解析、Web 控制台 | ✅ 已同步 |
| **L26** | [重力骰子 (BLE 版)](./L26_dice_ble) | 蓝牙低功耗 (BLE)、手机 APP 交互 | ✅ 已同步 |

---

## 🛠️ 环境准备

为了确保代码顺利运行，建议准备以下环境：

1.  **硬件**: ESP32 开发板（如 ESP32-S3, ESP32-C3 或经典款 ESP32）。
2.  **IDE**: 
    - [Arduino IDE 2.x](https://www.arduino.cc/en/software) (安装 ESP32 开发包)。
    - 或者 VS Code + [PlatformIO](https://platformio.org/)。
3.  **库支持**:
    - `I2Cdev` / `MPU6050` (用于 L25/L26)。
    - `NimBLE-Arduino` (用于 L26 BLE)。

---

## 🌟 为什么选择本教程？

*   **拒绝搬运**: 每一个 Demo 都经过真机实测。
*   **讲透原理**: 不仅仅是 `digitalWrite`，更会带你用数据手册讲解底层逻辑。
*   **互动学习**: 在 GitHub 提问或在 YouTube 评论区交流，我们会尽力回复。

---

## 🔗 快速链接

*   [Bilibili (同步更新)](https://space.bilibili.com/346852504)
*   [加入我们的社群](#) (敬请期待)

如果您觉得这个项目对您有帮助，请给一个 **Star** ⭐️，这是对我们最大的鼓励！
