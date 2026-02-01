// --- 引脚定义 (Pin Definitions) ---
const int INPUT_PIN = 0;   // 设置为“耳朵”的输入引脚
const int LED_PIN = 21;    // 设置为“显示器”的LED输出引脚

void setup() {
  // 启动串口通信，方便我们观察
  Serial.begin(115220);
  Serial.println("--- 数字输入联动演示 ---");

  // 将LED引脚设置为输出模式
  pinMode(LED_PIN, OUTPUT);
  
  // 将INPUT_PIN (GPIO 0) 设置为最基础的输入模式
  // 注意：这里我们故意使用 INPUT，而不是 INPUT_PULLUP 或 INPUT_PULLDOWN
  // 目的是为了在后续环节中清晰地演示“引脚浮空”现象。
  pinMode(INPUT_PIN, INPUT); //浮空模式
 pinMode(INPUT_PIN, INPUT_PULLUP ); //上拉模式
 pinMode(INPUT_PIN, INPUT_PULLDOWN); //下拉模式

  // 确保LED在启动时是熄灭的
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("初始化完成，等待读取输入引脚...");
}

void loop() {
  // 1. 读取输入引脚的当前状态 (会是 HIGH 或 LOW)
  int pinState = digitalRead(INPUT_PIN);

  // 2. 根据读取到的状态，做出判断
  if (pinState == HIGH) {
    // 如果读到的是高电平，就点亮LED
    digitalWrite(LED_PIN, HIGH);
  } else {
    // 如果读到的是低电平 (或者其他任何不是HIGH的情况)，就熄灭LED
    digitalWrite(LED_PIN, LOW);
  }

}