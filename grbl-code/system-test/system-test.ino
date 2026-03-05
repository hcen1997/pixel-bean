#include <FastAccelStepper.h>
#include "AVRStepperPins.h"
#include <Timer2ServoPwm.h>
#include <math.h>   // 用于 roundf

// ====================== 全局常量定义 ======================
// 串口配置
const long serialBaud = 115200;           // 串口波特率
const unsigned long printInterval = 1000;  // 状态打印间隔（优化：改为1秒一次）

// CoreXY电机配置
#define enablePin 8       // 总使能引脚（与舵机EN复用）
#define motor1DirPin 5    // A.DIR (X-Y)
#define motor1StepPin 9   // A.STEP (FastAccelStepper要求特定引脚)
#define motor2DirPin 6    // B.DIR (X+Y)
#define motor2StepPin 10  // B.STEP (FastAccelStepper要求特定引脚)
#define ledPin LED_BUILTIN
const uint32_t maxSpeed = 40000;            // 电机最大速度 (Hz)
const uint32_t defaultAcceleration = 6400;   // 默认加速度 (steps/s²)  // gcode修改不了，不知道为什么 就这样吧 够用了
const uint32_t minAcceleration = 0;        // 最小加速度（允许0）
const uint32_t maxAcceleration = 20000;     // 最大加速度 (steps/s²)
const float minScale = 0.001;               // 最小步数倍率
const float maxScale = 200.0;               // 最大步数倍率

// 舵机配置
#define servoPin 12       // 舵机信号引脚
const int MIN_SERVO_ANGLE = 60;           // 舵机最小角度
const int MAX_SERVO_ANGLE = 125;          // 舵机最大角度
const int SERVO_INIT_ANGLE = 70;          // 舵机默认初始角度
const int SERVO_DEFAULT_PRESS_ANGLE = 115; // 舵机默认按下角度
const int SERVO_DEFAULT_PRESS_DELAY = 100; // 舵机默认按下延迟(ms)
const int MIN_PRESS_DELAY = 10;           // 最小延迟（防止过短）
const int MAX_PRESS_DELAY = 2000;         // 最大延迟（防止过长）

// 全局变量定义 ======================
// FastAccelStepper引擎
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *motorA = NULL; // A轴(X-Y)
FastAccelStepper *motorB = NULL; // B轴(X+Y)

// CoreXY变量
bool motorEnabled = false;
bool isAbsoluteMode = true;                // G90/G91模式
uint8_t dirMask = 1;                       // 方向掩码(bit0=X,bit1=Y,bit2=A,bit3=B) 默认X反向（根据用户实测）
float stepScale = 1.0;                     // 步数倍率（每毫米步数，需用户校准）
uint32_t syncAcceleration = defaultAcceleration; // 同步加速度
uint32_t currentAcceleration = defaultAcceleration; // 当前加速度
uint32_t feedRate = 300;                    // 默认进给速度
unsigned long lastPrintTime = 0;

// GCode解析
char gcodeBuffer[64] = {0};                // 增大缓冲区，支持较长指令
uint8_t bufferIndex = 0;
float targetX_mm = -9999;                   // X轴目标位置（毫米）
float targetY_mm = -9999;                   // Y轴目标位置（毫米）

// 舵机变量
Timer2Servo myServo;
int servoCurrentAngle = SERVO_INIT_ANGLE;   // 舵机当前角度

// 非阻塞舵机状态机
enum ServoState {
  SERVO_IDLE,
  SERVO_MOVING,
  SERVO_PRESS_DOWN,  // 按下状态
  SERVO_PRESS_UP     // 释放状态
};
ServoState servoState = SERVO_IDLE;
int servoTargetAngle = SERVO_INIT_ANGLE;
unsigned long servoMoveStartTime = 0;
unsigned long servoMoveDuration = 0;        // 预估移动时间（ms），可设为固定值如500ms

