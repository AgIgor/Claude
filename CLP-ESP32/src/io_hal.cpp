#include "io_hal.h"
#include <Arduino.h>
#include "config.h"

void ioInit(bool diPullUp) {
  for (uint8_t i = 0; i < PLC_NUM_DI; i++) {
    pinMode(PIN_DI[i], diPullUp ? INPUT_PULLUP : INPUT_PULLDOWN);
  }
  for (uint8_t i = 0; i < PLC_NUM_DO; i++) {
    digitalWrite(PIN_DO[i], LOW);
    pinMode(PIN_DO[i], OUTPUT);
  }
  analogReadResolution(12);
  for (uint8_t i = 0; i < PLC_NUM_AI; i++) {
    analogSetPinAttenuation(PIN_AI[i], ADC_11db);
  }
}

uint32_t ioReadDI() {
  uint32_t mask = 0;
  for (uint8_t i = 0; i < PLC_NUM_DI; i++) {
    if (digitalRead(PIN_DI[i])) mask |= (1UL << i);
  }
  return mask;
}

void ioReadAI(int32_t* millivolts) {
  for (uint8_t i = 0; i < PLC_NUM_AI; i++) {
    millivolts[i] = (int32_t)analogReadMilliVolts(PIN_AI[i]);  // usa calibração de fábrica (eFuse)
  }
}

void ioWriteDO(uint32_t mask) {
  for (uint8_t i = 0; i < PLC_NUM_DO; i++) {
    digitalWrite(PIN_DO[i], (mask >> i) & 1 ? HIGH : LOW);
  }
}
