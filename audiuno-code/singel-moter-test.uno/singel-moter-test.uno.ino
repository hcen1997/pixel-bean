#include "FastAccelStepper.h"
#include "AVRStepperPins.h"

#define enablePin     8
#define dirPin        5
#define stepPin       9

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;




// 串口通信相关变量
unsigned long lastReportTime = 0; // 上次发送报告的时间
const unsigned long reportInterval = 1000; // 报告间隔：1秒

// 电机运动状态变量
long targetPosition = 1000; // 默认目标位置
bool movingToTarget = true; // true: 向目标位置移动, false: 向0移动
unsigned long stopTime = 0; // 停止时间戳
const unsigned long stopDuration = 333; // 停止时长：0.333秒
bool isStopped = false; // 是否处于停止状态
bool isEnabled = false; // 电机使能状态
uint32_t linearAcceleration = 256; // 线性加速度（立方曲线加速）

void setup() {
   // 初始化串口通信
   Serial.begin(115200);

   engine.init();
   stepper = engine.stepperConnectToPin(stepPin);

   if (stepper) {
      stepper->setDirectionPin(dirPin);
      stepper->setEnablePin(enablePin);
      stepper->setAutoEnable(false); // 禁用自动使能

      stepper->setSpeedInHz(500);    // 500 步/秒
      stepper->setAcceleration(100); // 100 步/秒²
      stepper->setLinearAcceleration(linearAcceleration); // 启用立方曲线加速
      stepper->disableOutputs(); // 禁用输出
   }
}

// 处理串口命令
void processSerialCommands() {
  if (!stepper || !Serial.available()) return;
  
  // 读取完整的命令行
  String command = Serial.readStringUntil('\n');
  command.trim();
  
  if (command.length() == 0) return;
  
  // 检查是否是M0命令
  if (command == "M0") {
    // 切换使能状态
    if (isEnabled) {
      stepper->disableOutputs();
      isEnabled = false;
      Serial.println("Stepper disabled");
    } else {
      stepper->enableOutputs();
      isEnabled = true;
      Serial.println("Stepper enabled");
      // 开始向目标位置移动
      if (movingToTarget) {
        stepper->moveTo(targetPosition);
      } else {
        stepper->moveTo(0);
      }
    }
    return;
  }
  
  // 提取命令字符
  char cmd = command.charAt(0);
  
  // 提取参数
  String paramStr = command.substring(1);
  paramStr.trim();
  long param = paramStr.toInt();
  
  switch (cmd) {
    case 'T': // 设置目标位置
      if (param != 0 || paramStr == "0") {
        targetPosition = param;
        // 发送确认
        Serial.println("Target set to: " + String(targetPosition));
      }
      break;
    
    case 'S': // 设置速度
      if (param != 0 || paramStr == "0") {
        stepper->setSpeedInHz(param);
        // 发送确认
        Serial.println("Speed set to: " + String(param) + " Hz");
      }
      break;
    
    case 'A': // 设置加速度
      if (param != 0 || paramStr == "0") {
        stepper->setAcceleration(param);
        // 发送确认
        Serial.println("Acceleration set to: " + String(param) + " steps/s²");
      }
      break;
    
    case 'M': // 绝对移动
      if (param != 0 || paramStr == "0") {
        stepper->moveTo(param);
        // 发送确认
        Serial.println("Moved to position: " + String(param));
      }
      break;
    
    case 'L': // 设置线性加速度（立方曲线加速）
      if (param != 0 || paramStr == "0") {
        linearAcceleration = param;
        stepper->setLinearAcceleration(linearAcceleration);
        // 发送确认
        Serial.println("Linear acceleration set to: " + String(linearAcceleration) + " steps");
      }
      break;
  }
}

// 发送电机状态报告（使用文本模式）
void sendMotorReport() {
  if (!stepper) return;
  
  // 发送状态报告
  Serial.print("Target: ");
  Serial.print(stepper->targetPos());
  Serial.print(" steps, Speed: ");
  Serial.print(stepper->getSpeedInMilliHz() / 1000);
  Serial.print(" Hz, Accel: ");
  Serial.print(stepper->getAcceleration());
  Serial.print(" steps/s², Linear Accel: ");
  Serial.print(linearAcceleration);
  Serial.println(" steps");
}

void loop() {
  // 定时发送电机状态报告
  unsigned long currentTime = millis();
  if (currentTime - lastReportTime >= reportInterval) {
    sendMotorReport();
    lastReportTime = currentTime;
  }
  
  // 处理串口命令
  processSerialCommands();
  
  // 电机来回运动逻辑
  if (stepper && isEnabled) {
    if (isStopped) {
      // 检查停止时间是否到
      if (currentTime - stopTime >= stopDuration) {
        isStopped = false;
        // 开始反向运动
        if (movingToTarget) {
          stepper->moveTo(targetPosition);
        } else {
          stepper->moveTo(0);
        }
      }
    } else {
      // 检查是否到达目标位置
        long currentPos = stepper->getCurrentPosition();
        if ((movingToTarget && currentPos >= targetPosition) || (!movingToTarget && currentPos <= 0)) {
        // 到达目标，开始停止
        isStopped = true;
        stopTime = currentTime;
        movingToTarget = !movingToTarget;
      }
    }
  }
}