/*
AccelStepper + G代码控制（仅电机1）- 修复F值解析bug + 加速度控制
核心修复：
1. 修复F参数解析逻辑（解决i++重复自增导致的参数跳过问题）
2. 优化参数解析循环，确保X/F/A参数完整解析
3. 增加参数解析日志，方便调试
4. 保留原有所有功能，兼容G90/G91/G0/G1/M0/G22
*/
#include <AccelStepper.h>  

// ====================== 硬件引脚定义 ======================
#define enablePin 8
#define motor1DirPin 5   // X.DIR
#define motor1StepPin 2  // X.STEP
#define ledPin LED_BUILTIN

// ====================== 常量定义（Flash存储） ======================
const float maxSpeed = 4000.0;       // 电机最大速度
const float defaultAcceleration = 40.0; // 默认加速度
const float minAcceleration = 5.0;  // 最小加速度（防止过小）
const float maxAcceleration = 2000.0;// 最大加速度（防止过大）
const long serialBaud = 115200;     // 串口波特率
const unsigned long printInterval = 200; // 状态打印间隔

// ====================== 全局变量（精简到最少） ======================
unsigned long lastPrintTime = 0;
bool motorEnabled = false;
bool isAbsoluteMode = true; // G90/G91模式标记
// G代码解析缓冲区（仅16字节，省内存）
char gcodeBuffer[16] = {0};
uint8_t bufferIndex = 0;
// G代码参数存储
long targetX = 0;    // X轴目标位置
float feedRate = 300.0; // 默认进给速度
float currentAcceleration = defaultAcceleration; // 当前加速度（新增）

// ====================== 对象实例化 ======================
AccelStepper motor1(AccelStepper::DRIVER, motor1StepPin, motor1DirPin);

// ====================== Flash文本（不占RAM） ======================
#define READY_MSG F("系统就绪 版本：00点40分 - G代码指令说明：")
#define GCODE_HELP F("G90=绝对坐标 | G91=相对坐标 | G0/G1 Xxx Fxx=电机1运动 | M0=切换使能 | G22 Axx=设置加速度（5-200）")

void setup() {
  // 串口初始化
  Serial.begin(serialBaud);
  while (!Serial);
  while (Serial.available() > 0) Serial.read();

  // 引脚初始化
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);

  // 电机参数初始化
  motor1.setMaxSpeed(maxSpeed);
  motor1.setAcceleration(currentAcceleration); // 使用当前加速度初始化
  motor1.setCurrentPosition(0);

  // 提示信息
  Serial.println(READY_MSG);
  Serial.println(GCODE_HELP);
  Serial.print(F("当前模式："));
  Serial.println(isAbsoluteMode ? F("G90绝对坐标") : F("G91相对坐标"));
  Serial.print(F("电机使能："));
  Serial.println(motorEnabled ? F("是") : F("否"));
  Serial.print(F("当前加速度：")); // 新增：打印初始加速度
  Serial.println(currentAcceleration);
  Serial.print(F("默认进给速度：")); // 新增：打印默认F值
  Serial.println(feedRate);
}

void loop() {
  // 1. 解析G代码指令
  parseGCodeCommand();

  // 2. 运行电机（仅使能时）
  if (motorEnabled) {
    motor1.run();
  }

  // 3. LED同步电机状态
  controlLed();

  // 4. 定时打印状态
  printMotorStatus();
}

// ====================== 核心：G代码解析函数（修复核心） ======================
void parseGCodeCommand() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    // 关键修复：仅把\n/\r作为指令结束符，空格/制表符保留（不触发解析）
    if (c == '\n' || c == '\r') {
      if (bufferIndex > 0) {
        processGCode(); // 解析完整G代码
        memset(gcodeBuffer, 0, sizeof(gcodeBuffer));
        bufferIndex = 0;
      }
      continue;
    }
    // 过滤不可见字符（除了空格/制表符）
    if (c < 32 && c != ' ' && c != '\t') {
      continue;
    }

    // 填充缓冲区（防止溢出）
    if (bufferIndex < sizeof(gcodeBuffer) - 1) {
      gcodeBuffer[bufferIndex++] = c;
    } else {
      // 缓冲区满，重置
      bufferIndex = 0;
      memset(gcodeBuffer, 0, sizeof(gcodeBuffer));
      Serial.println(F("错误：G代码指令过长！"));
    }
  }
}

