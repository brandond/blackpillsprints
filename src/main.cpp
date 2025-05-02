#include "Arduino.h"
#include "stm32f4xx.h"

#define OUTPUT_INTERVAL_HERTZ 20

HardwareTimer *TimerP;
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
volatile uint32_t StartTime;

volatile bool Simulate;
volatile uint32_t SimA;
volatile uint32_t SimB;

const char* JSONFormat = "{\"time\":%u,\"a\":{\"distance\":\"%.2f\",\"speed\":\"%.2f\"},\"b\":{\"distance\":\"%.2f\",\"speed\":\"%.2f\"}}\n";

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
  uint32_t currentCapture = TimerP->getCaptureCompare(2, MICROSEC_COMPARE_FORMAT);
  if (OverflowA && (currentCapture < LastCaptureA)) {
    IntervalA = (TimerP->getOverflow(MICROSEC_FORMAT) - LastCaptureA) + currentCapture;
  } else if (LastCaptureA != 0){
    IntervalA = currentCapture - LastCaptureA;
  }
  if (StartTime == 0){
    StartTime = HAL_GetTick();
  }
  LastCaptureA = currentCapture;
  OverflowA = false;
  RotationsA++;
}

// Calculate time since last B rotation and increment counter
void trigger_b_callback(){
  uint32_t currentCapture = TimerP->getCaptureCompare(3, MICROSEC_COMPARE_FORMAT);
  if (OverflowB && (currentCapture < LastCaptureB)) {
    IntervalB = (TimerP->getOverflow(MICROSEC_FORMAT) - LastCaptureB) + currentCapture;
  } else if (LastCaptureB != 0){
    IntervalB = currentCapture - LastCaptureB;
  }
  if (StartTime == 0){
    StartTime = HAL_GetTick();
  }
  LastCaptureB = currentCapture;
  OverflowB = false;
  RotationsB++;
}