// 舵机按下动作参数
int pressStartAngle = SERVO_INIT_ANGLE;
int pressEndAngle = SERVO_DEFAULT_PRESS_ANGLE;
int pressDelayTime = SERVO_DEFAULT_PRESS_DELAY;
unsigned long pressDownEndTime = 0;



// ====================== 初始化函数 ======================
void setup() {

    // 舵机初始化（先设置引脚为输出，再写角度，最后attach）
  pinMode(servoPin, OUTPUT);
  digitalWrite(servoPin, LOW);
  myServo.attach(servoPin);
  delay(3); // 短暂延迟
  myServo.write(servoCurrentAngle);


  // 串口初始化
  Serial.begin(serialBaud);
  while (!Serial);
  while (Serial.available() > 0) Serial.read();

  // 引脚初始化
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH); // 默认禁用电机/拉高EN

  // FastAccelStepper引擎初始化
  engine.init();

  // CoreXY电机初始化
  motorA = engine.stepperConnectToPin(motor1StepPin);
  if (motorA) {
    motorA->setDirectionPin(motor1DirPin);
    motorA->setEnablePin(enablePin);
    motorA->setAutoEnable(false);
    motorA->setSpeedInHz(maxSpeed);
    motorA->setAcceleration(currentAcceleration);
    motorA->setCurrentPosition(0);
    motorA->disableOutputs();
  }

  motorB = engine.stepperConnectToPin(motor2StepPin);
  if (motorB) {
    motorB->setDirectionPin(motor2DirPin);
    motorB->setEnablePin(enablePin);
    motorB->setAutoEnable(false);
    motorB->setSpeedInHz(maxSpeed);
    motorB->setAcceleration(currentAcceleration);
    motorB->setCurrentPosition(0);
    motorB->disableOutputs();
  }



  // 根据用户实测，默认X轴反向（dirMask=1）
  dirMask = 1;
  Serial.println(F("方向掩码默认设为1（X轴反向）"));

  // 系统提示
  Serial.println(F("=== CoreXY+舵机集成系统就绪（FastAccelStepper增强版） ==="));
  Serial.println(F("CoreXY指令：G90/G91 | G0/G1 Xxx Yxx Fxx | M0 | G22 Axx | G23 Dxx | G24 Sxx | G25 Cxx"));
  Serial.println(F("舵机指令：G30 Sxx(设置角度，非阻塞) | G31 [Pxx Qxx Rxx](按下动作，阻塞) | G32(复位到初始角度，非阻塞)"));
  Serial.print(F("初始状态：CoreXY禁用 | 舵机角度="));
  Serial.println(servoCurrentAngle);
}

// ====================== 主循环 ======================
void loop() {
  parseGCodeCommand();    // 解析GCode指令
  runCoreXY();            // 运行CoreXY电机
  updateServo();          // 非阻塞舵机状态更新
  controlLed();           // LED状态控制
  printSystemStatus();    // 打印系统状态
}

// ====================== CoreXY核心功能 ======================
void runCoreXY() {
  // FastAccelStepper会在后台自动运行，不需要在loop中调用run()
  // 这里可以添加其他需要在loop中处理的CoreXY相关逻辑
}

void controlLed() {
  bool motorMoving = false;
  if (motorA && motorB) {
    motorMoving = (motorA->isRunning() || motorB->isRunning());
  }
  digitalWrite(ledPin, (motorEnabled && motorMoving) ? HIGH : LOW);
}

void setAcceleration(float accelValue) {
  if (accelValue < 0) {
    Serial.println(F("错误：G22缺少A参数（例：G22 A50）"));
    return;
  }

  // 允许accelValue = 0，但FastAccelStepper加速度为0会导致无加减速运动
  currentAcceleration = constrain(accelValue, minAcceleration, maxAcceleration);
  if (motorA) motorA->setAcceleration(currentAcceleration);
  if (motorB) motorB->setAcceleration(currentAcceleration);
  Serial.print(F("加速度已设置为："));
  Serial.println(currentAcceleration);
}

