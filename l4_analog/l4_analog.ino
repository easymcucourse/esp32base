void setup() {
  // (仅用于ESP32) 设置ADC的衰减度，以确保能测量0-3.3V的全范围电压。
  //    如果没有这行，默认量程可能只有0-1.1V，导致读数不准。
  //  *严重警告* 
  // esp32版本3.3使用api有变化，网络上的代码绝大多数和3.3不兼容
  // 务必参考最新官方手册编写代码
  analogSetAttenuation(ADC_11db);

  // 步骤 1: 配置参数 (ledcSetup)
  // 为指定的引脚，设定频率(freq)和分辨率(resolution)
  ledcAttach(5, 5000, 12);

  pinMode(0, INPUT);
}

void loop() {
  // 读取摇杆的模拟值 (范围 0 - 4095)
  int joystickValue = analogRead(0);

  // 步骤 3: 写入数值 (ledcWrite)
  // 向我们配置好的PWM通道(channel)写入摇杆的数值(value)
  // 因为ADC和PWM分辨率都是12位，所以无需map()函数即可完美对应
  ledcWrite(5, joystickValue);

 
  // 短暂延时
  delay(10);
}