// ====================== G代码处理逻辑（优化参数解析 + 新增G22处理） ======================
void processGCode() {
  Serial.print(F("收到G代码："));
  Serial.println(gcodeBuffer);

  // 重置参数
  targetX = -9999; // 标记未设置
  float parsedFeedRate = feedRate; // 临时存储解析的F值（保留默认值）
  float newAcceleration = -1; // 新增：标记加速度参数是否设置

  // 先移除缓冲区中的所有空格/制表符（统一格式）
  char cleanBuffer[16] = {0};
  uint8_t cleanIdx = 0;
  for (uint8_t i = 0; i < bufferIndex; i++) {
    if (gcodeBuffer[i] != ' ' && gcodeBuffer[i] != '\t') {
      cleanBuffer[cleanIdx++] = gcodeBuffer[i];
    }
  }

  // 逐字符解析清理后的G代码（核心修复：改用while循环，避免i++重复自增）
  uint8_t i = 0;
  while (i < cleanIdx) {
    switch (cleanBuffer[i]) {
      // 坐标模式
      case 'G':
        i++; // 跳过G
        if (cleanBuffer[i] == '9') {
          i++; // 跳过9
          if (cleanBuffer[i] == '0') {
            isAbsoluteMode = true;
            Serial.println(F("切换为：G90 绝对坐标模式"));
          } else if (cleanBuffer[i] == '1') {
            isAbsoluteMode = false;
            Serial.println(F("切换为：G91 相对坐标模式"));
          }
          i++; // 跳过最后一位
        } else if (cleanBuffer[i] == '0') {
          // G0 快速定位（无加减速）
          i++; // 跳过0
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &parsedFeedRate, &newAcceleration);
          moveMotor(false, parsedFeedRate); // 传递解析后的F值
        } else if (cleanBuffer[i] == '1') {
          // G1 直线进给（有加减速）
          i++; // 跳过1
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &parsedFeedRate, &newAcceleration);
          moveMotor(true, parsedFeedRate); // 传递解析后的F值
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '2') {
          // G22 设置加速度
          i += 2; // 跳过22
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &parsedFeedRate, &newAcceleration);
          setAcceleration(newAcceleration); // 执行加速度设置
        }
        break;
      // 辅助功能：切换使能
      case 'M':
        i++; // 跳过M
        if (cleanBuffer[i] == '0') {
          motorEnabled = !motorEnabled;
          digitalWrite(enablePin, motorEnabled ? LOW : HIGH);
          Serial.print(F("电机使能状态："));
          Serial.println(motorEnabled ? F("已使能") : F("禁用"));
        }
        i++; // 跳过0
        break;
      default:
        i++; // 未知字符，跳过
        break;
    }
  }
}

