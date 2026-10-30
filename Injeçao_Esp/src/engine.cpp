#include "engine.h"
#include <SPI.h>
#include "pins.h"
#include "rt.h"
#include "settings.h"
#include "idle.h"
#include "knock.h"

LiveData live;
TestRequest testReq;

#define STALL_US 250000  // sem dente por 250 ms = motor parado

// ------------------------------------------------------------ sensores

static uint16_t mcpRead(uint8_t ch) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_ADC_CS, LOW);
  SPI.transfer(0x06 | (ch >> 2));  // start + single-ended + D2
  uint8_t hi = SPI.transfer((ch & 3) << 6) & 0x0F;
  uint8_t lo = SPI.transfer(0);
  digitalWrite(PIN_ADC_CS, HIGH);
  SPI.endTransaction();
  return (hi << 8) | lo;
}

static inline float rawToV(uint16_t raw) { return raw * (ADC_VREF / 4095.0f); }

// NTC com pull-up para 3,3 V. Retorna NAN se aberto/curto.
static float ntcTemp(uint16_t raw, float r25, float beta) {
  if (raw < 20 || raw > 4075) return NAN;
  float v = rawToV(raw);
  float r = cfg.ntcPullup * v / (ADC_VREF - v);
  return 1.0f / (1.0f / 298.15f + logf(r / r25) / beta) - 273.15f;
}

static float filt(float cur, float val, float a) { return isnan(cur) ? val : cur + (val - cur) * a; }

static float s_map = NAN, s_clt = NAN, s_iat = NAN, s_o2 = 0, s_tps = 0, s_vbat = NAN;
static uint8_t s_fault = 0;
static uint8_t slowCh = 0;

static void readSensors() {
  // MAP todo ciclo (1 kHz)
  float vMap = rawToV(mcpRead(ADC_MAP)) * cfg.divMap;
  float dv = cfg.mapV2 - cfg.mapV1;
  if (vMap < 0.1f || vMap > 4.95f || dv == 0) {
    s_fault |= 1;
    if (isnan(s_map)) s_map = 100;
  } else {
    s_fault &= ~1;
    float kpa = cfg.mapKpa1 + (vMap - cfg.mapV1) * (cfg.mapKpa2 - cfg.mapKpa1) / dv;
    s_map = filt(s_map, kpa, constrain(cfg.mapFilter, 0.01f, 1.0f));
  }

  // Demais sensores em rodizio (cada um a cada 5 ms)
  switch (slowCh) {
    case 0: {
      float t = ntcTemp(mcpRead(ADC_CLT), cfg.cltR25, cfg.cltBeta);
      if (isnan(t)) { s_fault |= 2; if (isnan(s_clt)) s_clt = 80; }
      else { s_fault &= ~2; s_clt = filt(s_clt, t, 0.05f); }
      break;
    }
    case 1: {
      float t = ntcTemp(mcpRead(ADC_IAT), cfg.iatR25, cfg.iatBeta);
      if (isnan(t)) { s_fault |= 4; if (isnan(s_iat)) s_iat = 30; }
      else { s_fault &= ~4; s_iat = filt(s_iat, t, 0.05f); }
      break;
    }
    case 2:
      s_o2 = filt(s_o2, rawToV(mcpRead(ADC_O2)), 0.5f);
      break;
    case 3:
      if (cfg.tpsEnabled) {
        float v = rawToV(mcpRead(ADC_TPS)) * cfg.divTps;
        float span = cfg.tpsVmax - cfg.tpsVmin;
        if (v < 0.1f || v > 4.95f || span == 0) { s_fault |= 8; s_tps = 0; }
        else { s_fault &= ~8; s_tps = filt(s_tps, constrain((v - cfg.tpsVmin) / span * 100, 0, 100), 0.5f); }
      } else {
        s_fault &= ~8;
        s_tps = 0;
      }
      break;
    case 4:
      s_vbat = filt(s_vbat, rawToV(mcpRead(ADC_VBAT)) * cfg.divVbat, 0.2f);
      break;
  }
  slowCh = (slowCh + 1) % 5;
}

// ------------------------------------------------------------ estado


static uint32_t runStartMs = 0;
static uint32_t aeUntilMs = 0;
static float clCorr = 0;
static uint32_t lastClMs = 0;
static bool revCut = false;
static float mapHist[4];  // MAP a cada 10 ms (para derivada)
static float tpsHist[4];
static uint8_t histIdx = 0;
static float mapDot = 0, tpsDot = 0;

