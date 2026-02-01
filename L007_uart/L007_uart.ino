// arduino代码在下期dfplayer实操中详细解说

#include <Arduino.h>
#include "HardwareSerial.h"

// =================================================================
// Pin Definitions for ESP32-C3 SUPER MINI
// =================================================================
#define BUTTON_PIN      10  // 按键引脚 (Button Pin) - A safe GPIO
#define UART_RX_PIN     6   // 自定义串口的接收引脚 (UART RX Pin) - Safe GPIOs
#define UART_TX_PIN     7   // 自定义串口的发送引脚 (UART TX Pin) - Safe GPIOs

// =================================================================
// Object Instantiation
// =================================================================
// 使用 Serial1 作为板间通信的串口
HardwareSerial SerialPort1(1);

// =================================================================
// Setup - 初始化
// =================================================================
void setup() {
  // 启动与电脑通信的“内置”串口，用于打印调试信息
  Serial.begin(115200);
  Serial.println("\nESP32-C3 Super Mini UART Ping-Pong Demo Ready.");
  Serial.println("Using RX1 on GPIO " + String(UART_RX_PIN) + " and TX1 on GPIO " + String(UART_TX_PIN));
  Serial.println("Press the button on GPIO " + String(BUTTON_PIN) + " to start...");

  // 启动板间通信的“外部”串口 (Serial1)
  // 参数: 波特率, 模式, RX引脚, TX引脚
  SerialPort1.begin(9600, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  // 设置按键引脚为输入模式，并启用内部上拉电阻
  // 按键另一端直接接地即可
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// =================================================================
// Loop - 主循环
// =================================================================
void loop() {
  // 1. 按键检测: 检查本机的按键是否被按下
  // 因为使用了INPUT_PULLUP，按键按下时引脚电平为LOW
  if (digitalRead(BUTTON_PIN) == LOW) {
    // 发送数字'0'作为起始信号
    byte start_signal = 0;
    SerialPort1.write(start_signal);
    
    // 在本机的串口监视器上打印发起信息
    Serial.print("Button pressed. Sent: ");
    Serial.println(start_signal);
    
    // 延时消抖，并防止连续发送
    delay(500); 
  }

  // 2. 数据接收: 检查是否收到了对方发来的数据
  if (SerialPort1.available() > 0) {
    // 读取接收到的字节
    byte received_number = SerialPort1.read();
    
    // 将数字加一
    byte number_to_send = received_number + 1;
    
    // 通过串口“回传”给对方
    SerialPort1.write(number_to_send);

    // 在本机的串口监视器上打印收发过程
    Serial.printf("Received: %d, Sent: %d\n", received_number, number_to_send);

    delay(1000);
  }
}