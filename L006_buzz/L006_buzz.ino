/***********************************************
 * Public Constants
 *************************************************/

#define NOTE_B0  31
#define NOTE_C1  33
#define NOTE_CS1 35
#define NOTE_D1  37
#define NOTE_DS1 39
#define NOTE_E1  41
#define NOTE_F1  44
#define NOTE_FS1 46
#define NOTE_G1  49
#define NOTE_GS1 52
#define NOTE_A1  55
#define NOTE_AS1 58
#define NOTE_B1  62
#define NOTE_C2  65
#define NOTE_CS2 69
#define NOTE_D2  73
#define NOTE_DS2 78
#define NOTE_E2  82
#define NOTE_F2  87
#define NOTE_FS2 93
#define NOTE_G2  98
#define NOTE_GS2 104
#define NOTE_A2  110
#define NOTE_AS2 117
#define NOTE_B2  123
#define NOTE_C3  131
#define NOTE_CS3 139
#define NOTE_D3  147
#define NOTE_DS3 156
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_FS3 185
#define NOTE_G3  196
#define NOTE_GS3 208
#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976
#define NOTE_C7  2093
#define NOTE_CS7 2217
#define NOTE_D7  2349
#define NOTE_DS7 2489
#define NOTE_E7  2637
#define NOTE_F7  2794
#define NOTE_FS7 2960
#define NOTE_G7  3136
#define NOTE_GS7 3322
#define NOTE_A7  3520
#define NOTE_AS7 3729
#define NOTE_B7  3951
#define NOTE_C8  4186
#define NOTE_CS8 4435
#define NOTE_D8  4699
#define NOTE_DS8 4978

/*******************************************************************
 * ESP32-C3 SuperMini: 使用 LEDC 底层函数驱动无源蜂鸣器奏乐
 * * 功能: 播放一段《小星星》旋律
 * 日期: 2025年9月17日
 * 地点: youtube频道 轻松易学嵌入式
 * * 核心原理:
 * 1. 使用 LEDC 的一个 PWM 通道来生成方波信号。
 * 2. 通过动态改变该通道的频率 (ledcChangeFrequency)，来产生不同的音高。
 * 3. 通过 delay() 函数来控制每个音符的持续时间（节拍）。
 *********************************************************************/


// --- 引脚和LEDC通道定义 ---
const int buzzerPin = 5; // 定义蜂鸣器连接到 GPIO5

const int ledcChannel = 0;      // 使用 LEDC 的通道 0
const int ledcResolution = 8;   // 设置分辨率为 8 位 (0-255)。对于蜂鸣器足够了。
const int initialFrequency = 1000; // 初始频率，随便设置一个即可，后面会动态改变

// --- 乐谱定义 ---
// 《小星星》的旋律音符数组
// Twinkle, Twinkle, Little Star Melody Array
int melody[] = {
  NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4,
  NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4, NOTE_D4, NOTE_C4,
  NOTE_G4, NOTE_G4, NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4,
  NOTE_G4, NOTE_G4, NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4,
  NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4,
  NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4, NOTE_D4, NOTE_C4
};

// 每个音符的节拍数组
// 4 = 四分音符, 2 = 二分音符, 8 = 八分音符
int noteDurations[] = {
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2
};


void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C3 LEDC Buzzer Music Test");

  // 初始化 LEDC PWM 通道
  // 注意这里使用esp32的arduino api 3.3
  // 其他版本用法不同， 参考最新文档
  ledcAttach(buzzerPin, initialFrequency, ledcResolution);
}

void loop() {

  Serial.println("Playing music...");
  playSong();
  Serial.println("Music finished.");
  delay(2000);
}

// 播放歌曲的函数
void playSong() {
  // 遍历乐谱中的每一个音符
  int numberOfNotes = sizeof(melody) / sizeof(melody[0]);
  for (int thisNote = 0; thisNote < numberOfNotes; thisNote++) {

    // 计算音符的持续时间（毫秒）
    // 1000ms / 4 = 250ms (四分音符)
    // 这里的 1.3 是一个速度系数，可以调整来改变播放速度
    int noteDuration = 1000 / noteDurations[thisNote];
    
    // 3. 改变 PWM 频率来播放当前音符
    ledcChangeFrequency(buzzerPin, melody[thisNote], ledcResolution);

    // 4. 写入占空比来发声
    // 写入 50% 的占空比 (128 / 255) 来让蜂鸣器发出最响亮的声音
    ledcWrite(buzzerPin, 128);
    
    // 保持这个音符的持续时间
    delay(noteDuration * 1.30);
    
    // 5. 停止发声，制造音符间的短暂间隔
    ledcWrite(buzzerPin, 0);
    
    // 短暂的停顿，让音符之间更清晰
    delay(noteDuration * 0.30);
  }
}