// Marcha lenta / A/C / detonacao
volatile float idleOverride = -1;
static float idleI = 0, idleDashpot = 0, idleAdvTrim = 0;
static bool idleCl = false;
static bool acReq = false, acOn = false;
static uint32_t acReqSinceMs = 0, acWotUntilMs = 0;
static float knockRetard[4];
static uint16_t knockLevel[4];
static uint32_t knockSeen = 0, knockCount = 0;

static struct {
  bool active;
  uint8_t type, ch;
  uint16_t left, intervalMs;
  uint32_t durUs, nextMs, relayOffMs;
} test;

// Reaplica a configuracao da roda fonica / fase / TPIC8101 quando muda pela web
static void checkHwConfig() {
  static uint8_t applied[7];
  static bool done = false;
  uint8_t cur[7] = {cfg.teeth,         cfg.missingTeeth, cfg.ckpRisingEdge, cfg.camEnabled,
                    cfg.camRisingEdge, cfg.camCountA,    cfg.camCountB};
  if (!done || memcmp(cur, applied, sizeof(cur))) {
    memcpy(applied, cur, sizeof(cur));
    done = true;
    rtApplyTrigger(cfg.teeth, cfg.missingTeeth, cfg.ckpRisingEdge, cfg.camEnabled,
                   cfg.camRisingEdge, cfg.camCountA, cfg.camCountB);
  }

  static float kFreq = -1;
  static uint8_t kGain = 255, kInteg = 255, kOn = 255;
  if (cfg.knockEnabled != kOn || cfg.knockFreqKhz != kFreq || cfg.knockGainIdx != kGain ||
      cfg.knockIntegIdx != kInteg) {
    kOn = cfg.knockEnabled;
    kFreq = cfg.knockFreqKhz;
    kGain = cfg.knockGainIdx;
    kInteg = cfg.knockIntegIdx;
    if (kOn) knockInit(kFreq, kGain, kInteg);
  }

  idleActuatorConfig(cfg.idleType, cfg.idlePwmFreq, cfg.idleStepperMax, cfg.idleStepperDirInvert);
}

static int32_t norm10(float deg, int32_t cycle10) {
  int32_t a = (int32_t)lroundf(deg * 10) % cycle10;
  return a < 0 ? a + cycle10 : a;
}

// Ordem de ignicao 1-3-4-2: PMS de cada cilindro (graus apos o PMS do cil. 1)
static const float CYL_TDC[4] = {0, 540, 180, 360};

