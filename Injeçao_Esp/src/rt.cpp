#include "rt.h"
#include <driver/gpio.h>
#include <driver/timer.h>
#include <soc/gpio_reg.h>
#include "pins.h"

#define TG TIMER_GROUP_0
#define TI TIMER_0

RtParams rt;

static portMUX_TYPE rtMux = portMUX_INITIALIZER_UNLOCKED;

// ------------------------------------------------------------ agendador

enum : uint8_t { SLOT_IDLE = 0, SLOT_PENDING = 1, SLOT_ON = 2 };

struct Slot {
  uint64_t onTime;
  uint64_t offTime;
  uint32_t mask;
  uint8_t state;
  uint8_t invert;
  uint8_t fireSet;  // bobina: horario da centelha ja definido
};

// 0..3 = bobinas, 4..7 = bicos, 8..11 = janelas de detonacao
#define NSLOTS 12
#define SLOT_KNOCK 8
static Slot slots[NSLOTS];
static volatile uint32_t knockEvents;
static volatile uint8_t knockCh;

static inline uint64_t IRAM_ATTR nowUs() {
  return timer_group_get_counter_value_in_isr(TG, TI);  // contador de 1 MHz
}

static inline void IRAM_ATTR outWrite(uint32_t mask, bool active, bool invert) {
  if (active != invert)
    REG_WRITE(GPIO_OUT_W1TS_REG, mask);
  else
    REG_WRITE(GPIO_OUT_W1TC_REG, mask);
}

// Executa os eventos vencidos e programa o alarme para o proximo.
// Chamar sempre com rtMux tomado.
static void IRAM_ATTR service() {
  for (;;) {
    uint64_t now = nowUs();
    uint64_t next = UINT64_MAX;
    for (int i = 0; i < NSLOTS; i++) {
      Slot &s = slots[i];
      if (s.state == SLOT_PENDING) {
        if (s.onTime <= now + 1) {
          outWrite(s.mask, true, s.invert);
          s.state = SLOT_ON;
        } else {
          if (s.onTime < next) next = s.onTime;
          continue;
        }
      }
      if (s.state == SLOT_ON) {
        if (s.offTime <= now + 1) {
          outWrite(s.mask, false, s.invert);
          s.state = SLOT_IDLE;
          if (i >= SLOT_KNOCK) {  // janela fechou: a tarefa le o TPIC8101
            knockCh = i - SLOT_KNOCK;
            knockEvents++;
          }
        } else if (s.offTime < next) {
          next = s.offTime;
        }
      }
    }
    if (next == UINT64_MAX) return;
    // No ESP32 um alarme no passado so dispararia apos estourar o contador,
    // entao eventos muito proximos sao resolvidos aqui mesmo no laco.
    if (next > nowUs() + 4) {
      timer_group_set_alarm_value_in_isr(TG, TI, next);
      timer_group_enable_alarm_in_isr(TG, TI);
      return;
    }
  }
}

static bool IRAM_ATTR timerIsr(void *) {
  portENTER_CRITICAL_ISR(&rtMux);
  service();
  portEXIT_CRITICAL_ISR(&rtMux);
  return false;
}

// ------------------------------------------------------------ decodificador

static volatile uint8_t dTeeth = 60, dMissing = 2, dCamEnabled = 1;
static volatile uint8_t dCamA = 1, dCamB = 0;  // pulsos de fase esperados em cada volta
static volatile int32_t dDeg10 = 60;  // decimos de grau por dente

static volatile uint64_t lastToothUs;
static volatile uint32_t lastPeriod;   // periodo de 1 dente (normalizado na falha)
static volatile uint16_t toothNo;      // 1..(teeth-missing), 0 = desconhecido
static volatile uint8_t gapsSeen;
static volatile bool crankSync;
static volatile uint64_t lastGapUs;
static volatile uint32_t revTimeUs;
static volatile uint32_t syncLoss, revs;

static volatile uint8_t phase;     // 0/1: qual volta do ciclo de 720 graus
static volatile uint8_t camPulses; // pulsos de fase na volta atual
static volatile bool camValid;
static volatile uint8_t camGood;

static inline int32_t IRAM_ATTR wrapDiff(int32_t target, int32_t angle, int32_t cycle) {
  int32_t d = (target - angle) % cycle;
  return d < 0 ? d + cycle : d;
}

