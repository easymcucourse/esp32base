// --- 引脚定义 (Pin Definitions) ---
const int INPUT_PIN = 0;   // 输入引脚
const int LED_PIN = 21;    // LED输出引脚

// 用于统计按键次数的计数器
int pressCount = 0;
// 用于非阻塞计时的变量，记录上一次打印日志的时间点
unsigned long lastPrintTime = 0;
// 打印日志的周期 (2000毫秒 = 2秒)
const long printInterval = 2000;



void setup() {
  Serial.begin(115200);
  Serial.println("--- 上升沿检测演示 ---");

  // 设置LED引脚为输出模式
  pinMode(LED_PIN, OUTPUT);
  
  // 将输入引脚设置为“下拉输入”模式
  pinMode(INPUT_PIN, INPUT_PULLUP);

  // 确保LED在启动时是熄灭的
  digitalWrite(LED_PIN, LOW);
}

void printCount()
{
  if (millis() - lastPrintTime >= printInterval) {
    
    // a. 时间到了，打印这2秒内的按键总次数
    Serial.print("----------------------------------\n");
    Serial.print("按键次数为: ");
    Serial.println(pressCount);
    Serial.print("----------------------------------\n");

    // b. 打印完毕后，将计数器清零，为下一个周期做准备
    pressCount = 0;

    // c. 更新“上一次打印的时间点”为“现在”
    lastPrintTime = millis();
  }
}

// --- 状态变量 (State Variables) ---
// 用于存储LED的当前状态 (LOW = 灭, HIGH = 亮)
int ledState = LOW;

// 用于“记住”输入引脚上一次的状态，这是边缘检测的关键！
int lastInputState = LOW;

void loop() {
  // 1. 读取输入引脚的“当前”状态
  int currentInputState = digitalRead(INPUT_PIN);

  // 2. 核心逻辑：进行比较，判断上升沿是否发生
  //    当“上一次”的状态是LOW，并且“当前”的状态是HIGH时，
  //    就意味着一个上升沿被我们捕捉到了！
  if (lastInputState == LOW && currentInputState == HIGH) {

    // 在检测到第一个上升沿后，立即让程序暂停50毫秒时间。
    // 这段时间足以让按键的物理弹跳结束，从而让后续的
    // 循环不会再错误地检测到因抖动产生的“伪”上升沿。
    delay(50); //暂停50毫秒

    pressCount++;
    // 切换LED的状态
    if (ledState == LOW) {
      ledState = HIGH; // 如果当前LED状态是LOW（灭），则把它变成HIGH（亮）
    } else {
      ledState = LOW; // 否则（说明当前LED状态是HIGH），则把它变成LOW（灭）
    }
    digitalWrite(LED_PIN, ledState); // 根据切换后的新状态，更新LED
  }

  lastInputState = currentInputState; //    将“当前”的状态赋值给“上一次”的状态，为下一次循环做准备

  // 打印按键次数
  printCount();
  
  // YouTube频道 (Channel): 轻松易学嵌入式
 
}