static void compute(const DecoderSnapshot &d, uint32_t nowMs) {
  checkHwConfig();

  float rpm = (d.crankSync && d.revTimeUs) ? 60e6f / d.revTimeUs : 0;
  bool running = rpm > 0;
  bool cranking = running && rpm < cfg.crankRpm;
  bool seq = cfg.camEnabled && d.camSync;

  if (!running || cranking) runStartMs = 0;
  else if (runStartMs == 0) runStartMs = nowMs;
  float runSec = runStartMs ? (nowMs - runStartMs) / 1000.0f : 0;

  float map = s_map, clt = s_clt, iat = s_iat, vbat = isnan(s_vbat) ? 12 : s_vbat;
  if (isnan(clt)) clt = 80;
  if (isnan(iat)) iat = 30;

  // Derivadas para enriquecimento de aceleracao (janela de 20 ms)
  static uint8_t div10 = 0;
  if (++div10 >= 10) {
    div10 = 0;
    uint8_t old = (histIdx + 3) & 3;  // amostra de 20 ms atras
    mapDot = (map - mapHist[old]) * 50;
    tpsDot = (s_tps - tpsHist[old]) * 50;
    histIdx = (histIdx + 1) & 3;
    mapHist[histIdx] = map;
    tpsHist[histIdx] = s_tps;
  }

  // ---------------- combustivel
  float reqFuel = (cfg.displacementCc / 4 * 1.204e-3f / cfg.stoichAfr / cfg.fuelDensity) /
                  (cfg.injFlowCcMin / 60000.0f);
  float ve = tableLookup(cfg.ve, rpm, map);
  float lam = constrain(tableLookup(cfg.lambda, rpm, map), 0.6f, 1.3f);
  float warm = curveLookup(cfg.warmup, clt) / 100;
  float iatCorr = 293.15f / (iat + 273.15f);

  float ae = 0, afterStart = 0;
  if (running && !cranking) {
    if (mapDot > cfg.aeMapdotThresh || (cfg.tpsEnabled && tpsDot > cfg.aeTpsdotThresh))
      aeUntilMs = nowMs + (uint32_t)cfg.aeDurationMs;
    if ((int32_t)(aeUntilMs - nowMs) > 0) ae = cfg.aePct / 100;
    if (runSec < cfg.afterStartSec && cfg.afterStartSec > 0)
      afterStart = cfg.afterStartPct / 100 * (1 - runSec / cfg.afterStartSec);
  }

  if (rpm > cfg.revLimit) revCut = true;
  else if (rpm < cfg.revLimit - cfg.revLimitHyst) revCut = false;

  bool dfco = cfg.dfcoEnabled && running && !cranking && rpm > cfg.dfcoRpm && map < cfg.dfcoMap &&
              clt > 60 && (!cfg.tpsEnabled || s_tps < 2);

  // Malha fechada com sonda banda estreita: so mantem lambda 1
  bool clActive = cfg.closedLoop && running && !cranking && clt >= cfg.clMinClt &&
                  rpm >= cfg.clMinRpm && rpm <= cfg.clMaxRpm && map <= cfg.clMaxMap &&
                  fabsf(lam - 1) < 0.02f && ae == 0 && !dfco && runSec > cfg.clDelaySec;
  if (!clActive) {
    clCorr = 0;
  } else if (nowMs - lastClMs >= 50) {
    lastClMs = nowMs;
    clCorr += (s_o2 > cfg.o2Threshold) ? -cfg.clStepPct : cfg.clStepPct;
    clCorr = constrain(clCorr, -cfg.clLimitPct, cfg.clLimitPct);
  }

  float fuelMs;
  bool floodClear = cranking && cfg.tpsEnabled && s_tps > 90;
  if (cranking) {
    fuelMs = reqFuel * ve / 100 * map / 100 * iatCorr * curveLookup(cfg.crankEnrich, clt) / 100;
  } else {
    fuelMs = reqFuel * ve / 100 * map / 100 * iatCorr * warm / lam * (1 + ae + afterStart) *
             (1 + clCorr / 100);
  }

  int pulses = seq ? 1 : 2;  // semissequencial: 2 pulsos por ciclo
  float dead = curveLookup(cfg.deadTime, vbat);
  float pwMs = fuelMs / pulses + dead;
  float usPerDeg = running ? 60e6f / (rpm * 360) : 0;
  float duty = 0;
  if (running) {
    float availMs = 720.0f / pulses * usPerDeg / 1000;
    float maxPw = availMs * cfg.maxDutyPct / 100;
    if (pwMs > maxPw) pwMs = maxPw;
    duty = pwMs / availMs * 100;
  }
  bool fuelCut = revCut || dfco || floodClear || fuelMs <= 0;

  // ---------------- ignicao
  float adv;
  if (cfg.fixedAdvanceOn) adv = cfg.fixedAdvance;
  else if (cranking) adv = cfg.crankAdvance;
  else adv = tableLookup(cfg.ign, rpm, map) + curveLookup(cfg.cltAdvance, clt) + idleAdvTrim;
  adv = constrain(adv, -10, 50);
  bool knockOn = cfg.knockEnabled && running && !cranking && !cfg.fixedAdvanceOn;

  float dwellMs = cranking ? cfg.crankDwellMs : curveLookup(cfg.dwell, vbat);
  dwellMs = min(dwellMs, cfg.maxDwellMs);

  // ---------------- publica para as interrupcoes
  if (running) {
    int32_t cycle10 = seq ? 7200 : 3600;
    float dwellDeg = dwellMs * 1000 / usPerDeg;
    float maxDwellDeg = (seq ? 720 : 360) * 0.8f;  // nunca encavala com o proximo disparo
    if (dwellDeg > maxDwellDeg) dwellDeg = maxDwellDeg;
    float pwDeg = pwMs * 1000 / usPerDeg;
    for (int c = 0; c < 4; c++) {
      if (!seq && c >= 2) {
        rt.ignMask[c] = 0;
        rt.injMask[c] = 0;
        continue;
      }
      float tdc = cfg.tdcOffset + (seq ? CYL_TDC[c] : c * 180.0f);
      float fire = tdc - (knockOn ? adv - knockRetard[c] : adv);
      rt.ignFire10[c] = norm10(fire, cycle10);
      rt.ignCharge10[c] = norm10(fire - dwellDeg, cycle10);
      rt.injStart10[c] = norm10(tdc - cfg.injEndAngle - pwDeg, cycle10);
      rt.knockStart10[c] = norm10(tdc + cfg.knockWinStart, cycle10);
      if (seq) {
        rt.ignMask[c] = 1u << PIN_IGN[c];
        rt.injMask[c] = 1u << PIN_INJ[c];
      } else if (c == 0) {  // par cil. 1 e 4
        rt.ignMask[c] = (1u << PIN_IGN[0]) | (1u << PIN_IGN[3]);
        rt.injMask[c] = (1u << PIN_INJ[0]) | (1u << PIN_INJ[3]);
      } else {              // par cil. 2 e 3 (180 graus depois)
        rt.ignMask[c] = (1u << PIN_IGN[1]) | (1u << PIN_IGN[2]);
        rt.injMask[c] = (1u << PIN_INJ[1]) | (1u << PIN_INJ[2]);
      }
    }
    rt.injPwUs = fuelCut ? 0 : (uint32_t)(pwMs * 1000);
    rt.knockWinUs = (uint32_t)(constrain(cfg.knockWinLen, 5.0f, 170.0f) * usPerDeg);
    rt.knockEnabled = cfg.knockEnabled;
    rt.maxDwellUs = (uint32_t)(cfg.maxDwellMs * 1000);
    rt.latencyUs = (uint32_t)max(cfg.outputLatencyUs, 0.0f);
    rt.ignInvert = cfg.ignInvert;
    rt.injInvert = cfg.injInvert;
    rt.sequential = seq;
    rt.ignEnabled = 1;
    rt.injEnabled = !fuelCut;
  } else {
    rt.ignEnabled = 0;
    rt.injEnabled = 0;
    rt.knockEnabled = 0;
    rt.ignInvert = cfg.ignInvert;
    rt.injInvert = cfg.injInvert;
  }

  // ---------------- dados ao vivo
  live.rpm = rpm;
  live.map = map;
  live.clt = clt;
  live.iat = iat;
  live.o2 = s_o2;
  live.tps = s_tps;
  live.vbat = vbat;
  live.mapDot = mapDot;
  live.ve = ve;
  live.lambdaTarget = lam;
  live.warmPct = warm * 100;
  live.aePct = ae * 100;
  live.afterStartPct = afterStart * 100;
  live.clCorrPct = clCorr;
  live.reqFuelMs = reqFuel;
  live.pwMs = running && !fuelCut ? pwMs : 0;
  live.dutyPct = duty;
  live.advance = adv;
  live.dwellMs = dwellMs;
  live.sync = d.crankSync;
  live.camSync = d.camSync;
  live.sequential = seq;
  live.cranking = cranking;
  live.running = running;
  live.revCut = revCut;
  live.dfco = dfco;
  live.clActive = clActive;
  live.sensorFault = s_fault;
  live.syncLoss = d.syncLoss;
  live.revs = d.revs;
  for (int c = 0; c < 4; c++) {
    live.knockLevel[c] = knockLevel[c];
    live.knockRetard[c] = knockRetard[c];
  }
  live.knockCount = knockCount;
}