void setDirMask(int newDir) {
  if (newDir < 0 || newDir > 15) {
    Serial.println(F("错误：G23 D值范围0-15（D0=默认 | D1=反转X | D2=反转Y | D4=反转A | D8=反转B）"));
    return;
  }
  dirMask = newDir;
  Serial.print(F("方向掩码已设置为："));
  Serial.println(dirMask);
}

void setStepScale(float newScaleVal) {
  if (newScaleVal < 0) {
    Serial.println(F("错误：G24 S值需≥0（例：G24 S1.2）"));
    return;
  }
  stepScale = constrain(newScaleVal, minScale, maxScale);
  Serial.print(F("步数倍率（每毫米步数）已设置为："));
  Serial.println(stepScale);
}

void setSyncAcceleration(float newSyncVal) {
  if (newSyncVal < 0) {
    Serial.println(F("错误：G25 C值需≥0（例：G25 C180）"));
    return;
  }
  // 允许newSyncVal=0，此时不使用同步加速度（将用currentAcceleration）
  syncAcceleration = (newSyncVal == 0) ? 0 : constrain(newSyncVal, minAcceleration, maxAcceleration);
  Serial.print(F("同步加速度已设置为："));
  Serial.println(syncAcceleration);
}

// 将毫米值转换为步数（四舍五入）
long mmToSteps(float mm) {
  return lroundf(mm * stepScale);
}

// 将步数转换为毫米
float stepsToMm(long steps) {
  return (float)steps / stepScale;
}

void moveCoreXY(bool useAcceleration, float parsedFeedRate) {
  if (!motorEnabled) {
    Serial.println(F("错误：电机未使能！发送M0使能"));
    return;
  }

  if (targetX_mm == -9999 && targetY_mm == -9999) {
    Serial.println(F("错误：G代码缺少X/Y参数（例：G1 X100 Y50 F300）"));
    return;
  }

  if (!motorA || !motorB) {
    Serial.println(F("错误：电机初始化失败！"));
    return;
  }

  // 逆解算当前XY位置（步数）- 完全重写：正确处理X/Y反转
  long rawA = motorA->getCurrentPosition();
  long rawB = motorB->getCurrentPosition();
  
  // 1. 先应用A/B反转掩码
  if (dirMask & 0x04) rawA = -rawA; // A电机反转
  if (dirMask & 0x08) rawB = -rawB; // B电机反转
  
  // 2. 应用X/Y反转（CoreXY特殊逻辑，与正解算相反）
  if (dirMask & 0x02) {
    // Y反转：A和B交换
    long temp = rawA;
    rawA = rawB;
    rawB = temp;
  }
  if (dirMask & 0x01) {
    // X反转：A和B交换并取反
    long temp = rawA;
    rawA = -rawB;
    rawB = -temp;
  }
  
  // 3. 进行CoreXY逆解算
  long curr_x_steps = (rawA + rawB) / 2;
  long curr_y_steps = (rawB - rawA) / 2;
  
  // 转换为毫米
  float currentX_mm = stepsToMm(curr_x_steps);
  float currentY_mm = stepsToMm(curr_y_steps);

  // 计算目标位置（毫米）
  float finalX_mm = (targetX_mm == -9999) ? currentX_mm : (isAbsoluteMode ? targetX_mm : (currentX_mm + targetX_mm));
  float finalY_mm = (targetY_mm == -9999) ? currentY_mm : (isAbsoluteMode ? targetY_mm : (currentY_mm + targetY_mm));
  
  // 转换为步数
  long finalX_steps = mmToSteps(finalX_mm);
  long finalY_steps = mmToSteps(finalY_mm);

  // 加速度有点问题 这里不设置 先写死
  // G代码F值单位是毫米/分钟，需要转换为Hz（每秒步数）
  // 转换公式：Hz = (F值 / 60) * stepScale
  uint32_t finalSpeed = constrain(parsedFeedRate , 0, maxSpeed);
  motorA->setSpeedInHz(finalSpeed);
  motorB->setSpeedInHz(finalSpeed);

  // CoreXY正解算（步数） - 完全重写：正确处理X/Y反转
  long final_x_steps = finalX_steps;
  long final_y_steps = finalY_steps;
  
  // 1. 先进行标准CoreXY正解算
  long targetA = final_x_steps - final_y_steps;
  long targetB = final_x_steps + final_y_steps;
  
  // 2. 应用X/Y反转（CoreXY特殊逻辑）
  if (dirMask & 0x01) {
    // X反转：A和B交换并取反
    long temp = targetA;
    targetA = -targetB;
    targetB = -temp;
  }
  if (dirMask & 0x02) {
    // Y反转：A和B交换
    long temp = targetA;
    targetA = targetB;
    targetB = temp;
  }
  
  // 3. 最后应用A/B反转掩码
  if (dirMask & 0x04) targetA = -targetA; // A电机反转
  if (dirMask & 0x08) targetB = -targetB; // B电机反转

  // 执行运动
  motorA->moveTo(targetA);
  motorB->moveTo(targetB);

  // 打印运动信息（使用毫米）
  Serial.print(F("执行"));
  Serial.print(useAcceleration ? F("G1") : F("G0"));
  Serial.print(F("：XY(mm)("));
  Serial.print(finalX_mm);
  Serial.print(F(","));
  Serial.print(finalY_mm);
  Serial.print(F(") | 电机AB("));
  Serial.print(targetA);
  Serial.print(F(","));
  Serial.print(targetB);
  Serial.print(F(") | 速度："));
  Serial.print(finalSpeed);
  Serial.println(F(")"));
}