// Agenda os eventos que caem entre este dente e o proximo.
static void IRAM_ATTR scheduleFromTooth(uint64_t now, int32_t angle10, int32_t span10,
                                        uint32_t period, int32_t cycle10) {
  const int32_t deg10 = dDeg10;
  const uint32_t lat = rt.latencyUs;
  bool changed = false;

  if (rt.ignEnabled) {
    for (int c = 0; c < 4; c++) {
      uint32_t mask = rt.ignMask[c];
      if (!mask) continue;
      Slot &s = slots[c];
      int32_t d = wrapDiff(rt.ignCharge10[c], angle10, cycle10);
      if (d < span10 && s.state == SLOT_IDLE) {
        uint64_t t = now + (uint32_t)d * period / deg10;
        if (t > now + lat) t -= lat;
        s.onTime = t;
        s.offTime = t + rt.maxDwellUs;  // seguranca ate a centelha ser agendada
        s.mask = mask;
        s.invert = rt.ignInvert;
        s.fireSet = 0;
        s.state = SLOT_PENDING;
        changed = true;
      }
      d = wrapDiff(rt.ignFire10[c], angle10, cycle10);
      if (d < span10 && s.state != SLOT_IDLE && !s.fireSet) {
        uint64_t t = now + (uint32_t)d * period / deg10;
        if (t > now + lat) t -= lat;
        if (t <= s.onTime) t = s.onTime + 1;
        if (t < s.offTime) s.offTime = t;
        s.fireSet = 1;
        changed = true;
      }
    }
  }

  if (rt.ignEnabled && rt.knockEnabled) {
    for (int c = 0; c < 4; c++) {
      if (!rt.ignMask[c]) continue;
      Slot &s = slots[SLOT_KNOCK + c];
      int32_t d = wrapDiff(rt.knockStart10[c], angle10, cycle10);
      if (d < span10 && s.state == SLOT_IDLE) {
        uint64_t t = now + (uint32_t)d * period / deg10;
        s.onTime = t;
        s.offTime = t + rt.knockWinUs;
        s.mask = 1u << PIN_KNOCK_HOLD;
        s.invert = 0;
        s.state = SLOT_PENDING;
        changed = true;
      }
    }
  }

  uint32_t pw = rt.injPwUs;
  if (rt.injEnabled && pw > 0) {
    for (int c = 0; c < 4; c++) {
      uint32_t mask = rt.injMask[c];
      if (!mask) continue;
      Slot &s = slots[4 + c];
      int32_t d = wrapDiff(rt.injStart10[c], angle10, cycle10);
      if (d < span10 && s.state == SLOT_IDLE) {
        uint64_t t = now + (uint32_t)d * period / deg10;
        if (t > now + lat) t -= lat;
        s.onTime = t;
        s.offTime = t + pw;
        s.mask = mask;
        s.invert = rt.injInvert;
        s.state = SLOT_PENDING;
        changed = true;
      }
    }
  }

  if (changed) service();
}

static void IRAM_ATTR ckpIsr(void *) {
  portENTER_CRITICAL_ISR(&rtMux);
  uint64_t now = nowUs();
  const uint32_t missing = dMissing;
  const uint32_t realTeeth = dTeeth - missing;

  if (lastToothUs == 0) {  // primeiro dente apos reset
    lastToothUs = now;
    portEXIT_CRITICAL_ISR(&rtMux);
    return;
  }
  uint32_t dt = (uint32_t)(now - lastToothUs);
  if (crankSync && dt < (lastPeriod >> 2)) {  // pulso espurio (ruido)
    portEXIT_CRITICAL_ISR(&rtMux);
    return;
  }
  lastToothUs = now;

  // Falha: intervalo ~ (missing+1)x o dente anterior. Limiar no meio do caminho.
  bool gap = lastPeriod && (uint64_t)dt * 2 > (uint64_t)lastPeriod * (2 + missing);

  if (gap) {
    if (gapsSeen && toothNo != realTeeth) {
      if (crankSync) syncLoss++;
      crankSync = false;
      gapsSeen = 1;
    } else {
      if (gapsSeen < 2) gapsSeen++;
      crankSync = gapsSeen >= 2;
    }
    toothNo = 1;
    if (lastGapUs) revTimeUs = (uint32_t)(now - lastGapUs);
    lastGapUs = now;
    revs++;

    if (dCamEnabled) {
      // Conta os pulsos de fase da volta que terminou: contagem A -> a proxima
      // volta e a 0; contagem B -> volta 1. As voltas precisam alternar.
      uint8_t n = camPulses;
      camPulses = 0;
      uint8_t np = n == dCamA ? 0 : (n == dCamB ? 1 : 2);
      if (np == 2 || np == phase) {
        camGood = 0;
        camValid = false;
      } else if (camGood < 3) {
        camGood++;
      }
      if (camGood >= 2) camValid = true;
      phase = np & 1;
    }
    lastPeriod = dt / (missing + 1);
  } else {
    if (toothNo) {
      toothNo++;
      if (toothNo > realTeeth) {  // passou do numero de dentes sem achar a falha
        if (crankSync) syncLoss++;
        crankSync = false;
        toothNo = 0;
        gapsSeen = 0;
      }
    }
    lastPeriod = dt;
  }

  if (crankSync) {
    const int32_t deg10 = dDeg10;
    int32_t angle10 = (toothNo - 1) * deg10;
    int32_t span10 = (toothNo == realTeeth) ? deg10 * (missing + 1) : deg10;
    bool seq = rt.sequential;
    if (!seq || camValid) {
      if (seq && phase) angle10 += 3600;
      scheduleFromTooth(now, angle10, span10, lastPeriod, seq ? 7200 : 3600);
    }
  }
  portEXIT_CRITICAL_ISR(&rtMux);
}

