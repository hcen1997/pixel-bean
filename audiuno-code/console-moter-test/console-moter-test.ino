/*
Arduino CNC Shield驱动双NEMA17步进电机 + 串口控制/状态打印 + LED同步
最终版修正：
1. 显式初始化所有电机引脚，初始化后EN引脚默认高电平（禁用电机）
2. 恢复LED功能：任意电机运动则亮，全部停止则灭
3. 串口指令：0=切换使能状态 | 1=电机1正转 | 2=电机1反转 | 3=电机2正转 | 4=电机2反转
4. 200ms间隔打印双电机位置/速度/加速度
*/
#include <AccelStepper.h>  

// ====================== 硬件引脚定义 ======================
// 通用使能引脚（所有电机共享）
const int enablePin = 8;  

// 电机1（X轴）引脚
const int motor1DirPin = 5;   // X.DIR
const int motor1StepPin = 2;  // X.STEP
// 电机2（Y轴）引脚
const int motor2DirPin = 6;   // Y.DIR
const int motor2StepPin = 3;  // Y.STEP

// LED引脚（板载LED，通常是13）
const int ledPin = LED_BUILTIN;

// ====================== 电机参数配置 ======================
const int moveSteps = 200;    // 单次运动步数（NEMA17默认200步/圈）
const float maxSpeed = 300.0; // 最大速度
const float acceleration = 20.0; // 加速度

// ====================== 串口/状态参数 ======================
const long serialBaud = 115200; // 串口波特率
const unsigned long printInterval = 200; // 状态打印间隔（ms）
unsigned long lastPrintTime = 0; // 上一次打印时间戳

// 新增：记录当前使能状态（默认禁用）
bool motorEnabled = false;

// ====================== 步进电机对象实例化 ======================
AccelStepper motor1(1, motor1StepPin, motor1DirPin);
AccelStepper motor2(1, motor2StepPin, motor2DirPin);

void setup() {
  // 1. 初始化串口
  Serial.begin(serialBaud);
  while (!Serial); // 等待串口连接（仅Uno/Nano等需要）

  // 2. 初始化LED引脚
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW); // 初始灭灯

  // 3. 显式初始化所有电机引脚为输出模式
  pinMode(motor1StepPin, OUTPUT);
  pinMode(motor1DirPin, OUTPUT);
  pinMode(motor2StepPin, OUTPUT);
  pinMode(motor2DirPin, OUTPUT);
  
  // 4. 使能引脚初始化并置高（禁用电机，核心修改1：默认不使能）
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);
  motorEnabled = false; // 标记当前禁用状态

  // 5. 初始化电机参数
  motor1.setMaxSpeed(maxSpeed);
  motor1.setAcceleration(acceleration);
  motor1.setCurrentPosition(0);
  
  motor2.setMaxSpeed(maxSpeed);
  motor2.setAcceleration(acceleration);
  motor2.setCurrentPosition(0);

  // 移除原"最后使能电机"的代码，保持默认禁用

  // 6. 打印就绪信息（更新指令说明）
  Serial.println("系统就绪 - 串口指令说明：");
  Serial.println("0:切换使能状态 | 1:电机1正转 | 2:电机1反转 | 3:电机2正转 | 4:电机2反转");
  Serial.print("当前使能状态：");
  Serial.println(motorEnabled ? "已使能" : "禁用");
}

void loop() {
  // 1. 处理串口指令
  handleSerialCommand();

  // 2. 仅当电机使能时，才运行电机（非阻塞）
  if (motorEnabled) {
    motor1.run();
    motor2.run();
  }

  // 3. 控制LED：电机运动则亮，停止则灭
  controlLedByMotorStatus();

  // 4. 定时打印电机状态
  printMotorStatus();
}

// ====================== 串口指令处理函数（核心修改2：新增0指令） ======================
void handleSerialCommand() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    Serial.print("收到指令：");
    Serial.println(cmd);

    switch (cmd) {
      case '0': // 新增：切换使能状态
        motorEnabled = !motorEnabled; // 反转使能状态
        digitalWrite(enablePin, motorEnabled ? LOW : HIGH); // EN低电平使能，高电平禁用
        Serial.print("使能状态已切换为：");
        Serial.println(motorEnabled ? "已使能" : "禁用");
        break;
      case '1': motor1.moveTo(moveSteps); break;
      case '2': motor1.moveTo(0); break;
      case '3': motor2.moveTo(moveSteps); break;
      case '4': motor2.moveTo(0); break;
      default: Serial.println("无效指令！仅支持：0/1/2/3/4"); break;
    }
  }
}

// ====================== LED控制函数 ======================
void controlLedByMotorStatus() {
  // 只有电机使能且运动时，LED才亮
  bool motor1Moving = (motor1.speed() != 0);
  bool motor2Moving = (motor2.speed() != 0);

  if (motorEnabled && (motor1Moving || motor2Moving)) {
    digitalWrite(ledPin, HIGH);
  } else {
    digitalWrite(ledPin, LOW);
  }
}

// ====================== 电机状态打印函数（新增使能状态打印） ======================
void printMotorStatus() {
  unsigned long currentTime = millis();
  if (currentTime - lastPrintTime >= printInterval) {
    lastPrintTime = currentTime;

    Serial.print("使能状态：");
    Serial.print(motorEnabled ? "已使能" : "禁用");
    Serial.print(" | 电机1：");
    Serial.print(motor1.currentPosition());
    Serial.print(",");
    Serial.print(motor1.speed());
    Serial.print(",");
    Serial.print(motor1.acceleration());
    Serial.print(" | 电机2：");
    Serial.print(motor2.currentPosition());
    Serial.print(",");
    Serial.print(motor2.speed());
    Serial.print(",");
    Serial.println(motor2.acceleration());
  }
}
