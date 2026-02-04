/*
 * 【主机 Master - ESP32-S3】
 * 使用标准 Arduino SPI.h 库
 * (标准事务处理版本)
 */

#include <SPI.h>

// ---------------------------------
// S3 (Master) 引脚定义
// ---------------------------------
#define SPI_SCLK 9
#define SPI_MISO 10
#define SPI_MOSI 11
#define SPI_CS1  4   // 连接到第一台 C3
#define SPI_CS2  5   // 连接到第二台 C3

// ---------------------------------
// 通信协议 (自定义)
// ---------------------------------
#define CMD_LED_ON  0x01
#define CMD_LED_OFF 0x00

// 定义SPI设置 (在 setup 之外定义，更整洁)
SPISettings settingsC3(1000000, MSBFIRST, SPI_MODE0);

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-S3 SPI Master (标准事务处理)");

  // 1. 初始化CS引脚 (标准 Arduino GPIO 函数)
  pinMode(SPI_CS1, OUTPUT);
  pinMode(SPI_CS2, OUTPUT);
  digitalWrite(SPI_CS1, HIGH); // 默认不选中
  digitalWrite(SPI_CS2, HIGH); // 默认不选中

  // 2. 初始化 SPI 总线 (标准 Arduino SPI 函数)
  // (SCK, MISO, MOSI)
  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI);

  Serial.println("SPI 初始化完成。");
}

/**
 * @brief 向指定的从机发送一个字节的命令
 * @param csPin 要控制的从机 CS 引脚
 * @param cmd 要发送的命令 (CMD_LED_ON 或 CMD_LED_OFF)
 */
void sendCmd(int csPin, byte cmd) {
  // 在通信开始前，锁定总线并应用设置
  SPI.beginTransaction(settingsC3);

  // 1. 拉低CS线，选中目标从机
  digitalWrite(csPin, LOW);

  // 2. 传输数据
  byte response = SPI.transfer(cmd);

  // 3. 拉高CS线，释放从机
  digitalWrite(csPin, HIGH);

  // 通信结束，释放总线锁
  SPI.endTransaction();

  Serial.printf("向 CS 引脚 %d 发送命令: 0x%02X, 收到响应: 0x%02X\n", csPin, cmd, response);

  // 稍微等待，确保从机有时间处理
  delay(10);
}

void loop() {
  // 1. 第一台LED亮
  Serial.println("1. C3-1 LED 亮");
  sendCmd(SPI_CS1, CMD_LED_ON);
  delay(1000);

  // 2. 第二台LED亮
  Serial.println("2. C3-2 LED 亮");
  sendCmd(SPI_CS2, CMD_LED_ON);
  delay(1000);

  // 3. 第一台LED暗
  Serial.println("3. C3-1 LED 暗");
  sendCmd(SPI_CS1, CMD_LED_OFF);
  delay(1000);

  // 4. 第二台LED暗
  Serial.println("4. C3-2 LED 暗");
  sendCmd(SPI_CS2, CMD_LED_OFF);
  delay(1000);

  Serial.println("--- 循环结束 ---");
}