// ====================== 舵机核心功能（非阻塞） ======================
// 启动非阻塞舵机转动
void startServoMove(int angle) {
  int constrainedAngle = constrain(angle, MIN_SERVO_ANGLE, MAX_SERVO_ANGLE);
  servoTargetAngle = constrainedAngle;
  myServo.write(servoTargetAngle);
  servoState = SERVO_MOVING;
  servoMoveStartTime = millis();
  // 预估舵机转动时间（假设0.2s/60度，这里简单固定为500ms）
  servoMoveDuration = 500;
  Serial.print(F("舵机开始转动到角度："));
  Serial.println(servoTargetAngle);
}

// 更新舵机状态（在loop中调用）
void updateServo() {
  switch (servoState) {
    case SERVO_MOVING:
      if (millis() - servoMoveStartTime >= servoMoveDuration) {
        // 移动完成
        servoCurrentAngle = servoTargetAngle;
        servoState = SERVO_IDLE;
        Serial.print(F("舵机到达目标角度："));
        Serial.println(servoCurrentAngle);
      }
      break;
    
    case SERVO_PRESS_DOWN:
      if (millis() - servoMoveStartTime >= servoMoveDuration) {
        // 按下动作完成，开始延迟
        pressDownEndTime = millis() + pressDelayTime;
        servoState = SERVO_PRESS_UP;
        Serial.println(F("舵机按下到位，开始延迟"));
      }
      break;
    
    case SERVO_PRESS_UP:
      if (millis() >= pressDownEndTime) {
        // 延迟完成，开始释放
        myServo.write(pressStartAngle);
        servoTargetAngle = pressStartAngle;
        servoMoveStartTime = millis();
        servoState = SERVO_MOVING;
        Serial.println(F("舵机开始释放"));
      }
      break;
    
    case SERVO_IDLE:
    default:
      break;
  }
}

// 设置舵机角度（非阻塞）
void setServoAngle(int angle) {
  startServoMove(angle);
}

