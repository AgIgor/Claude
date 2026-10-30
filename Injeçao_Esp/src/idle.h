#pragma once
#include <Arduino.h>

enum IdleType : uint8_t { IDLE_NONE = 0, IDLE_PWM = 1, IDLE_STEPPER = 2 };

// Atuador de marcha lenta. Posicao sempre em % (0 = fechado, 100 = aberto).
void idleActuatorConfig(uint8_t type, float pwmFreq, float stepperMax, bool dirInvert);
void idleActuatorSet(float pct);
void idleActuatorTick(uint32_t nowMs, float stepMs);  // chamar a cada 1 ms
float idleActuatorPos();  // posicao real (motor de passo pode estar a caminho)
