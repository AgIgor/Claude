#pragma once
#include <Arduino.h>

void knockInit(float freqKhz, uint8_t gainIdx, uint8_t integIdx);
uint16_t knockRead();