// 舵机按下动作（非阻塞版本）
void servoPressAction(int startAngle, int endAngle, int delayTime) {
  // 角度范围约束
  pressStartAngle = constrain(startAngle, MIN_SERVO_ANGLE, MAX_SERVO_ANGLE);
  pressEndAngle = constrain(endAngle, MIN_SERVO_ANGLE, MAX_SERVO_ANGLE);
  // 延迟时间约束（防止过短/过长）
  pressDelayTime = constrain(delayTime, MIN_PRESS_DELAY, MAX_PRESS_DELAY);

  Serial.print(F("执行舵机按下动作（非阻塞）："));
  Serial.print(pressStartAngle);
  Serial.print(F("°→"));
  Serial.print(pressEndAngle);
  Serial.print(F("°（延迟"));
  Serial.print(pressDelayTime);
  Serial.println(F("ms后返回）"));

  // 开始按下动作（非阻塞）
  myServo.write(pressEndAngle);
  servoTargetAngle = pressEndAngle;
  servoMoveStartTime = millis();
  servoMoveDuration = 500; // 预估移动时间
  servoState = SERVO_PRESS_DOWN;
  Serial.println(F("舵机开始按下动作"));
}

// 舵机复位到初始角度（非阻塞）
void servoReset() {
  startServoMove(SERVO_INIT_ANGLE);
  Serial.println(F("舵机开始复位到初始角度"));
}

// ====================== GCode解析与处理（增强版） ======================
void parseGCodeCommand() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (bufferIndex > 0) {
        processGCode();
        memset(gcodeBuffer, 0, sizeof(gcodeBuffer));
        bufferIndex = 0;
      }
      continue;
    }

    if (c < 32 && c != ' ' && c != '\t') continue;
    if (bufferIndex < sizeof(gcodeBuffer) - 1) {
      gcodeBuffer[bufferIndex++] = c;
    } else {
      bufferIndex = 0;
      memset(gcodeBuffer, 0, sizeof(gcodeBuffer));
      Serial.println(F("错误：G代码指令过长！"));
    }
  }
}

// 解析浮点参数（支持负号和小数点）
float parseFloatParam(const char*& str) {
  char buf[16];
  uint8_t i = 0;
  bool decimal = false;
  // 跳过可能的前导空格（已由调用者清理，但为安全保留）
  while (*str == ' ') str++;
  if (*str == '-') {
    buf[i++] = *str;
    str++;
  }
  while (i < 15 && (isdigit(*str) || (*str == '.' && !decimal))) {
    if (*str == '.') decimal = true;
    buf[i++] = *str++;
  }
  buf[i] = '\0';
  return atof(buf);
}

// 解析整数参数（支持负号）
int parseIntParam(const char*& str) {
  char buf[8];
  uint8_t i = 0;
  while (*str == ' ') str++;
  if (*str == '-') {
    buf[i++] = *str;
    str++;
  }
  while (i < 7 && isdigit(*str)) {
    buf[i++] = *str++;
  }
  buf[i] = '\0';
  return atoi(buf);
}

