// youtube频道 轻松易学嵌入式
// 这是一个控制L298N电机驱动模块的ESP32演示代码。
// 功能：通过一个游戏摇杆控制电机的速度，并通过一个按键平滑地切换电机方向。
// 硬件连接：
// - L298N IN1 -> ESP32 GPIO 20
// - L298N IN2 -> ESP32 GPIO 21
// - L298N ENA -> ESP32 GPIO 5 (PWM控制速度)
// - 换向按键   -> ESP32 GPIO 10 (另一端接地)
// - 摇杆X/Y轴  -> ESP32 GPIO 0 (ADC读取模拟值)

// --- 全局状态变量 ---
bool isForward = true;            // 用于追踪当前的电机方向, true=正转, false=反转
int lastButtonState = HIGH;       // 用于检测按键按下的瞬间 (边沿检测)
int currentPwmValue = 0;          // 存储当前电机的PWM值，用于平滑减速

// --- 摇杆和速度配置 ---
const int maxval = 2000;              // 油门最大时对应的PWM值 (0-4095)
const int minval = 1000;              // 油门最小时对应的PWM值 (刚开始推动摇杆时)
const int joystickThrottleZone = 2400;// 摇杆油门区的下限，超过此值才开始加速
const int joystickBrakeZone = 1000;   // 摇杆刹车区的上限，低于此值触发刹车

void setup() {
  // youtube频道 轻松易学嵌入式
  
  // 启动串口通讯，波特率为115200，方便我们观察程序状态和调试
  Serial.begin(115200);

  // --- 引脚模式配置 ---

  // 配置L298N的两个方向控制引脚为输出模式
  pinMode(20, OUTPUT); // IN1引脚
  pinMode(21, OUTPUT); // IN2引脚

  // 配置换向按键的引脚为输入模式，并启用内部上拉电阻
  pinMode(10, INPUT_PULLUP);

  // --- PWM (脉冲宽度调制) 配置 ---
  // 设置针脚5的属性 (频率5000Hz, 分辨率12位)
  // 注意使用arduino api 3.3
  // 其他版本用法不同， 参考最新文档
  ledcAttach(5, 5000, 12);

  // 油门摇杆
  pinMode(0, INPUT);

  // 在串口监视器中打印一条消息，表示初始化已完成
  Serial.println("ESP32 Motor Direction Toggle Demo Initialized.");
}

void change(){
    Serial.println("---------------------------------");
    Serial.println("Direction change button pressed!");
    
    Serial.println("Slowing down...");
    for (int i = currentPwmValue; i >= 0; i -= 20) {
      ledcWrite(5, i);
      delay(10);
    }
    ledcWrite(5, 0);
    currentPwmValue = 0;
    Serial.println("Motor stopped.");
    
    isForward = !isForward;
    
    Serial.print("New direction is: ");
    Serial.println(isForward ? "FORWARD" : "REVERSE");
    
    Serial.println("Please release the button to regain control.");
    while(digitalRead(10) == LOW) {
      delay(50);
    }
    Serial.println("Button released. Control enabled.");
    Serial.println("---------------------------------");

    lastButtonState = HIGH; 
}

void loop() {
  // youtube频道 轻松易学嵌入式

  // --- 步骤一：检测换向按键是否被按下 ---
  int buttonState = digitalRead(10);
  if (buttonState == LOW && lastButtonState == HIGH) {
    // 换向
    change();
    return;
  }
  lastButtonState = buttonState;

  // --- 步骤二：根据摇杆位置控制电机状态 (油门/滑行/刹车) ---
  int joystickValue = analogRead(0);
  String motorMode = "";

  if (joystickValue > joystickThrottleZone) {
    // --- 油门区 ---
    motorMode = isForward ? "FORWARD" : "REVERSE";
    // 根据 isForward 变量设置电机方向
    if (isForward) {
      digitalWrite(20, HIGH);
      digitalWrite(21, LOW);
    } else {
      digitalWrite(20, LOW);
      digitalWrite(21, HIGH);
    }

    // 将摇杆的有效范围线性映射到我们设定的速度范围
    currentPwmValue = map(joystickValue, joystickThrottleZone, 4095, minval, maxval);
    currentPwmValue = constrain(currentPwmValue, 0, 4095);
    ledcWrite(5, currentPwmValue);

  } 

  if (joystickValue >= joystickBrakeZone &&
    joystickValue <= joystickThrottleZone) {
    // --- 滑行/死区 (1000-2200) ---
    motorMode = "COASTING (Dead Zone)";
    // 切断电机动力，让其自由滑行
    ledcWrite(5, 0);
    currentPwmValue = 0;
  }


  if (joystickValue < joystickBrakeZone) {
    // --- 刹车区 ---
    motorMode = "BRAKING";
    // 将IN1和IN2都设为LOW（或都设为HIGH），以实现电磁刹车
    digitalWrite(20, LOW);
    digitalWrite(21, LOW);
    currentPwmValue = 0; // 逻辑上速度为0

  } 

  
  

  // --- 步骤三：打印当前状态 ---
  Serial.print("Timestamp (ms): ");
  Serial.print(millis());
  Serial.print(" | Mode: ");
  Serial.print(motorMode);
  Serial.print(" | Joystick: ");
  Serial.print(joystickValue);
  Serial.print(" | Speed PWM: ");
  Serial.println(currentPwmValue);
  
  // 延时以降低串口输出频率，方便观察
  delay(200);
}