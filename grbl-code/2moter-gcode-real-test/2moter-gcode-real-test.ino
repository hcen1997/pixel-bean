/*
AccelStepper + CoreXY + G代码控制（双电机）+ 在线校准
核心功能：
1. 基础功能：G90/G91、G0/G1 Xxx Yyy Fxx、M0、G22 Axx
2. CoreXY解算：自动转换X/Y→双电机运动
3. 新增校准指令：
   - G23 Dxx：方向校准（反转X/Y/A/B轴）
   - G24 Sxx：步数倍率设置（全局缩放）
   - G25 Cxx：同步加速度微调（优化双电机同步性）
4. 保留F值/加速度解析、状态打印等所有原有功能
*/
#include <AccelStepper.h>  

// ====================== 硬件引脚定义 ======================
#define enablePin 8       // 总使能引脚
// CoreXY双电机引脚（A轴=电机1，B轴=电机2）
#define motor1DirPin 5    // A.DIR (X-Y)
#define motor1StepPin 2   // A.STEP
#define motor2DirPin 6    // B.DIR (X+Y)
#define motor2StepPin 3   // B.STEP
#define ledPin LED_BUILTIN

// ====================== 常量定义（Flash存储） ======================
const float maxSpeed = 4000.0;        // 电机最大速度（双电机同步）
const float defaultAcceleration = 40.0; // 默认加速度
const float minAcceleration = 5.0;   // 最小加速度
const float maxAcceleration = 2000.0;// 最大加速度
const float minScale = 0.1;          // 最小步数倍率
const float maxScale = 5.0;          // 最大步数倍率
const long serialBaud = 115200;      // 串口波特率
const unsigned long printInterval = 200; // 状态打印间隔

// ====================== 全局变量（新增校准参数） ======================
unsigned long lastPrintTime = 0;
bool motorEnabled = false;
bool isAbsoluteMode = true; // G90/G91模式标记
// 方向校准位掩码（0=正方向，1=反方向）
// bit0=X轴, bit1=Y轴, bit2=A轴, bit3=B轴
uint8_t dirMask = 0;       
float stepScale = 1.0;      // 步数倍率（默认1倍）
float syncAcceleration = defaultAcceleration; // 同步加速度

// G代码解析缓冲区
char gcodeBuffer[32] = {0}; // 扩容到32字节，支持X/Y双参数
uint8_t bufferIndex = 0;
// G代码参数存储
long targetX = -9999;       // X轴目标位置
long targetY = -9999;       // Y轴目标位置
float feedRate = 300.0;     // 默认进给速度
float currentAcceleration = defaultAcceleration; // 当前加速度

// ====================== CoreXY双电机实例化 ======================
AccelStepper motorA(AccelStepper::DRIVER, motor1StepPin, motor1DirPin); // A轴(X-Y)
AccelStepper motorB(AccelStepper::DRIVER, motor2StepPin, motor2DirPin); // B轴(X+Y)

// ====================== Flash文本 ======================
#define READY_MSG F("CoreXY系统就绪 版本：02.00 - G代码指令说明：")
#define GCODE_HELP F("基础指令：G90/G91 | G0/G1 Xxx Yyy Fxx | M0 | G22 Axx\n校准指令：G23 Dxx(方向) | G24 Sxx(倍率) | G25 Cxx(同步加速度)")
#define DIR_HELP F("G23 D值说明：D0=默认 | D1=反转X | D2=反转Y | D4=反转A | D8=反转B | D3=反转X+Y")

