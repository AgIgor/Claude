#pragma once
#include <stdint.h>

// Camada de abstração do hardware de I/O. Só este módulo conhece pinos.

void ioInit(bool diPullUp);
uint32_t ioReadDI();                 // bit n = nível lógico físico da entrada n
void ioReadAI(int32_t* millivolts);  // PLC_NUM_AI valores, em mV
void ioWriteDO(uint32_t mask);       // bit n = estado da saída n
