/*
Arduino CNC Shield驱动双NEMA17步进电机 + 串口控制/状态打印 + LED同步
新增功能：双电机互斥运动（一个运动时，拒绝另一个的控制指令）
核心修正：
1. 显式初始化所有电机引脚，初始化后EN引脚默认高电平（禁用电机）
2. 恢复LED功能：任意电机运动则亮，全部停止则灭
3. 串口指令：0=切换使能状态 | 1=电机1正转 | 2=电机1反转 | 3=电机2正转 | 4=电机2反转
4. 200ms间隔打印双电机位置/速度/加速度
5. 双电机互斥：一个运动时，拒绝另一个的控制指令
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
  Serial.println("注意：双电机互斥运动，一个运动时拒绝另一个的指令");
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

// ====================== 串口指令处理函数（优化后） ======================
void handleSerialCommand() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    // 核心修复：过滤换行符(\n)、回车符(\r)、空格等空白字符
    if (cmd == '\n' || cmd == '\r' || cmd == ' ') {
      return; // 直接返回，不处理空白字符
    }

    Serial.print("收到指令：");
    Serial.println(cmd);

    // 先判断电机是否使能
    if (!motorEnabled && cmd != '0') {
      Serial.println("错误：电机未使能，请先发送指令0使能电机！");
      return;
    }

    // 优化：用官方isRunning()判断电机是否在运行（彻底避免浮点bug）
    bool motor1Moving = motor1.isRunning();
    bool motor2Moving = motor2.isRunning();

    switch (cmd) {
      case '0': // 切换使能状态（不受互斥限制）
        motorEnabled = !motorEnabled; 
        digitalWrite(enablePin, motorEnabled ? LOW : HIGH); 
        Serial.print("使能状态已切换为：");
        Serial.println(motorEnabled ? "已使能" : "禁用");
        break;

      case '1': // 电机1正转
        if (motor2Moving) { // 电机2正在运动，拒绝指令
          Serial.println("拒绝执行：电机2正在运动，禁止同时控制电机1！");
        } else {
          motor1.moveTo(moveSteps);
          Serial.println("执行：电机1正转");
        }
        break;

      case '2': // 电机1反转
        if (motor2Moving) {
          Serial.println("拒绝执行：电机2正在运动，禁止同时控制电机1！");
        } else {
          motor1.moveTo(0);
          Serial.println("执行：电机1反转");
        }
        break;

      case '3': // 电机2正转
        if (motor1Moving) { // 电机1正在运动，拒绝指令
          Serial.println("拒绝执行：电机1正在运动，禁止同时控制电机2！");
        } else {
          motor2.moveTo(moveSteps);
          Serial.println("执行：电机2正转");
        }
        break;

      case '4': // 电机2反转
        if (motor1Moving) {
          Serial.println("拒绝执行：电机1正在运动，禁止同时控制电机2！");
        } else {
          motor2.moveTo(0);
          Serial.println("执行：电机2反转");
        }
        break;

      default: 
        Serial.println("无效指令！仅支持：0/1/2/3/4"); 
        break;
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

// ====================== 电机状态打印函数 ======================
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
