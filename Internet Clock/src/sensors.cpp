#include "sensors.h"
#include "settings.h"
#include <Wire.h>

// Drivers mínimos e não bloqueantes para AHT10/AHT20 e BH1750.

// ---------- AHT10 ----------
static const uint8_t AHT_ADDR = 0x38;

enum AhtState : uint8_t { AHT_IDLE, AHT_MEASURING };
static AhtState ahtState = AHT_IDLE;
static bool     ahtPresent = false;
static bool     ahtValid = false;
static uint8_t  ahtFails = 0;
static uint32_t ahtLastStart = 0;
static uint32_t ahtTrigger = 0;
static bool     ahtFirst = true;
static float    tAvg = NAN, hAvg = NAN;

static bool i2cWrite(uint8_t addr, const uint8_t* data, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

static bool ahtInit() {
  static const uint8_t reset[] = {0xBA};
  static const uint8_t initAht10[] = {0xE1, 0x08, 0x00};
  static const uint8_t initAht20[] = {0xBE, 0x08, 0x00};
  if (!i2cWrite(AHT_ADDR, reset, 1)) return false;
  delay(20);
  if (!i2cWrite(AHT_ADDR, initAht10, 3)) i2cWrite(AHT_ADDR, initAht20, 3);
  delay(10);
  return true;
}

static bool ahtStart() {
  static const uint8_t cmd[] = {0xAC, 0x33, 0x00};
  return i2cWrite(AHT_ADDR, cmd, 3);
}

// 1 = ok, 0 = ainda ocupado, -1 = erro
static int ahtRead(float& t, float& h) {
  if (Wire.requestFrom(AHT_ADDR, (uint8_t)6) != 6) return -1;
  uint8_t b[6];
  for (uint8_t i = 0; i < 6; i++) b[i] = Wire.read();
  if (b[0] & 0x80) return 0;
  uint32_t rh = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
  uint32_t rt = ((uint32_t)(b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];
  h = rh * 100.0f / 1048576.0f;
  t = rt * 200.0f / 1048576.0f - 50.0f;
  if (t < -40 || t > 85 || h < 0 || h > 100) return -1;
  return 1;
}

static void ahtFail() {
  ahtState = AHT_IDLE;
  if (++ahtFails >= 3) {
    ahtValid = false;
    ahtPresent = false;  // força reinicialização
  }
}

static void ahtLoop(uint32_t now) {
  uint32_t interval = (uint32_t)S.sensorInterval * 1000UL;

  if (ahtState == AHT_IDLE) {
    if (!ahtFirst && now - ahtLastStart < interval) return;
    ahtFirst = false;
    ahtLastStart = now;
    if (!ahtPresent) {
      ahtPresent = ahtInit();
      if (!ahtPresent) return;
    }
    if (!ahtStart()) { ahtFail(); return; }
    ahtTrigger = now;
    ahtState = AHT_MEASURING;
    return;
  }

  // Medição leva ~75ms
  if (now - ahtTrigger < 80) return;
  float t, h;
  int r = ahtRead(t, h);
  if (r == 0) {
    if (now - ahtTrigger > 300) ahtFail();
    return;
  }
  if (r < 0) { ahtFail(); return; }

  ahtState = AHT_IDLE;
  ahtFails = 0;
  if (!ahtValid || isnan(tAvg)) {
    tAvg = t;
    hAvg = h;
  } else {
    const float a = 0.3f;
    tAvg += a * (t - tAvg);
    hAvg += a * (h - hAvg);
  }
  ahtValid = true;
}

// Pressão de vapor de saturação (Magnus), hPa
static float satVapor(float t) {
  return 6.112f * expf(17.62f * t / (243.12f + t));
}

// ---------- BH1750 ----------
static uint8_t  bhAddr = 0;
static bool     bhValid = false;
static uint32_t bhLast = 0;
static uint32_t bhRetry = 0;
static float    luxAvg = NAN;

static bool bhInit() {
  static const uint8_t powerOn[] = {0x01};
  static const uint8_t contHiRes2[] = {0x11};  // 0.5 lux de resolução
  const uint8_t addrs[] = {0x23, 0x5C};
  for (uint8_t a : addrs) {
    if (i2cWrite(a, powerOn, 1) && i2cWrite(a, contHiRes2, 1)) {
      bhAddr = a;
      return true;
    }
  }
  bhAddr = 0;
  return false;
}

static void bhLoop(uint32_t now) {
  if (!bhAddr) {
    if (now - bhRetry < 10000 && bhRetry) return;
    bhRetry = now;
    if (!bhInit()) return;
    bhLast = now;
    return;
  }
  if (now - bhLast < 200) return;
  bhLast = now;
  if (Wire.requestFrom(bhAddr, (uint8_t)2) != 2) {
    bhValid = false;
    bhAddr = 0;
    return;
  }
  uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
  float lux = raw / 2.4f;
  if (!bhValid || isnan(luxAvg)) luxAvg = lux;
  else luxAvg += 0.25f * (lux - luxAvg);
  bhValid = true;
}

// ---------- API ----------
void Sensors::begin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);
  ahtPresent = ahtInit();
  bhInit();
}

void Sensors::loop() {
  uint32_t now = millis();
  ahtLoop(now);
  bhLoop(now);
}

bool  Sensors::climateValid() { return ahtValid; }
float Sensors::rawTemp() { return tAvg; }
float Sensors::rawHumi() { return hAvg; }

float Sensors::temp() {
  return tAvg + S.tempOffset10 / 10.0f;
}

float Sensors::humi() {
  // Mantém a umidade absoluta: se o sensor está mais quente que o ambiente,
  // a UR lida é menor que a real. Recalcula a UR para a temperatura corrigida.
  float t = temp();
  float h = hAvg * satVapor(tAvg) / satVapor(t) + S.humiOffset10 / 10.0f;
  return constrain(h, 0.0f, 100.0f);
}

bool  Sensors::luxValid() { return bhValid; }
float Sensors::lux() { return luxAvg; }