// ------------------------------------------------------------ detonacao (a cada 1 ms)

static void knockProcess(uint32_t nowMs) {
  if (!cfg.knockEnabled) {
    memset(knockRetard, 0, sizeof(knockRetard));
    return;
  }
  uint8_t ch;
  uint32_t n = rtKnockEvents(ch);
  if (n != knockSeen) {  // uma janela fechou desde a ultima volta da tarefa
    knockSeen = n;
    uint16_t v = knockRead();
    knockLevel[ch & 3] = v;
    bool active = live.running && !live.cranking && live.rpm >= cfg.knockMinRpm &&
                  live.map >= cfg.knockMinMap;
    if (active && v > curveLookup(cfg.knockThreshold, live.rpm)) {
      knockRetard[ch & 3] = min(knockRetard[ch & 3] + cfg.knockRetardStep, cfg.knockMaxRetard);
      knockCount++;
    }
  }
  static uint32_t lastRecover = 0;
  if (nowMs - lastRecover >= 100) {
    lastRecover = nowMs;
    for (float &r : knockRetard) r = max(0.0f, r - cfg.knockRecoverDegSec * 0.1f);
  }
}

// ------------------------------------------------------------ marcha lenta e A/C (a cada 10 ms)

static void idleAndAc(uint32_t nowMs) {
  const float dt = 0.01f;
  float rpm = live.rpm, clt = live.clt;
  bool running = live.running, cranking = live.cranking;
  float runSec = runStartMs ? (nowMs - runStartMs) / 1000.0f : 0;

  // ---- A/C: sobe a lenta primeiro, acopla o compressor depois do atraso
  acReq = cfg.acEnabled && digitalRead(PIN_AC_REQ) == (cfg.acInputActiveHigh ? HIGH : LOW);
  if (cfg.tpsEnabled && live.tps > cfg.acWotTps) acWotUntilMs = nowMs + (uint32_t)(cfg.acWotCutSec * 1000);
  bool acAllowed = running && !cranking && runSec > 2 && rpm >= cfg.acMinRpm &&
                   rpm <= cfg.acMaxRpm && clt <= cfg.acMaxClt &&
                   (int32_t)(acWotUntilMs - nowMs) <= 0;
  bool acWanted = acReq && acAllowed;
  if (!acWanted) acReqSinceMs = 0;
  else if (!acReqSinceMs) acReqSinceMs = nowMs;
  acOn = acWanted && nowMs - acReqSinceMs >= (uint32_t)cfg.acDelayMs;

  // ---- marcha lenta
  float target = curveLookup(cfg.idleTarget, clt) + (acWanted ? cfg.idleAcRpmUp : 0);
  float base = curveLookup(cfg.idleBase, clt) + (acWanted ? cfg.idleAcAdder : 0) +
               ((live.fan1 || live.fan2) ? cfg.idleFanAdder : 0);
  float pos;
  idleAdvTrim = 0;
  idleCl = false;
  if (!running || cranking) {
    pos = cfg.idleCrankPos;
    idleI = 0;
    idleDashpot = cfg.idleDashpotPct;
  } else {
    bool atIdle = cfg.tpsEnabled ? live.tps < cfg.idleTpsMax : rpm < target + cfg.idleActivationRpm;
    if (atIdle && runSec > 1) {
      idleCl = true;
      float err = target - rpm;
      idleI = constrain(idleI + cfg.idleKi * err * dt, -cfg.idleIntLimit, cfg.idleIntLimit);
      idleDashpot = max(0.0f, idleDashpot - cfg.idleDashpotDecay * dt);
      pos = base + cfg.idleKp * err + idleI + idleDashpot;
      // Avanco reage em milissegundos; a valvula, em centenas
      if (!cfg.fixedAdvanceOn)
        idleAdvTrim = constrain(err * cfg.idleAdvGain, -cfg.idleAdvLimit, cfg.idleAdvLimit);
    } else {
      // Fora da lenta: segura a valvula um pouco aberta para o motor nao morrer ao soltar o pedal
      idleDashpot = cfg.idleDashpotPct;
      pos = base + idleI + idleDashpot;
    }
  }
  if (idleOverride >= 0) pos = idleOverride;
  pos = constrain(pos, 0.0f, 100.0f);
  if (cfg.idleType != IDLE_NONE) idleActuatorSet(pos);

  live.idlePos = cfg.idleType == IDLE_NONE ? 0 : idleActuatorPos();
  live.idleTarget = target;
  live.idleAdvTrim = idleAdvTrim;
  live.idleClosedLoop = idleCl;
  live.acReq = acReq;
  live.acOn = acOn;
}