void setup() {
  // 串口初始化
  Serial.begin(serialBaud);
  while (!Serial);
  while (Serial.available() > 0) Serial.read();

  // 引脚初始化
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH); // 默认禁用电机

  // 双电机参数初始化
  motorA.setMaxSpeed(maxSpeed);
  motorA.setAcceleration(currentAcceleration);
  motorA.setCurrentPosition(0);
  
  motorB.setMaxSpeed(maxSpeed);
  motorB.setAcceleration(currentAcceleration);
  motorB.setCurrentPosition(0);

  // 提示信息
  Serial.println(READY_MSG);
  Serial.println(GCODE_HELP);
  Serial.println(DIR_HELP);
  Serial.print(F("当前模式："));
  Serial.println(isAbsoluteMode ? F("G90绝对坐标") : F("G91相对坐标"));
  Serial.print(F("电机使能："));
  Serial.println(motorEnabled ? F("是") : F("否"));
  Serial.print(F("当前加速度："));
  Serial.println(currentAcceleration);
  Serial.print(F("步数倍率："));
  Serial.println(stepScale);
  Serial.print(F("方向掩码："));
  Serial.println(dirMask);
  Serial.print(F("默认进给速度："));
  Serial.println(feedRate);
}

void loop() {
  // 1. 解析G代码指令
  parseGCodeCommand();

  // 2. 运行双电机（仅使能时）
  if (motorEnabled) {
    motorA.run();
    motorB.run();
  }

  // 3. LED同步电机状态（任意电机运动则亮）
  controlLed();

  // 4. 定时打印状态
  printMotorStatus();
}

// ====================== 核心：G代码解析函数 ======================
void parseGCodeCommand() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    // 仅把\n/\r作为指令结束符
    if (c == '\n' || c == '\r') {
      if (bufferIndex > 0) {
        processGCode(); 
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
      bufferIndex = 0;
      memset(gcodeBuffer, 0, sizeof(gcodeBuffer));
      Serial.println(F("错误：G代码指令过长！"));
    }
  }
}

// ====================== G代码处理逻辑（新增校准指令） ======================
void processGCode() {
  Serial.print(F("收到G代码："));
  Serial.println(gcodeBuffer);

  // 重置参数
  targetX = -9999; 
  targetY = -9999;
  float parsedFeedRate = feedRate; 
  float newAcceleration = -1; 
  // 新增校准参数
  int newDirMask = -1;
  float newScale = -1.0;
  float newSyncAcc = -1.0;

  // 移除空格/制表符
  char cleanBuffer[32] = {0};
  uint8_t cleanIdx = 0;
  for (uint8_t i = 0; i < bufferIndex; i++) {
    if (gcodeBuffer[i] != ' ' && gcodeBuffer[i] != '\t') {
      cleanBuffer[cleanIdx++] = gcodeBuffer[i];
    }
  }

  // 逐字符解析
  uint8_t i = 0;
  while (i < cleanIdx) {
    switch (cleanBuffer[i]) {
      case 'G':
        i++; 
        if (cleanBuffer[i] == '9') {
          // G90/G91 坐标模式
          i++; 
          if (cleanBuffer[i] == '0') {
            isAbsoluteMode = true;
            Serial.println(F("切换为：G90 绝对坐标模式"));
          } else if (cleanBuffer[i] == '1') {
            isAbsoluteMode = false;
            Serial.println(F("切换为：G91 相对坐标模式"));
          }
          i++; 
        } else if (cleanBuffer[i] == '0') {
          // G0 快速定位
          i++; 
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &targetY, &parsedFeedRate, &newAcceleration, &newDirMask, &newScale, &newSyncAcc);
          moveCoreXY(false, parsedFeedRate); 
        } else if (cleanBuffer[i] == '1') {
          // G1 直线进给
          i++; 
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &targetY, &parsedFeedRate, &newAcceleration, &newDirMask, &newScale, &newSyncAcc);
          moveCoreXY(true, parsedFeedRate); 
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '2') {
          // G22 设置加速度
          i += 2; 
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &targetY, &parsedFeedRate, &newAcceleration, &newDirMask, &newScale, &newSyncAcc);
          setAcceleration(newAcceleration); 
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '3') {
          // 新增：G23 方向校准
          i += 2;
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &targetY, &parsedFeedRate, &newAcceleration, &newDirMask, &newScale, &newSyncAcc);
          setDirMask(newDirMask);
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '4') {
          // 新增：G24 步数倍率
          i += 2;
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &targetY, &parsedFeedRate, &newAcceleration, &newDirMask, &newScale, &newSyncAcc);
          setStepScale(newScale);
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '5') {
          // 新增：G25 同步加速度
          i += 2;
          parseParamsFixed(cleanBuffer, cleanIdx, i, &targetX, &targetY, &parsedFeedRate, &newAcceleration, &newDirMask, &newScale, &newSyncAcc);
          setSyncAcceleration(newSyncAcc);
        }
        break;
      case 'M':
        // M0 使能切换
        i++; 
        if (cleanBuffer[i] == '0') {
          motorEnabled = !motorEnabled;
          digitalWrite(enablePin, motorEnabled ? LOW : HIGH);
          Serial.print(F("电机使能状态："));
          Serial.println(motorEnabled ? F("已使能") : F("禁用"));
        }
        i++; 
        break;
      default:
        i++; 
        break;
    }
  }
}