// Set overflow flags and reset stats if overflow has not been cleared by trigger since last overflow
void overflow_callback(){
  if (OverflowA && OverflowB){
    StartTime = 0;
  }
  if (OverflowA) {
    LastCaptureA = 0;
    RotationsA = 0;
    IntervalA = 0;
  } else {
    OverflowA = true;
  }
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

  // TIM2 counts player roller revolutions:
  // A on PA1 (AF01 TIM2_CH2)
  // B on PA2 (AF01 TIM2_CH3)
  TimerP = new HardwareTimer(TIM2);
  TimerP->setMode(2, TIMER_INPUT_CAPTURE_FALLING, PA1); // sensor is normally high, pulled down to ground when magnet is present
  TimerP->setMode(3, TIMER_INPUT_CAPTURE_FALLING, PA2); // sensor is normally high, pulled down to ground when magnet is present
  TimerP->setCaptureCompare(2, 2);                      // require settled reading for 2 ticks before counting
  TimerP->setCaptureCompare(3, 2);                      // require settled reading for 2 ticks before counting
  TimerP->setPrescaleFactor(4096);                      // timer clock runs 2^12 slower than system clock, to prevent overflow within sampling interval
  TimerP->attachInterrupt(2, trigger_a_callback);
  TimerP->attachInterrupt(3, trigger_b_callback);
  TimerP->attachInterrupt(overflow_callback);

  // TIM3 ticks, triggering report via USB serial at regular intervals
  TimerI = new HardwareTimer(TIM3);
  TimerI->setOverflow(OUTPUT_INTERVAL_HERTZ, HERTZ_FORMAT);
  TimerI->attachInterrupt(interval_callback);

  // Set PA1 pin mode
  LL_GPIO_SetPinMode(GPIOA, GPIO_PIN_1, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinPull(GPIOA, GPIO_PIN_1, LL_GPIO_PULL_UP);
  LL_GPIO_SetAFPin_0_7(GPIOA, GPIO_PIN_1, GPIO_AF1_TIM2);

  // Set PA2 pin mode
  LL_GPIO_SetPinMode(GPIOA, GPIO_PIN_2, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinPull(GPIOA, GPIO_PIN_2, LL_GPIO_PULL_UP);
  LL_GPIO_SetAFPin_0_7(GPIOA, GPIO_PIN_2, GPIO_AF1_TIM2);

  // Start timers
  TimerP->resumeChannel(2);
  TimerP->resumeChannel(3);
  TimerI->resume();
}

// Reset stats and toggle between sim and hardware timers
void toggleSim(){
  RotationsA = 0;
  IntervalA = 0;
  OverflowA = false;
  RotationsB = 0;
  IntervalB = 0;
  OverflowB = false;

  if (Simulate){
    Simulate = false;
    StartTime = 0;
    LastCaptureA = 0;
    LastCaptureB = 0;
    TimerP->resumeChannel(2);
    TimerP->resumeChannel(3);
  } else {
    TimerP->pause();
    Simulate = true;
    StartTime = HAL_GetTick();
    randomSeed(StartTime);
    LastCaptureA = StartTime;
    LastCaptureB = StartTime;
    SimA = StartTime + random(400, 501);
    SimB = StartTime + random(400, 501);
  }
}

// calculate change vs current interval time for next interval
inline int32_t deltaOffset(uint32_t delta){
    if (delta > 100) { // way too slow, wait a lot less next time
      return random(-100,-24);
    } else if (delta > 25) { // too slow, wait less next time
      return random(-5,3);
    } else if (delta < 15) { // too fast, wait more next time
      return random(-2,6);
    } else { // just jitter a bit
      return random(-1,2);
    }
}

// advance state one tick
void stepSim(){
  uint32_t curTime = HAL_GetTick();
  uint32_t delta;
  if (curTime >= SimA) {
    delta = curTime - LastCaptureA;
    IntervalA = delta * 1000;
    SimA += delta + deltaOffset(delta);
    LastCaptureA = curTime;
    RotationsA++;
  }
  if (curTime >= SimB) {
    delta = curTime - LastCaptureB;
    IntervalB = delta * 1000;
    SimB += delta + deltaOffset(delta);
    LastCaptureB = curTime;
    RotationsB++;
  }
}

// low priority IO loop
void loop(){
  // check for onboard button press - toggles sim state
  if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == 0){
    toggleSim();
  }

  // advance state one tick if sim is active
  if (Simulate){
    stepSim();
  }

  // check for report interval tick
  if (Ticked) {
    Ticked = false;

    // don't bother printing if the port isn't configured yet
    if (! SerialUSB) {
      return;
    }

    uint32_t elapsedTime = 0;
    float metersA, metersB, millisA, millisB, speedA, speedB = 0;

    // only output once per second if data is 0
    if ((StartTime == 0) && (Ticks % OUTPUT_INTERVAL_HERTZ != 0)){
      return;
    }

    // milliseconds since first event since counters were reset
    if (StartTime != 0){
      elapsedTime = HAL_GetTick() - StartTime;
    }

    if (IntervalA > 0) {
      // distance is rotations * roller diameter in meters
      metersA = (float)RotationsA * 0.35908;
      // last revolution interval in millis, just divide by 1000 to convert from micros
      millisA = (float)IntervalA / 1000.0;
      // speed is distance over time - roller diameter in cm divided by revolution interval in millis, times 36 to get km/h
      speedA = (35.908 / millisA) * 36.0;
    }
    if (IntervalB > 0) {
      // distance is rotations * roller diameter in meters
      metersB = (float)RotationsB * 0.35908;
      // last revolution interval in millis, just divide by 1000 to convert from micros
      millisB = (float)IntervalB / 1000.0;
      // speed is distance over time - roller diameter in cm divided by revolution interval in millis, times 36 to get km/h
      speedB = (35.908 / millisB) * 36.0;
    }

    /*
    int len = sprintf(StringBuffer, "%05u  %07u  A count=%05u dist=%07.2fm time=%07.2fms speed=%05.2fkph  B count=%05u dist=%07.2fm time=%07.2fms speed=%05.2fkph\n",
        Ticks, elapsedTime,
        RotationsA, metersA, millisA, speedA,
        RotationsB, metersB, millisB, speedB);
    */

    int len = sprintf(StringBuffer, JSONFormat, elapsedTime, metersA, speedA, metersB, speedB);
    SerialUSB.write(StringBuffer, len);
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
  }
}