// ------------------------------------------------------------ saidas auxiliares

static void auxOutputs(const DecoderSnapshot &d, uint32_t nowMs) {
  static bool fan1 = false, fan2 = false;
  float clt = live.clt;
  bool cltFault = s_fault & 2;
  if (clt >= cfg.fan1On || cltFault) fan1 = true;
  else if (clt <= cfg.fan1Off) fan1 = false;
  if (clt >= cfg.fan2On || cltFault) fan2 = true;
  else if (clt <= cfg.fan2Off) fan2 = false;
  bool acFan1 = acOn && cfg.acFan == 1, acFan2 = acOn && cfg.acFan == 2;

  // Bomba: escorva ao ligar e depois so com rotacao (seguranca em caso de batida)
  bool turning = d.lastToothUs && (d.nowUs - d.lastToothUs) < 1000000;
  bool pump = turning || nowMs < (uint32_t)(cfg.pumpPrimeSec * 1000);

  bool tFan1 = false, tFan2 = false, tPump = false, tAc = false;
  if (test.active && test.type >= TEST_FAN1) {
    bool on = (int32_t)(test.relayOffMs - nowMs) > 0;
    if (!on) test.active = false;
    tFan1 = on && test.type == TEST_FAN1;
    tFan2 = on && test.type == TEST_FAN2;
    tPump = on && test.type == TEST_PUMP;
    tAc = on && test.type == TEST_AC;
  }

  digitalWrite(PIN_FAN1, fan1 || acFan1 || tFan1);
  digitalWrite(PIN_FAN2, fan2 || acFan2 || tFan2);
  digitalWrite(PIN_PUMP, pump || tPump);
  digitalWrite(PIN_AC, acOn || tAc);
  live.fan1 = fan1 || acFan1 || tFan1;
  live.fan2 = fan2 || acFan2 || tFan2;
  live.pump = pump || tPump;

  // LED: aceso = sincronizado; pisca rapido = so rotacao sem sincronismo.
  // Com motor de passo o GPIO2 e o DIR do driver.
  if (cfg.idleType != IDLE_STEPPER) {
    bool led = d.crankSync ? true : (turning && (nowMs / 100) % 2);
    digitalWrite(PIN_LED, led);
  }
}