// ====================== 修复版：解析参数（新增校准参数） ======================
void parseParamsFixed(char* buffer, uint8_t bufferLen, uint8_t startIndex, 
                     long* targetX, long* targetY, float* feedRate, float* newAcceleration,
                     int* newDirMask, float* newScale, float* newSyncAcc) {
  char paramBuffer[8] = {0};
  uint8_t paramIdx = 0;
  
  uint8_t i = startIndex;
  while (i < bufferLen) {
    if (buffer[i] == 'X') {
      // 解析X轴目标位置
      paramIdx = 0;
      i++; 
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *targetX = atol(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到X参数："));
      Serial.println(*targetX);
    } else if (buffer[i] == 'Y') {
      // 解析Y轴目标位置
      paramIdx = 0;
      i++; 
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *targetY = atol(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到Y参数："));
      Serial.println(*targetY);
    } else if (buffer[i] == 'F') {
      // 解析进给速度F
      paramIdx = 0;
      i++; 
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *feedRate = atof(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到F参数："));
      Serial.println(*feedRate);
    } else if (buffer[i] == 'A') {
      // 解析加速度A（G22）
      paramIdx = 0;
      i++; 
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *newAcceleration = atof(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到A参数："));
      Serial.println(*newAcceleration);
    } else if (buffer[i] == 'D') {
      // 新增：解析方向掩码D（G23）
      paramIdx = 0;
      i++; 
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *newDirMask = atoi(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到D参数（方向掩码）："));
      Serial.println(*newDirMask);
    } else if (buffer[i] == 'S') {
      // 新增：解析倍率S（G24）
      paramIdx = 0;
      i++; 
      while (i < bufferLen && (isDigit(buffer[i]) || buffer[i] == '.')) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *newScale = atof(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到S参数（倍率）："));
      Serial.println(*newScale);
    } else if (buffer[i] == 'C') {
      // 新增：解析同步加速度C（G25）
      paramIdx = 0;
      i++; 
      while (i < bufferLen && isDigit(buffer[i])) {
        paramBuffer[paramIdx++] = buffer[i];
        i++;
      }
      *newSyncAcc = atof(paramBuffer);
      memset(paramBuffer, 0, sizeof(paramBuffer));
      Serial.print(F("解析到C参数（同步加速度）："));
      Serial.println(*newSyncAcc);
    } else {
      i++;
    }
  }
}

// ====================== 新增：方向掩码设置（G23） ======================
void setDirMask(int newDir) {
  if (newDir < 0 || newDir > 15) { // 4位掩码，0-15
    Serial.println(F("错误：G23 D值范围0-15（D0=默认 | D1=反转X | D2=反转Y | D4=反转A | D8=反转B）"));
    return;
  }
  dirMask = newDir;
  Serial.print(F("方向掩码已设置为："));
  Serial.println(dirMask);
  // 打印当前方向配置
  Serial.print(F("当前方向：X="));
  Serial.print((dirMask & 0x01) ? F("反转") : F("正方向"));
  Serial.print(F(" | Y="));
  Serial.print((dirMask & 0x02) ? F("反转") : F("正方向"));
  Serial.print(F(" | A="));
  Serial.print((dirMask & 0x04) ? F("反转") : F("正方向"));
  Serial.print(F(" | B="));
  Serial.println((dirMask & 0x08) ? F("反转") : F("正方向"));
}

// ====================== 新增：步数倍率设置（G24） ======================
void setStepScale(float newScaleVal) {
  if (newScaleVal < 0) {
    Serial.println(F("错误：G24 S值需≥0（例：G24 S1.2）"));
    return;
  }
  // 限制倍率范围
  if (newScaleVal < minScale) {
    stepScale = minScale;
    Serial.print(F("倍率过小，已限制为最小值："));
    Serial.println(minScale);
  } else if (newScaleVal > maxScale) {
    stepScale = maxScale;
    Serial.print(F("倍率过大，已限制为最大值："));
    Serial.println(maxScale);
  } else {
    stepScale = newScaleVal;
    Serial.print(F("步数倍率已设置为："));
    Serial.println(stepScale);
  }
}

// ====================== 新增：同步加速度设置（G25） ======================
void setSyncAcceleration(float newSyncVal) {
  if (newSyncVal < 0) {
    Serial.println(F("错误：G25 C值需≥0（例：G25 C180）"));
    return;
  }
  // 限制加速度范围
  if (newSyncVal < minAcceleration) {
    syncAcceleration = minAcceleration;
    Serial.print(F("同步加速度过小，已限制为最小值："));
    Serial.println(minAcceleration);
  } else if (newSyncVal > maxAcceleration) {
    syncAcceleration = maxAcceleration;
    Serial.print(F("同步加速度过大，已限制为最大值："));
    Serial.println(maxAcceleration);
  } else {
    syncAcceleration = newSyncVal;
    Serial.print(F("同步加速度已设置为："));
    Serial.println(syncAcceleration);
  }
  // 双电机同步更新
  motorA.setAcceleration(syncAcceleration);
  motorB.setAcceleration(syncAcceleration);
}

// ====================== 原有：加速度设置（G22） ======================
void setAcceleration(float accelValue) {
  if (accelValue < 0) {
    Serial.println(F("错误：G22缺少A参数（例：G22 A50）"));
    return;
  }
  
  // 参数校验
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
  
  // 双电机同步更新加速度
  motorA.setAcceleration(currentAcceleration);
  motorB.setAcceleration(currentAcceleration);
}

// ====================== CoreXY运动解算（新增校准逻辑） ======================
void moveCoreXY(bool useAcceleration, float parsedFeedRate) {
  if (!motorEnabled) {
    Serial.println(F("错误：电机未使能！发送M0使能"));
    return;
  }

  // 检查X/Y参数（至少设置一个）
  if (targetX == -9999 && targetY == -9999) {
    Serial.println(F("错误：G代码缺少X/Y参数（例：G1 X100 Y50 F300）"));
    return;
  }

  // 补全未设置的参数（用当前位置）
  long currentX = 0, currentY = 0;
  // 从CoreXY逆解算当前X/Y位置（带方向校准）
  long rawA = motorA.currentPosition();
  long rawB = motorB.currentPosition();
  // 方向反转处理
  rawA = (dirMask & 0x04) ? -rawA : rawA; // A轴方向
  rawB = (dirMask & 0x08) ? -rawB : rawB; // B轴方向
  currentX = (rawA + rawB) / 2;
  currentY = (rawB - rawA) / 2;
  
  // 目标位置处理（带方向校准）
  long finalX = (targetX == -9999) ? currentX : (isAbsoluteMode ? targetX : (currentX + targetX));
  long finalY = (targetY == -9999) ? currentY : (isAbsoluteMode ? targetY : (currentY + targetY));
  // X/Y方向反转
  finalX = (dirMask & 0x01) ? -finalX : finalX;
  finalY = (dirMask & 0x02) ? -finalY : finalY;

  // 设置速度（双电机同步）
  float finalSpeed = (parsedFeedRate > maxSpeed) ? maxSpeed : parsedFeedRate;
  motorA.setMaxSpeed(finalSpeed);
  motorB.setMaxSpeed(finalSpeed);
  
  // CoreXY正解算：转换为两个电机的目标位置（带步数倍率）
  long targetA = (finalX - finalY) * stepScale; 
  long targetB = (finalX + finalY) * stepScale; 
  // 电机方向反转（最终生效）
  targetA = (dirMask & 0x04) ? -targetA : targetA;
  targetB = (dirMask & 0x08) ? -targetB : targetB;
  
  // 加减速控制（优先使用同步加速度）
  float accelToUse = (syncAcceleration > 0) ? syncAcceleration : currentAcceleration;
  if (useAcceleration) {
    motorA.setAcceleration(accelToUse);
    motorB.setAcceleration(accelToUse);
  } else {
    motorA.setAcceleration(0);
    motorB.setAcceleration(0);
  }
  
  // 发送运动指令
  motorA.moveTo(targetA);
  motorB.moveTo(targetB);
  
  // 打印运动信息（带校准参数）
  Serial.print(F("执行"));
  Serial.print(useAcceleration ? F("G1") : F("G0"));
  Serial.print(F("：XY目标("));
  Serial.print(finalX);
  Serial.print(F(","));
  Serial.print(finalY);
  Serial.print(F(") | 电机A/B目标("));
  Serial.print(targetA);
  Serial.print(F(","));
  Serial.print(targetB);
  Serial.print(F(") | 速度："));
  Serial.print(finalSpeed);
  Serial.print(F(" | 加速度："));
  Serial.print(accelToUse);
  Serial.print(F(" | 倍率："));
  Serial.println(stepScale);
}

// ====================== LED控制（双电机） ======================
void controlLed() {
  bool motorMoving = (motorA.speed() != 0 || motorB.speed() != 0);
  digitalWrite(ledPin, (motorEnabled && motorMoving) ? HIGH : LOW);
}

// ====================== 状态打印（新增校准参数） ======================
void printMotorStatus() {
  unsigned long currentTime = millis();
  if (currentTime - lastPrintTime >= printInterval) {
    lastPrintTime = currentTime;

    // 逆解算当前X/Y位置（带方向校准）
    long rawA = motorA.currentPosition();
    long rawB = motorB.currentPosition();
    rawA = (dirMask & 0x04) ? -rawA : rawA;
    rawB = (dirMask & 0x08) ? -rawB : rawB;
    long currentX = (rawA + rawB) / 2;
    long currentY = (rawB - rawA) / 2;

    Serial.print(F("模式："));
    Serial.print(isAbsoluteMode ? F("G90") : F("G91"));
    Serial.print(F(" | 使能："));
    Serial.print(motorEnabled ? F("是") : F("否"));
    Serial.print(F(" | XY位置("));
    Serial.print(currentX);
    Serial.print(F(","));
    Serial.print(currentY);
    Serial.print(F(") | 倍率："));
    Serial.print(stepScale);
    Serial.print(F(" | 电机A：位置="));
    Serial.print(motorA.currentPosition());
    Serial.print(F(", 速度="));
    Serial.print(motorA.speed());
    Serial.print(F(" | 电机B：位置="));
    Serial.print(motorB.currentPosition());
    Serial.print(F(", 速度="));
    Serial.print(motorB.speed());
    Serial.print(F(" | 同步加速度="));
    Serial.println(syncAcceleration);
  }
}
