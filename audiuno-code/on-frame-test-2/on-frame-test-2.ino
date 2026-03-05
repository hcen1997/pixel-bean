/*
Arduino CNC Shield双电机+MG90S舵机控制（内存优化版）
核心优化：
1. 替换String为字符数组，减少动态内存占用
2. 合并冗余变量，移除无用全局变量
3. 优化函数内局部变量，减少栈占用
4. 保留所有功能：互斥运动、舵机控制、串口指令、状态打印
*/
#include <AccelStepper.h>  
#include <Servo.h>

// ====================== 硬件引脚定义（宏定义，不占RAM） ======================
#define enablePin 8
#define motor1DirPin 5
#define motor1StepPin 2
#define motor2DirPin 6
#define motor2StepPin 3
#define ledPin LED_BUILTIN
#define servoPin 12

// ====================== 常量定义（存在Flash，不占RAM） ======================
const int moveSteps = 200;
const float maxSpeed = 300.0;
const float acceleration = 20.0;
const int servoMinAngle = 0;
const int servoMaxAngle = 180;
const long serialBaud = 115200;
const unsigned long printInterval = 200;

// ====================== 全局变量（仅保留必需的，精简到最少） ======================
unsigned long lastPrintTime = 0;
bool motorEnabled = false;
int currentServoAngle = 90;
// 优化：用字符数组替代String，固定长度（仅占10字节，String默认占32+字节）
char serialBuffer[10] = {0}; 
uint8_t bufferIndex = 0; // 缓冲区索引，用uint8_t（1字节）替代int（2字节）

// ====================== 对象实例化（精简） ======================
AccelStepper motor1(1, motor1StepPin, motor1DirPin);
AccelStepper motor2(1, motor2StepPin, motor2DirPin);
Servo myServo;

// ====================== Flash存储的提示文本（不占RAM） ======================
// 使用F()宏，把字符串存在Flash，而非RAM
#define READY_MSG F("系统就绪 版本：23点24分 - 串口指令说明：")
#define CMD_MSG F("0:切换使能 | 1:电机1正转 | 2:电机1反转 | 3:电机2正转 | 4:电机2反转 | 5+角度（例：590）")
#define MUTEX_MSG F("注意：双电机互斥运动")

void setup() {
  Serial.begin(serialBaud);
  while (!Serial);
  while (Serial.available() > 0) Serial.read();

  // 初始化引脚（精简，无冗余）
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);

  // 舵机初始化
  myServo.attach(servoPin);
  myServo.write(currentServoAngle);
  Serial.print(F("舵机初始化角度：")); // F()宏优化
  Serial.println(currentServoAngle);

  // 电机参数初始化（精简）
  motor1.setMaxSpeed(maxSpeed);
  motor1.setAcceleration(acceleration);
  motor1.setCurrentPosition(0);
  motor2.setMaxSpeed(maxSpeed);
  motor2.setAcceleration(acceleration);
  motor2.setCurrentPosition(0);

  // 提示信息用F()宏，不占RAM
  Serial.println(READY_MSG);
  Serial.println(CMD_MSG);
  Serial.println(MUTEX_MSG);
  Serial.print(F("步进电机使能："));
  Serial.println(motorEnabled ? F("否") : F("是"));
}

void loop() {
  handleSerialCommand();

  if (motorEnabled) {
    motor1.run();
    motor2.run();
  }

  controlLedByMotorStatus();
  printMotorAndServoStatus();
}