static void runTests(uint32_t nowMs) {
  if (testReq.pending) {
    testReq.pending = false;
    if (live.rpm == 0) {
      test.active = true;
      test.type = testReq.type;
      test.ch = testReq.ch & 3;
      test.left = testReq.count;
      test.intervalMs = max<uint16_t>(testReq.intervalMs, 20);
      float ms = testReq.type == TEST_IGN ? min(testReq.ms, cfg.maxDwellMs) : min(testReq.ms, 50.0f);
      test.durUs = (uint32_t)(ms * 1000);
      test.nextMs = nowMs;
      test.relayOffMs = nowMs + (uint32_t)testReq.ms;
    }
  }
  if (!test.active || test.type > TEST_IGN) return;
  if (live.rpm > 0 || test.left == 0) {
    test.active = false;
    return;
  }
  if ((int32_t)(nowMs - test.nextMs) >= 0) {
    rtTestPulse(test.type == TEST_IGN, test.ch, test.durUs);
    test.left--;
    test.nextMs = nowMs + test.intervalMs;
  }
}

// ------------------------------------------------------------ tarefa

static void engineTask(void *) {
  rtInit();
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&lastWake, 1);
    uint32_t t0 = micros();
    uint32_t nowMs = millis();

    readSensors();

    DecoderSnapshot d;
    rtSnapshot(d);
    if (d.lastToothUs && d.nowUs - d.lastToothUs > STALL_US) {
      rtResetSync();
      rtSnapshot(d);
    }

    // Se a web estiver alterando a config neste instante, mantem os valores anteriores
    if (xSemaphoreTake(cfgMutex, 0) == pdTRUE) {
      compute(d, nowMs);
      knockProcess(nowMs);
      static uint8_t div = 0;
      if (++div >= 10) {
        div = 0;
        idleAndAc(nowMs);
        auxOutputs(d, nowMs);
      }
      xSemaphoreGive(cfgMutex);
    }
    idleActuatorTick(nowMs, cfg.idleStepMs);
    runTests(nowMs);
    live.loopUs = micros() - t0;
  }
}

void engineStart() {
  pinMode(PIN_FAN1, OUTPUT);
  pinMode(PIN_FAN2, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_AC, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_FAN1, LOW);
  digitalWrite(PIN_FAN2, LOW);
  digitalWrite(PIN_PUMP, LOW);
  digitalWrite(PIN_AC, LOW);
  pinMode(PIN_AC_REQ, INPUT);

  pinMode(PIN_ADC_CS, OUTPUT);
  digitalWrite(PIN_ADC_CS, HIGH);
  pinMode(PIN_KNOCK_CS, OUTPUT);  // TPIC8101 sempre desselecionado fora das leituras
  digitalWrite(PIN_KNOCK_CS, HIGH);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_ADC_CS);

  memset(&live, 0, sizeof(live));
  // Nucleo 1, prioridade alta. Wi-Fi e servidor web ficam no nucleo 0.
  xTaskCreatePinnedToCore(engineTask, "engine", 8192, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
}
