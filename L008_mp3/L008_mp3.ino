#include "Arduino.h"
#include "DFRobotDFPlayerMini.h"

// 使用 HardwareSerial，您可以根据 ESP32-C3 的引脚定义进行修改
// 这里我们使用 Serial1，并指定 RX 和 TX 引脚
HardwareSerial mySoftwareSerial(1);
DFRobotDFPlayerMini myDFPlayer;

// 定义控制播放的 GPIO 引脚
const int stopButton = 20;      // 将 GPIO 20 定义为停止按钮
const int nextTrackButton = 21; // 将 GPIO 21 定义为下一曲按钮

void setup() {
  // 初始化串口用于调试
  Serial.begin(115200);

  // 初始化与 DFPlayer Mini 通信的串口
  // 根据您的接线修改这里的引脚号 (RX, TX)
  mySoftwareSerial.begin(9600, SERIAL_8N1, 7, 6);

  Serial.println();
  Serial.println(F("正在初始化 DFPlayer Mini ..."));

  // 检查 DFPlayer Mini 是否成功连接
  if (!myDFPlayer.begin(mySoftwareSerial)) {
    Serial.println(F("无法连接 DFPlayer Mini:"));
    Serial.println(F("1. 请检查接线是否正确"));
    Serial.println(F("2. 请确认 SD 卡已插入"));
    while(true);
  }
  Serial.println(F("DFPlayer Mini 初始化完成."));

  // 设置音量 (0~30)
  myDFPlayer.volume(30);

  // 设置 GPIO 引脚为输入模式，并启用上拉电阻
  pinMode(stopButton, INPUT_PULLUP);
  pinMode(nextTrackButton, INPUT_PULLUP);
}

void loop() {
  // 检测 GPIO 20 (停止按钮) 是否被按下 (低电平触发)
  if (digitalRead(stopButton) == LOW) {
    Serial.println("暂停播放");
    myDFPlayer.pause(); // 发送暂停命令
    delay(500); // 简单的按键消抖
  }

  // 检测 GPIO 21 (下一曲按钮) 是否被按下 (低电平触发)
  if (digitalRead(nextTrackButton) == LOW) {
    Serial.println("播放下一曲");
    myDFPlayer.next(); // 播放下一首音乐
    delay(500); // 简单的按键消抖
  }

  // 可以在这里添加代码来打印 DFPlayer Mini 的状态信息
  if (myDFPlayer.available()) {
    printDetail(myDFPlayer.readType(), myDFPlayer.read());
  }
}

// 打印 DFPlayer Mini 的详细信息 (此函数未作修改)
void printDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}