// ====================== 串口指令处理（核心内存优化） ======================
void handleSerialCommand() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    
    // 过滤空白字符
    if (cmd == '\n' || cmd == '\r' || cmd == ' ') {
      if (bufferIndex > 0) {
        processSerialBuffer();
        // 清空缓冲区（无需重新赋值，重置索引即可）
        memset(serialBuffer, 0, sizeof(serialBuffer));
        bufferIndex = 0;
      }
      continue;
    }

    // 字符数组缓冲区，满了就重置（防止溢出）
    if (bufferIndex < sizeof(serialBuffer) - 1) {
      serialBuffer[bufferIndex++] = cmd;
    } else {
      bufferIndex = 0;
      memset(serialBuffer, 0, sizeof(serialBuffer));
    }

    // 单字符指令直接处理（减少延迟）
    if (bufferIndex == 1) {
      char singleCmd = serialBuffer[0];
      if (!motorEnabled && singleCmd != '0' && singleCmd != '5') {
        Serial.println(F("错误：步进电机未使能！")); // F()宏
        bufferIndex = 0;
        memset(serialBuffer, 0, sizeof(serialBuffer));
        return;
      }

      bool motor1Moving = motor1.isRunning();
      bool motor2Moving = motor2.isRunning();

      switch (singleCmd) {
        case '0':
          motorEnabled = !motorEnabled;
          digitalWrite(enablePin, motorEnabled ? LOW : HIGH);
          Serial.print(F("步进电机使能："));
          Serial.println(motorEnabled ? F("是") : F("否"));
          bufferIndex = 0;
          memset(serialBuffer, 0, sizeof(serialBuffer));
          break;
        case '1':
          if (motor2Moving) {
            Serial.println(F("拒绝：电机2正在运动！"));
          } else {
            motor1.moveTo(moveSteps);
            Serial.println(F("执行：电机1正转"));
          }
          bufferIndex = 0;
          memset(serialBuffer, 0, sizeof(serialBuffer));
          break;
        case '2':
          if (motor2Moving) {
            Serial.println(F("拒绝：电机2正在运动！"));
          } else {
            motor1.moveTo(0);
            Serial.println(F("执行：电机1反转"));
          }
          bufferIndex = 0;
          memset(serialBuffer, 0, sizeof(serialBuffer));
          break;
        case '3':
          if (motor1Moving) {
            Serial.println(F("拒绝：电机1正在运动！"));
          } else {
            motor2.moveTo(moveSteps);
            Serial.println(F("执行：电机2正转"));
          }
          bufferIndex = 0;
          memset(serialBuffer, 0, sizeof(serialBuffer));
          break;
        case '4':
          if (motor1Moving) {
            Serial.println(F("拒绝：电机1正在运动！"));
          } else {
            motor2.moveTo(0);
            Serial.println(F("执行：电机2反转"));
          }
          bufferIndex = 0;
          memset(serialBuffer, 0, sizeof(serialBuffer));
          break;
        case '5':
          // 舵机指令，等待完整字符
          break;
        default:
          Serial.print(F("无效指令："));
          Serial.println(singleCmd);
          bufferIndex = 0;
          memset(serialBuffer, 0, sizeof(serialBuffer));
          break;
      }
    }
  }
}

// ====================== 舵机指令处理（优化） ======================
void processSerialBuffer() {
  if (serialBuffer[0] == '5' && bufferIndex > 1) {
    int targetAngle = atoi(serialBuffer + 1); // 替代String.toInt()，更省内存
    targetAngle = constrain(targetAngle, servoMinAngle, servoMaxAngle);
    myServo.write(targetAngle);
    currentServoAngle = targetAngle;
    Serial.print(F("舵机角度："));
    Serial.println(targetAngle);
  } else {
    Serial.println(F("无效舵机指令！"));
  }
}

// ====================== LED控制（精简） ======================
void controlLedByMotorStatus() {
  bool motor1Moving = (motor1.speed() != 0);
  bool motor2Moving = (motor2.speed() != 0);
  digitalWrite(ledPin, (motorEnabled && (motor1Moving || motor2Moving)) ? HIGH : LOW);
}

// ====================== 状态打印（F()宏优化） ======================
void printMotorAndServoStatus() {
  unsigned long currentTime = millis();
  if (currentTime - lastPrintTime >= printInterval) {
    lastPrintTime = currentTime;

    Serial.print(F("使能："));
    Serial.print(motorEnabled ? F("是") : F("否"));
    Serial.print(F(" | 电机1："));
    Serial.print(motor1.currentPosition());
    Serial.print(F(","));
    Serial.print(motor1.speed());
    Serial.print(F(","));
    Serial.print(motor1.acceleration());
    Serial.print(F(" | 电机2："));
    Serial.print(motor2.currentPosition());
    Serial.print(F(","));
    Serial.print(motor2.speed());
    Serial.print(F(","));
    Serial.print(motor2.acceleration());
    Serial.print(F(" | 舵机："));
    Serial.println(currentServoAngle);
  }
}