// ====================== 修复版：解析G代码参数（X/F/A） ======================
void parseParamsFixed(char* buffer, uint8_t bufferLen, uint8_t startIndex, 
                     long* targetX, float* feedRate, float* newAcceleration) {
  char paramBuffer[8] = {0};
  uint8_t paramIdx = 0;
  
  uint8_t i = startIndex;
  while (i < bufferLen) {
    if (buffer[i] == 'X') {
      // 解析X轴目标位置
      paramIdx = 0;
      i++; // 跳过X
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *targetX = atol(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到X参数："));
      Serial.println(*targetX);
    } else if (buffer[i] == 'F') {
      // 解析进给速度F（核心修复：完整解析数字）
      paramIdx = 0;
      i++; // 跳过F
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *feedRate = atof(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到F参数："));
      Serial.println(*feedRate);
    } else if (buffer[i] == 'A') {
      // 解析加速度A
      paramIdx = 0;
      i++; // 跳过A
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *newAcceleration = atof(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到A参数："));
      Serial.println(*newAcceleration);
    } else {
      // 未知参数，跳过
      i++;
    }
  }
}

// ====================== 新增：设置加速度函数 ======================
void setAcceleration(float accelValue) {
  if (accelValue < 0) {
    Serial.println(F("错误：G22缺少A参数（例：G22 A50）"));
    return;
  }
  
  // 参数校验：限制在5-200之间
  if (accelValue < minAcceleration) {
    currentAcceleration = minAcceleration;
    Serial.print(F("加速度过小，已限制为最小值："));
    Serial.println(minAcceleration);
  } else if (accelValue > maxAcceleration) {
    currentAcceleration = maxAcceleration;
    Serial.print(F("加速度过大，已限制为最大值："));
    Serial.println(maxAcceleration);
  } else {
    currentAcceleration = accelValue;
    Serial.print(F("加速度已设置为："));
    Serial.println(currentAcceleration);
  }
  
  // 更新电机加速度参数
  motor1.setAcceleration(currentAcceleration);
}

// ====================== 修复版：控制电机运动（传递解析后的F值） ======================
void moveMotor(bool useAcceleration, float parsedFeedRate) {
  if (!motorEnabled) {
    Serial.println(F("错误：电机未使能！发送M0使能"));
    return;
  }

  if (*&targetX == -9999) {
    Serial.println(F("错误：G代码缺少X参数（例：G1 X200 F300）"));
    return;
  }

  // 设置电机最大速度（F值），不超过全局上限
  float finalSpeed = (parsedFeedRate > maxSpeed) ? maxSpeed : parsedFeedRate;
  motor1.setMaxSpeed(finalSpeed);
  
  // 绝对/相对坐标处理
  long currentPos = motor1.currentPosition();
  long finalTarget = isAbsoluteMode ? *&targetX : (currentPos + *&targetX);
  
  // 加减速控制（使用当前加速度值）
  if (useAcceleration) {
    motor1.setAcceleration(currentAcceleration); // 使用动态设置的加速度
    motor1.moveTo(finalTarget);
    Serial.print(F("执行G1：电机1移动到"));
  } else {
    motor1.setAcceleration(0); // 关闭加减速（快速定位）
    motor1.moveTo(finalTarget);
    Serial.print(F("执行G0：电机1快速定位到"));
  }
  
  Serial.print(finalTarget);
  Serial.print(F(" 速度："));
  Serial.print(finalSpeed);
  Serial.print(F(" 加速度："));
  Serial.println(currentAcceleration);
}

// ====================== LED控制（仅电机1） ======================
void controlLed() {
  bool motor1Moving = (motor1.speed() != 0);
  digitalWrite(ledPin, (motorEnabled && motor1Moving) ? HIGH : LOW);
}

// ====================== 状态打印（仅电机1）- 新增加速度显示 ======================
void printMotorStatus() {
  unsigned long currentTime = millis();
  if (currentTime - lastPrintTime >= printInterval) {
    lastPrintTime = currentTime;

    Serial.print(F("模式："));
    Serial.print(isAbsoluteMode ? F("G90") : F("G91"));
    Serial.print(F(" | 使能："));
    Serial.print(motorEnabled ? F("是") : F("否"));
    Serial.print(F(" | 电机1：位置="));
    Serial.print(motor1.currentPosition());
    Serial.print(F(", 速度="));
    Serial.print(motor1.speed());
    Serial.print(F(", 加速度="));
    Serial.print(currentAcceleration);
    Serial.print(F(", 当前F值="));
    Serial.println(feedRate); // 新增：打印当前F值
  }
}
