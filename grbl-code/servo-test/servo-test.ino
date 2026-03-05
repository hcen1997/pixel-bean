#include <Servo.h>

// 核心对象
Servo myServo;  

// 引脚定义
const int servoPin = 12;  // 舵机信号引脚
const int enPin = 8;      // CNC扩展板使能引脚

// 角度控制变量
int targetAngle = 70;     // 目标角度（初始值改为70°，在60-125范围内）
int currentAngle = 70;    // 当前角度（初始值改为70°）

// 角度范围常量（方便后续修改）
const int MIN_ANGLE = 60; // 最小角度限制
const int MAX_ANGLE = 125; // 最大角度限制

// 串口缓冲区
char serialBuffer[10] = {0}; 
uint8_t bufIndex = 0;       

void setup() {
  // 初始化8号引脚为输出并拉高，防止故障
  pinMode(enPin, OUTPUT);
  digitalWrite(enPin, HIGH);
  
  // 舵机初始化：先写角度再attach，避免初始化异常（核心修改点1）
  myServo.write(currentAngle); // 先写入初始角度70°
  myServo.attach(servoPin);    // 后执行attach
  
  // 串口初始化
  Serial.begin(115200);        
  Serial.println("=== 舵机立即到位控制就绪 ===");
  Serial.print("指令：直接输入角度值（");
  Serial.print(MIN_ANGLE);
  Serial.print("-");
  Serial.print(MAX_ANGLE);
  Serial.println("），例如输入90则立即运动到90°");
  Serial.print("初始角度：");
  Serial.print(currentAngle);
  Serial.println("°");
}

void loop() {
  // 1. 处理串口指令（仅接收角度值）
  handleSerialCommand();

  // 2. 定期打印状态
  static unsigned long lastPrintTime = 0;
  if (millis() - lastPrintTime > 500) {
    lastPrintTime = millis();
    Serial.print("当前角度：");
    Serial.print(currentAngle);
    Serial.print("° | 目标角度：");
    Serial.print(targetAngle);
    Serial.println("°");
  }
}

// 串口指令处理（仅解析角度值，解析后立即设置舵机角度）
void handleSerialCommand() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    
    // 指令结束符（换行/回车/空格）
    if (cmd == '\n' || cmd == '\r' || cmd == ' ') {
      if (bufIndex > 0) {
        // 解析输入的角度值
        int inputAngle = atoi(serialBuffer);
        // 限制角度范围为60-125°
        inputAngle = constrain(inputAngle, MIN_ANGLE, MAX_ANGLE);
        
        if (inputAngle != currentAngle) {
          targetAngle = inputAngle;
          currentAngle = targetAngle; // 同步当前角度
          myServo.write(targetAngle); // 立即写入目标角度，无延迟
          Serial.print("已立即运动到：");
          Serial.println(targetAngle);
        } else {
          Serial.println("目标角度=当前角度，无需运动");
        }
        
        // 清空缓冲区
        memset(serialBuffer, 0, sizeof(serialBuffer));
        bufIndex = 0;
      }
      continue;
    }

    // 仅接收数字字符
    if (isdigit(cmd) && bufIndex < sizeof(serialBuffer)-1) {
      serialBuffer[bufIndex++] = cmd;
    }
  }
}