void processGCode() {
  Serial.print(F("收到G代码："));
  Serial.println(gcodeBuffer);

  // 重置目标位置（毫米）
  targetX_mm = -9999;
  targetY_mm = -9999;
  float parsedFeedRate = feedRate;
  float newAcceleration = -1;
  int newDirMask = -1;
  float newScale = -1.0;
  float newSyncAcc = -1.0;
  int newServoAngle = -1;
  int servoPressStart = -1; // G31 P参数：按下动作起始角
  int servoPressEnd = -1;   // G31 Q参数：按下动作终止角
  int servoPressDelay = -1; // G31 R参数：按下动作延迟时间(ms)

  // 清理空格和制表符，生成紧凑字符串以便解析
  char cleanBuffer[64] = {0};
  uint8_t cleanIdx = 0;
  for (uint8_t i = 0; i < bufferIndex; i++) {
    if (gcodeBuffer[i] != ' ' && gcodeBuffer[i] != '\t') {
      cleanBuffer[cleanIdx++] = gcodeBuffer[i];
    }
  }

  // 逐字符解析指令和参数
  uint8_t i = 0;
  while (i < cleanIdx) {
    switch (cleanBuffer[i]) {
      // CoreXY基础指令
      case 'G':
        i++;
        if (cleanBuffer[i] == '9') {
          i++;
          isAbsoluteMode = (cleanBuffer[i] == '0');
          Serial.println(isAbsoluteMode ? F("切换为G90绝对坐标") : F("切换为G91相对坐标"));
          i++;
        } else if (cleanBuffer[i] == '0' || cleanBuffer[i] == '1') {
          bool useAccel = (cleanBuffer[i] == '1');
          i++;
          // 解析行内剩余参数
          while (i < cleanIdx) {
            char c = cleanBuffer[i];
            if (c == 'X' || c == 'Y' || c == 'F' || c == 'A' || c == 'D' || c == 'S' || c == 'C' || c == 'P' || c == 'Q' || c == 'R') {
              i++; // 跳过字母
              const char* start = &cleanBuffer[i];
              if (c == 'X') {
                targetX_mm = parseFloatParam(start);
                i = start - cleanBuffer;
              } else if (c == 'Y') {
                targetY_mm = parseFloatParam(start);
                i = start - cleanBuffer;
              } else if (c == 'F') {
                parsedFeedRate = parseFloatParam(start);
                i = start - cleanBuffer;
              } else if (c == 'A') {
                newAcceleration = parseFloatParam(start);
                i = start - cleanBuffer;
              } else if (c == 'D') {
                newDirMask = parseIntParam(start);
                i = start - cleanBuffer;
              } else if (c == 'S') {
                // S参数可能用于步进倍率或舵机角度，根据指令上下文区分
                // 但此处G0/G1中不会出现S，忽略
                parseFloatParam(start); // 跳过
                i = start - cleanBuffer;
              } else if (c == 'C') {
                newSyncAcc = parseFloatParam(start);
                i = start - cleanBuffer;
              } else if (c == 'P' || c == 'Q' || c == 'R') {
                // 这些参数在G0/G1中无效，忽略
                parseFloatParam(start);
                i = start - cleanBuffer;
              }
            } else {
              i++; // 未知字符，跳过
            }
          }
          moveCoreXY(useAccel, parsedFeedRate);
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '2') {
          i += 2;
          while (i < cleanIdx) {
            char c = cleanBuffer[i];
            if (c == 'A') {
              i++;
              const char* start = &cleanBuffer[i];
              newAcceleration = parseFloatParam(start);
              i = start - cleanBuffer;
            } else i++;
          }
          setAcceleration(newAcceleration);
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '3') {
          i += 2;
          while (i < cleanIdx) {
            char c = cleanBuffer[i];
            if (c == 'D') {
              i++;
              const char* start = &cleanBuffer[i];
              newDirMask = parseIntParam(start);
              i = start - cleanBuffer;
            } else i++;
          }
          setDirMask(newDirMask);
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '4') {
          i += 2;
          while (i < cleanIdx) {
            char c = cleanBuffer[i];
            if (c == 'S') {
              i++;
              const char* start = &cleanBuffer[i];
              newScale = parseFloatParam(start);
              i = start - cleanBuffer;
            } else i++;
          }
          setStepScale(newScale);
        } else if (cleanBuffer[i] == '2' && cleanBuffer[i+1] == '5') {
          i += 2;
          while (i < cleanIdx) {
            char c = cleanBuffer[i];
            if (c == 'C') {
              i++;
              const char* start = &cleanBuffer[i];
              newSyncAcc = parseFloatParam(start);
              i = start - cleanBuffer;
            } else i++;
          }
          setSyncAcceleration(newSyncAcc);
        }
        // 舵机专属指令
        else if (cleanBuffer[i] == '3' && cleanBuffer[i+1] == '0') {
          i += 2;
          while (i < cleanIdx) {
            char c = cleanBuffer[i];
            if (c == 'S') {
              i++;
              const char* start = &cleanBuffer[i];
              newServoAngle = parseIntParam(start); // 角度为整数
              i = start - cleanBuffer;
            } else i++;
          }
          if (newServoAngle >= 0) setServoAngle(newServoAngle);
          else Serial.println(F("错误：G30缺少S参数（例：G30 S90）"));
        } else if (cleanBuffer[i] == '3' && cleanBuffer[i+1] == '1') {
          i += 2;
          while (i < cleanIdx) {
            char c = cleanBuffer[i];
            if (c == 'P') {
              i++;
              const char* start = &cleanBuffer[i];
              servoPressStart = parseIntParam(start);
              i = start - cleanBuffer;
            } else if (c == 'Q') {
              i++;
              const char* start = &cleanBuffer[i];
              servoPressEnd = parseIntParam(start);
              i = start - cleanBuffer;
            } else if (c == 'R') {
              i++;
              const char* start = &cleanBuffer[i];
              servoPressDelay = parseIntParam(start);
              i = start - cleanBuffer;
            } else i++;
          }
          // 确定按下动作的参数（未指定则用默认值）
          int startAngle = (servoPressStart >= 0) ? servoPressStart : SERVO_INIT_ANGLE;
          int endAngle = (servoPressEnd >= 0) ? servoPressEnd : SERVO_DEFAULT_PRESS_ANGLE;
          int delayTime = (servoPressDelay >= 0) ? servoPressDelay : SERVO_DEFAULT_PRESS_DELAY;
          servoPressAction(startAngle, endAngle, delayTime); // 阻塞执行
        } else if (cleanBuffer[i] == '3' && cleanBuffer[i+1] == '2') {
          i += 2;
          servoReset(); // 舵机复位（非阻塞）
        }
        break;

      // 电机使能切换
      case 'M':
        i++;
        if (cleanBuffer[i] == '0') {
          motorEnabled = !motorEnabled;
          if (motorA) {
            if (motorEnabled) motorA->enableOutputs();
            else motorA->disableOutputs();
          }
          if (motorB) {
            if (motorEnabled) motorB->enableOutputs();
            else motorB->disableOutputs();
          }
          Serial.print(F("电机使能状态："));
          Serial.println(motorEnabled ? F("是") : F("否"));
        }
        i++;
        break;

      default:
        i++;
        break;
    }
  }
}

