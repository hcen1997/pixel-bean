#include <FastAccelStepper.h>
// 未实机测试，仅示例代码

// 电机1的引脚
#define enablePin1     8
#define dirPin1        5
#define stepPin1       9

// 电机2的引脚
#define enablePin2     7
#define dirPin2        4
#define stepPin2       10

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper1 = NULL;
FastAccelStepper *stepper2 = NULL;

void setup() {
  // 初始化串口
  Serial.begin(115200);

  // 初始化引擎
  engine.init();

  // 创建第一个步进电机
  stepper1 = engine.stepperConnectToPin(stepPin1);
  if (stepper1) {
    stepper1->setDirectionPin(dirPin1);
    stepper1->setEnablePin(enablePin1);
    stepper1->setAutoEnable(false);
    stepper1->setSpeedInHz(500);    // 500 步/秒
    stepper1->setAcceleration(100); // 100 步/秒²
    stepper1->enableOutputs();
  }

  // 创建第二个步进电机
  stepper2 = engine.stepperConnectToPin(stepPin2);
  if (stepper2) {
    stepper2->setDirectionPin(dirPin2);
    stepper2->setEnablePin(enablePin2);
    stepper2->setAutoEnable(false);
    stepper2->setSpeedInHz(300);    // 300 步/秒
    stepper2->setAcceleration(50);  // 50 步/秒²
    stepper2->enableOutputs();
  }
}

void loop() {
  // 控制电机1：从0移动到1000，再返回0
  if (stepper1) {
    stepper1->moveTo(1000);
    while (stepper1->isRunning()) {
      // 等待完成
    }
    stepper1->moveTo(0);
    while (stepper1->isRunning()) {
      // 等待完成
    }
  }

  // 控制电机2：从0移动到500，再返回0
  if (stepper2) {
    stepper2->moveTo(500);
    while (stepper2->isRunning()) {
      // 等待完成
    }
    stepper2->moveTo(0);
    while (stepper2->isRunning()) {
      // 等待完成
    }
  }

  // 延迟一段时间
  delay(1000);
}