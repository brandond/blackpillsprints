#include "Arduino.h"
#include "stm32f4xx.h"

HardwareTimer *TimerA;
HardwareTimer *TimerB;
HardwareTimer *TimerI;

volatile uint32_t RotationsA;
volatile uint32_t LastCaptureA;
volatile uint32_t IntervalA;
volatile bool OverflowA;

volatile uint32_t RotationsB;
volatile uint32_t LastCaptureB;
volatile uint32_t IntervalB;
volatile bool OverflowB;

volatile bool Ticked;
volatile uint32_t Ticks;

uint32_t CurrentCapture;

char StringBuffer[512];

// blink, dummy
void blink(int count){
  for(int i=0; i < count; i++){
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    delay(250);
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    delay(250);
  }
}

// reboot into stm32 bootloader
// stolen from micropython
void bootloader() {
    HAL_RCC_DeInit();
    HAL_DeInit();

    __HAL_REMAPMEMORY_SYSTEMFLASH();
    __ASM volatile ("movs r3, #0\nldr r3, [r3, #0]\nMSR msp, r3\n" : : : "r3");
    ((void (*)(void)) *((uint32_t*) 0x00000004))();

    while(1);
}

// blink a few times then reboot
void error(){
  blink(10);
  HAL_NVIC_SystemReset();
}

// Calculate time since last A rotation and increment counter
void trigger_a_callback(){
  CurrentCapture = TimerA->getCaptureCompare(2, MICROSEC_COMPARE_FORMAT);
  if (OverflowA && (CurrentCapture < LastCaptureA)) {
    IntervalA = (TimerA->getOverflow(MICROSEC_FORMAT) - LastCaptureA) + CurrentCapture;
  } else if (LastCaptureA != 0){
    IntervalA = CurrentCapture - LastCaptureA;
  }
  LastCaptureA = CurrentCapture;
  OverflowA = false;
  RotationsA++;
}

// Set overflow A flag and reset stats if overflow has not been cleared by trigger since last overflow
void overflow_a_callback(){
  if (OverflowA) {
    LastCaptureA = 0;
    RotationsA = 0;
    IntervalA = 0;
  } else {
    OverflowA = true;
  }
}

// Calculate time since last B rotation and increment counter
void trigger_b_callback(){
  CurrentCapture = TimerB->getCaptureCompare(3, MICROSEC_COMPARE_FORMAT);
  if (OverflowB && (CurrentCapture < LastCaptureB)) {
    IntervalB = (TimerB->getOverflow(MICROSEC_FORMAT) - LastCaptureB) + CurrentCapture;
  } else if (LastCaptureB != 0){
    IntervalB = CurrentCapture - LastCaptureB;
  }
  LastCaptureB = CurrentCapture;
  OverflowB = false;
  RotationsB++;
}

// Set overflow B flag and reset stats if overflow has not been cleared by trigger since last overflow
void overflow_b_callback(){
  if (OverflowB) {
    LastCaptureB = 0;
    RotationsB = 0;
    IntervalB = 0;
  } else {
    OverflowB = true;
  }
}

// system timer interrupt, just counts up at 1hz and the main loop does the rest outside ISR
void interval_callback(){
  Ticked = true;
  Ticks++;
}

// init stuff
void setup(){
  SerialUSB.begin();

  pinMode(PA0, INPUT_PULLUP); // onboard button
  pinMode(PC13, OUTPUT);      // onboard LED
  digitalWrite(PC13, HIGH);   // LED off at startup

  // TIM2 counts player A revolutions on PA1 (AF01 TIM2_CH2)
  TimerA = new HardwareTimer(TIM2);
  TimerA->setMode(2, TIMER_INPUT_CAPTURE_FALLING, PA1); // sensor is normally high, pulled down to ground when magnet is present
  TimerA->setCaptureCompare(2, 2);                      // require settled reading for 2 ticks before counting
  TimerA->setPrescaleFactor(4096);                      // timer clock runs 2^12 slower than system clock, to prevent overflow within sampling interval
  TimerA->attachInterrupt(2, trigger_a_callback);
  TimerA->attachInterrupt(overflow_a_callback);

  // TIM5 counts player B revolutions on PA2 (AF02 TIM5_CH3)
  TimerB = new HardwareTimer(TIM5);
  TimerB->setMode(3, TIMER_INPUT_CAPTURE_FALLING, PA2); // sensor is normally high, pulled down to ground when magnet is present
  TimerB->setCaptureCompare(3, 2);                      // require settled reading for 2 ticks before counting
  TimerB->setPrescaleFactor(4096);                      // timer clock runs 2^12 slower than system clock, to prevent overflow within sampling interval
  TimerB->attachInterrupt(3, trigger_b_callback);
  TimerB->attachInterrupt(overflow_b_callback);

  // TIM3 ticks, triggering report via USB serial at regular intervals
  TimerI = new HardwareTimer(TIM3);
  TimerI->setOverflow(1, HERTZ_FORMAT);
  TimerI->attachInterrupt(interval_callback);

  // Set PA1 pin mode
  LL_GPIO_SetPinMode(GPIOA, GPIO_PIN_1, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinPull(GPIOA, GPIO_PIN_1, LL_GPIO_PULL_UP);
  LL_GPIO_SetAFPin_0_7(GPIOA, GPIO_PIN_1, GPIO_AF1_TIM2);

  // Set PA2 pin mode
  LL_GPIO_SetPinMode(GPIOA, GPIO_PIN_2, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinPull(GPIOA, GPIO_PIN_2, LL_GPIO_PULL_UP);
  LL_GPIO_SetAFPin_0_7(GPIOA, GPIO_PIN_2, GPIO_AF2_TIM5);

  // Start timers
  TimerA->resumeChannel(2);
  TimerB->resumeChannel(3);
  TimerI->resume();
}

// low priority IO loop
void loop(){
  // check for onboard button press - resets into DFU mode
  if (digitalRead(PA0) == LOW){
    bootloader();
  }

  // check for report interval tick
  if (Ticked) {
    Ticked = false;
    // distance is rotations * roller diameter in meters
    float metersA = (float)RotationsA * 0.35908;
    float metersB = (float)RotationsB * 0.35908;
    // last revolution interval in millis, just divide by 1000 to convert from micros
    float millisA = (float)IntervalA / 1000.0;
    float millisB = (float)IntervalB / 1000.0;
    // speed is distance over time - roller diameter in cm divided by revolution interval in millis, times 36 to get km/h
    float speedA = (35.908 / millisA) * 36.0;
    float speedB = (35.908 / millisB) * 36.0;

    int len = sprintf(StringBuffer, "%05u  A count=%05u dist=%07.2fm time=%07.2fms speed=%05.2fkph  B count=%05u dist=%07.2fm time=%07.2fms speed=%05.2fkph\n",
        Ticks,
        RotationsA, metersA, millisA, speedA,
        RotationsB, metersB, millisB, speedB);
    SerialUSB.write(StringBuffer, len);
  }
}
