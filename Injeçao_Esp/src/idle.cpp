#include "idle.h"
#include "pins.h"

#define IDLE_LEDC_CH 0
#define IDLE_LEDC_BITS 10

static uint8_t type = IDLE_NONE;
static float pwmFreq = 0;
static int32_t stepMax = 200;
static bool dirInv = false;

static float targetPct = 0;
static int32_t stepPos = 0, stepTarget = 0;
static uint32_t lastStepMs = 0;
static bool homing = false;  // fechando o curso todo para achar o zero

void idleActuatorConfig(uint8_t t, float freq, float stepperMax, bool dirInvert) {
  int32_t sm = constrain((int32_t)stepperMax, 10, 2000);
  if (t == type && freq == pwmFreq && sm == stepMax && dirInvert == dirInv) return;

  if (type == IDLE_PWM) ledcDetachPin(PIN_IDLE);
  type = t;
  pwmFreq = freq;
  stepMax = sm;
  dirInv = dirInvert;

  pinMode(PIN_IDLE, OUTPUT);
  digitalWrite(PIN_IDLE, LOW);
  if (type == IDLE_PWM) {
    ledcSetup(IDLE_LEDC_CH, constrain(freq, 20.0f, 2000.0f), IDLE_LEDC_BITS);
    ledcAttachPin(PIN_IDLE, IDLE_LEDC_CH);
    idleActuatorSet(targetPct);
  } else if (type == IDLE_STEPPER) {
    pinMode(PIN_IDLE_DIR, OUTPUT);
    // Referencia: fecha o curso todo (+10%) e assume posicao 0
    stepPos = stepMax + stepMax / 10;
    stepTarget = 0;
    homing = true;
  }
}

void idleActuatorSet(float pct) {
  targetPct = constrain(pct, 0.0f, 100.0f);
  if (type == IDLE_PWM) {
    ledcWrite(IDLE_LEDC_CH, (uint32_t)(targetPct / 100 * ((1 << IDLE_LEDC_BITS) - 1)));
  } else if (type == IDLE_STEPPER && !homing) {
    stepTarget = lroundf(targetPct / 100 * stepMax);
  }
}

void idleActuatorTick(uint32_t nowMs, float stepMs) {
  if (type != IDLE_STEPPER || stepPos == stepTarget) return;
  if (nowMs - lastStepMs < (uint32_t)max(stepMs, 1.0f)) return;
  lastStepMs = nowMs;
  bool open = stepTarget > stepPos;
  digitalWrite(PIN_IDLE_DIR, open != dirInv);
  delayMicroseconds(2);
  digitalWrite(PIN_IDLE, HIGH);
  delayMicroseconds(3);
  digitalWrite(PIN_IDLE, LOW);
  stepPos += open ? 1 : -1;
  if (homing && stepPos == 0) {  // fim da referencia
    homing = false;
    idleActuatorSet(targetPct);
  }
}

float idleActuatorPos() {
  if (type == IDLE_STEPPER) return constrain(stepPos, 0, stepMax) * 100.0f / stepMax;
  return type == IDLE_PWM ? targetPct : 0;
}