static void IRAM_ATTR cmpIsr(void *) {
  if (camPulses < 255) camPulses++;
}

// ------------------------------------------------------------ API

static void resetDecoderLocked() {
  lastToothUs = 0;
  lastPeriod = 0;
  toothNo = 0;
  gapsSeen = 0;
  crankSync = false;
  lastGapUs = 0;
  revTimeUs = 0;
  camPulses = 0;
  camValid = false;
  camGood = 0;
}

static void killLocked() {
  for (int i = 0; i < NSLOTS; i++) {
    if (slots[i].state == SLOT_ON) outWrite(slots[i].mask, false, slots[i].invert);
    slots[i].state = SLOT_IDLE;
  }
  for (int i = 0; i < 4; i++) {
    outWrite(1u << PIN_IGN[i], false, rt.ignInvert);
    outWrite(1u << PIN_INJ[i], false, rt.injInvert);
  }
  outWrite(1u << PIN_KNOCK_HOLD, false, false);
}

void rtInit() {
  memset((void *)&rt, 0, sizeof(rt));
  for (int i = 0; i < 4; i++) {
    pinMode(PIN_IGN[i], OUTPUT);
    pinMode(PIN_INJ[i], OUTPUT);
  }
  pinMode(PIN_KNOCK_HOLD, OUTPUT);
  killLocked();

  timer_config_t tc = {};
  tc.alarm_en = TIMER_ALARM_DIS;
  tc.counter_en = TIMER_PAUSE;
  tc.intr_type = TIMER_INTR_LEVEL;
  tc.counter_dir = TIMER_COUNT_UP;
  tc.auto_reload = TIMER_AUTORELOAD_DIS;
  tc.divider = 80;  // APB 80 MHz -> 1 MHz
  timer_init(TG, TI, &tc);
  timer_set_counter_value(TG, TI, 1);
  timer_isr_callback_add(TG, TI, timerIsr, nullptr, 0);
  timer_start(TG, TI);

  pinMode(PIN_CKP, INPUT);
  pinMode(PIN_CMP, INPUT);
  gpio_install_isr_service(0);  // ja instalado -> erro ignorado
  gpio_isr_handler_add((gpio_num_t)PIN_CKP, ckpIsr, nullptr);
  gpio_isr_handler_add((gpio_num_t)PIN_CMP, cmpIsr, nullptr);
}

void rtApplyTrigger(uint8_t teeth, uint8_t missing, bool ckpRising, bool camEnabled, bool camRising,
                    uint8_t camCountA, uint8_t camCountB) {
  gpio_set_intr_type((gpio_num_t)PIN_CKP, ckpRising ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);
  gpio_set_intr_type((gpio_num_t)PIN_CMP,
                     camEnabled ? (camRising ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE) : GPIO_INTR_DISABLE);
  portENTER_CRITICAL(&rtMux);
  dTeeth = teeth;
  dMissing = missing;
  dDeg10 = 3600 / teeth;
  dCamEnabled = camEnabled;
  dCamA = camCountA;
  dCamB = camCountB;
  resetDecoderLocked();
  killLocked();
  portEXIT_CRITICAL(&rtMux);
}

void rtSnapshot(DecoderSnapshot &s) {
  portENTER_CRITICAL(&rtMux);
  s.crankSync = crankSync;
  s.camSync = camValid;
  s.revTimeUs = revTimeUs;
  s.toothPeriodUs = lastPeriod;
  s.lastToothUs = lastToothUs;
  s.nowUs = nowUs();
  s.syncLoss = syncLoss;
  s.revs = revs;
  portEXIT_CRITICAL(&rtMux);
}

uint32_t rtKnockEvents(uint8_t &ch) {
  portENTER_CRITICAL(&rtMux);
  uint32_t n = knockEvents;
  ch = knockCh;
  portEXIT_CRITICAL(&rtMux);
  return n;
}

void rtResetSync() {
  portENTER_CRITICAL(&rtMux);
  resetDecoderLocked();
  killLocked();
  portEXIT_CRITICAL(&rtMux);
}

void rtKillOutputs() {
  portENTER_CRITICAL(&rtMux);
  killLocked();
  portEXIT_CRITICAL(&rtMux);
}

bool rtTestPulse(bool ign, uint8_t ch, uint32_t durUs) {
  if (ch > 3) return false;
  portENTER_CRITICAL(&rtMux);
  Slot &s = slots[(ign ? 0 : 4) + ch];
  bool ok = s.state == SLOT_IDLE;
  if (ok) {
    s.onTime = nowUs() + 20;
    s.offTime = s.onTime + durUs;
    s.mask = 1u << (ign ? PIN_IGN[ch] : PIN_INJ[ch]);
    s.invert = ign ? rt.ignInvert : rt.injInvert;
    s.fireSet = 1;
    s.state = SLOT_PENDING;
    service();
  }
  portEXIT_CRITICAL(&rtMux);
  return ok;
}