// ====================== 系统状态打印 ======================
void printSystemStatus() {
  unsigned long currentTime = millis();
  if (currentTime - lastPrintTime >= printInterval) {
    lastPrintTime = currentTime;

    if (!motorA || !motorB) {
      Serial.println(F("错误：电机初始化失败！"));
      return;
    }

    // CoreXY状态（步数转毫米）- 与moveCoreXY使用相同的逆解算逻辑
    long rawA = motorA->getCurrentPosition();
    long rawB = motorB->getCurrentPosition();
    
    // 1. 先应用A/B反转掩码
    if (dirMask & 0x04) rawA = -rawA; // A电机反转
    if (dirMask & 0x08) rawB = -rawB; // B电机反转
    
    // 2. 应用X/Y反转（CoreXY特殊逻辑，与正解算相反）
    if (dirMask & 0x02) {
      // Y反转：A和B交换
      long temp = rawA;
      rawA = rawB;
      rawB = temp;
    }
    if (dirMask & 0x01) {
      // X反转：A和B交换并取反
      long temp = rawA;
      rawA = -rawB;
      rawB = -temp;
    }
    
    // 3. 进行CoreXY逆解算
    long curr_x_steps = (rawA + rawB) / 2;
    long curr_y_steps = (rawB - rawA) / 2;
    
    // 转换为毫米
    float currentX_mm = stepsToMm(curr_x_steps);
    float currentY_mm = stepsToMm(curr_y_steps);

    // 打印综合状态（使用毫米）
    Serial.print(F("模式："));
    Serial.print(isAbsoluteMode ? F("G90") : F("G91"));
    Serial.print(F(" | 电机使能："));
    Serial.print(motorEnabled ? F("是") : F("否"));
    Serial.print(F(" | XY(.01mm)("));
    Serial.print(currentX_mm);
    Serial.print(F(","));
    Serial.print(currentY_mm);
    Serial.print(F(") | 舵机角度："));
    Serial.print(servoCurrentAngle);
    Serial.print(F("° | 倍率："));
    Serial.println(stepScale);
  }
}
