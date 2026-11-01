#include "capture.h"
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <soc/gpio_struct.h>

// Com 240 MHz, cada borda guarda o tempo em unidades de 2 ciclos (bit 0 = nível).
static const uint32_t CPU_HZ = 240000000;
static const uint32_t TICK_HZ = CPU_HZ / 2;
static const uint32_t REL_MAX = 0x70000000;  // limite de tempo relativo dentro de uma rajada

static const int PINS[CH_COUNT] = {PIN_CH_A, PIN_CH_B};

struct EdgeRing {
  volatile uint32_t head;
  volatile uint32_t tail;
  volatile uint32_t overflow;
  uint32_t buf[EDGE_RING_SIZE];
};

struct Channel {
  EdgeRing ring;
  uint32_t burst[BURST_MAX_EDGES];
  uint32_t burstN;
  uint64_t burstStart;
  uint64_t lastEdge;
  bool line;              // nível da linha depois da última borda
  uint32_t handledOverflow;
  uint32_t overflowUntil;

  uint32_t sample[SAMPLE_MAX_EDGES];
  uint32_t sampleN;
  uint32_t sampleEnd;     // fim da linha do tempo comprimida
  uint32_t sampleAtLastAnalysis;

  uint32_t scope[SCOPE_MAX_PULSES];  // largura em décimos de microssegundo
  uint32_t scopeN;
  bool scopeFirst;

  ChannelStats st;
};

// No heap: juntos, os buffers não cabem na RAM estática.
static Channel* chans[CH_COUNT];
static uint32_t* scratch;
static uint8_t frameBuf[sizeof(FrameHeader) + FRAME_MAX_BYTES];
static RingbufHandle_t ring;
static SemaphoreHandle_t mx;
static CaptureConfig cfg;
static volatile bool configChanged;
static volatile bool resetRequest;
static uint32_t ledUntil;
static void (*frameHook)(int ch, const uint8_t* data, size_t n);

static uint32_t clockCc;
static uint64_t clockAbs;
static uint64_t startUs;  // aproxima o relógio da captura do esp_timer usado nos marcadores

static inline uint32_t IRAM_ATTR cycleCount() {
  uint32_t c;
  __asm__ __volatile__("rsr %0, ccount" : "=a"(c));
  return c;
}

static void IRAM_ATTR onEdge(void* arg) {
  const uint32_t c = cycleCount();
  const uint32_t ch = (uint32_t)arg;
  const int pin = ch == 0 ? PIN_CH_A : PIN_CH_B;
  const uint32_t level = (GPIO.in1.data >> (pin - 32)) & 1;  // 34 e 35 ficam no segundo banco
  EdgeRing& r = chans[ch]->ring;
  const uint32_t head = r.head, next = (head + 1) & (EDGE_RING_SIZE - 1);
  if (next == r.tail) {  // ruído demais: desliga esta linha até a tarefa religar
    r.overflow = r.overflow + 1;
    GPIO.pin[pin].int_type = 0;
    return;
  }
  r.buf[head] = (c & ~1u) | level;
  r.head = next;
}

static void refreshClock() {
  const uint32_t c = cycleCount();
  clockAbs += (uint32_t)(c - clockCc);
  clockCc = c;
}

static inline uint64_t extendTime(uint32_t cc) {
  return clockAbs + (int32_t)(cc - clockCc);
}

static void clearSamples(Channel& c) {
  c.sampleN = 0;
  c.sampleEnd = 0;
  c.sampleAtLastAnalysis = 0;
}

static void storeScope(Channel& c) {
  xSemaphoreTake(mx, portMAX_DELAY);
  c.scopeFirst = c.burst[0] & 1;
  uint32_t n = 0;
  for (uint32_t i = 1; i < c.burstN && n < SCOPE_MAX_PULSES; i++) {
    uint32_t ticks = (c.burst[i] >> 1) - (c.burst[i - 1] >> 1);
    c.scope[n++] = (uint64_t)ticks * 10000000ULL / TICK_HZ;
  }
  c.scopeN = n;
  xSemaphoreGive(mx);
}

// Junta as rajadas numa linha do tempo sem os silêncios longos, que só atrapalhariam a análise.
static void storeSample(Channel& c) {
  if (c.sampleN + c.burstN > SAMPLE_MAX_EDGES) {
    if (c.sampleAtLastAnalysis < c.sampleN) return;  // ainda não analisou o que tem
    clearSamples(c);
    if (c.burstN > SAMPLE_MAX_EDGES) return;
  }
  const uint32_t relEnd = c.burst[c.burstN - 1] >> 1;
  if ((uint64_t)c.sampleEnd + relEnd + TICK_HZ / 100 >= REL_MAX) return;
  for (uint32_t i = 0; i < c.burstN; i++) {
    c.sample[c.sampleN++] = (((c.burst[i] >> 1) + c.sampleEnd) << 1) | (c.burst[i] & 1);
  }
  c.sampleEnd += relEnd + TICK_HZ / 100;  // 10 ms entre rajadas
}

// Com a fila cheia e nenhum navegador lendo, perde o quadro mais antigo.
static void pushFrame(int ch, const uint8_t* item, size_t size) {
  Channel& c = *chans[ch];
  while (xRingbufferSend(ring, item, size, 0) != pdTRUE) {
    size_t got;
    void* old = xRingbufferReceive(ring, &got, 0);
    if (!old) return;
    vRingbufferReturnItem(ring, old);
    c.st.droppedFrames++;
  }
}

static void decodeBurst(int ch) {
  Channel& c = *chans[ch];
  if (!cfg.baud) return;
  DecodeStats ds;
  FrameHeader* h = (FrameHeader*)frameBuf;
  uint8_t* data = frameBuf + sizeof(FrameHeader);
  // Depois do silêncio a linha está em repouso; antes da rajada ela pode estar desligada.
  const bool idle = c.burst[c.burstN - 1] & 1;
  size_t n = uartDecode(c.burst, c.burstN, idle, cfg.baud, UART_FORMATS[cfg.fmt], TICK_HZ,
                        data, FRAME_MAX_BYTES, ds);
  c.st.framing += ds.framing;
  c.st.parity += ds.parity;
  if (!n) return;
  h->tUs = startUs + c.burstStart / (CPU_HZ / 1000000);
  h->len = n;
  h->errors = ds.framing + ds.parity;
  h->ch = ch;
  c.st.frames++;
  c.st.bytes += n;
  pushFrame(ch, frameBuf, sizeof(FrameHeader) + n);
  if (frameHook) frameHook(ch, data, n);
#if LED_PIN >= 0
  digitalWrite(LED_PIN, HIGH);
  ledUntil = millis() + LED_HOLD_MS;
#endif
}

static void countPulses(Channel& c) {
  for (uint32_t i = 1; i < c.burstN; i++) {
    uint32_t us = ((c.burst[i] >> 1) - (c.burst[i - 1] >> 1)) / (TICK_HZ / 1000000);
    int b = 0;
    for (uint32_t limit = 5; b < PULSE_BUCKETS - 1 && us >= limit; limit *= 2) b++;
    c.st.hist[b]++;
  }
}

static void finishBurst(int ch) {
  Channel& c = *chans[ch];
  if (!c.burstN) return;
  countPulses(c);
  c.st.bursts++;
  c.st.edges += c.burstN;
  storeScope(c);
  storeSample(c);
  decodeBurst(ch);
  c.burstN = 0;
}

static void addEdge(int ch, uint32_t e) {
  Channel& c = *chans[ch];
  const bool level = e & 1;
  if (level == c.line) return;  // borda perdida ou repetida: o nível atual é o que vale
  const uint64_t t = extendTime(e & ~1u);
  const uint64_t gap = (uint64_t)cfg.gapMs * (CPU_HZ / 1000);
  if (c.burstN && t - c.lastEdge > gap) finishBurst(ch);
  if (!c.burstN) c.burstStart = t;
  const uint64_t ticks = (t - c.burstStart) / 2;
  if (ticks >= REL_MAX) {
    finishBurst(ch);
    c.burstStart = t;
  }
  c.burst[c.burstN++] = (uint32_t)((t - c.burstStart) / 2) << 1 | level;
  c.lastEdge = t;
  c.line = level;
  if (c.burstN == BURST_MAX_EDGES) finishBurst(ch);
}

static void handleOverflow(int ch, uint32_t ms) {
  Channel& c = *chans[ch];
  if (c.ring.overflow != c.handledOverflow) {
    c.handledOverflow = c.ring.overflow;
    c.st.overflows++;
    c.overflowUntil = ms + OVERFLOW_HOLD_MS;
    if (!c.overflowUntil) c.overflowUntil = 1;
    c.burstN = 0;
  }
  if (c.overflowUntil && (int32_t)(ms - c.overflowUntil) >= 0) {
    c.overflowUntil = 0;
    c.line = digitalRead(PINS[ch]);
    gpio_set_intr_type((gpio_num_t)PINS[ch], GPIO_INTR_ANYEDGE);
  }
}

static void analyze() {
  bool anyBetter = false;
  const CaptureConfig cur = cfg;
  for (int ch = 0; ch < CH_COUNT; ch++) {
    Channel& c = *chans[ch];
    if (c.sampleN <= c.sampleAtLastAnalysis) continue;
    UartAnalysis a;
    uartAnalyze(c.sample, c.sampleN, c.sample[c.sampleN - 1] & 1, TICK_HZ, scratch, a);
    c.sampleAtLastAnalysis = c.sampleN;
    xSemaphoreTake(mx, portMAX_DELAY);
    c.st.analysis = a;
    xSemaphoreGive(mx);
    anyBetter = true;
  }
  if (!anyBetter || !cur.autoLock) return;

  // Trava só se a melhor opção for confiável e a outra linha não discordar.
  const UartCandidate* best = nullptr;
  for (int ch = 0; ch < CH_COUNT; ch++) {
    const UartAnalysis& a = chans[ch]->st.analysis;
    if (!a.count) continue;
    const UartCandidate& c = a.top[0];
    if (c.score < LOCK_MIN_SCORE || c.good < LOCK_MIN_BYTES) continue;
    if (best && (best->baud != c.baud || best->fmt != c.fmt)) return;
    best = &c;
  }
  if (!best) return;
  cfg.baud = best->baud;
  cfg.fmt = best->fmt;
  cfg.autoLock = false;
  configChanged = true;
}

static void applyReset() {
  if (!resetRequest) return;
  resetRequest = false;
  for (Channel* p : chans) {
    Channel& c = *p;
    clearSamples(c);
    xSemaphoreTake(mx, portMAX_DELAY);
    c.st.analysis = UartAnalysis{};
    memset(c.st.hist, 0, sizeof(c.st.hist));
    xSemaphoreGive(mx);
  }
}

static void captureTask(void*) {
  uint32_t lastAnalysis = 0;
  for (;;) {
    refreshClock();
    applyReset();
    const uint32_t ms = millis();
    for (int ch = 0; ch < CH_COUNT; ch++) {
      Channel& c = *chans[ch];
      EdgeRing& r = c.ring;
      uint32_t tail = r.tail;
      const uint32_t head = r.head;
      while (tail != head) {
        const uint32_t e = r.buf[tail];
        tail = (tail + 1) & (EDGE_RING_SIZE - 1);
        r.tail = tail;
        addEdge(ch, e);
      }
      const uint64_t gap = (uint64_t)cfg.gapMs * (CPU_HZ / 1000);
      if (c.burstN && extendTime(cycleCount() & ~1u) - c.lastEdge > gap) finishBurst(ch);
      handleOverflow(ch, ms);
      c.st.level = c.line;
    }
    if (ms - lastAnalysis >= ANALYZE_MS) {
      lastAnalysis = ms;
      analyze();
    }
#if LED_PIN >= 0
    if (ledUntil && (int32_t)(ms - ledUntil) >= 0) {
      ledUntil = 0;
      digitalWrite(LED_PIN, LOW);
    }
#endif
    vTaskDelay(1);
  }
}

namespace Capture {

void begin(const CaptureConfig& c) {
  cfg = c;
  mx = xSemaphoreCreateMutex();
  ring = xRingbufferCreate(FRAME_RING_BYTES, RINGBUF_TYPE_NOSPLIT);
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  scratch = (uint32_t*)heap_caps_malloc(SAMPLE_MAX_EDGES * sizeof(uint32_t), caps);
  for (Channel*& p : chans) p = (Channel*)heap_caps_calloc(1, sizeof(Channel), caps);
#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT);
#endif
  startUs = esp_timer_get_time();
  clockCc = cycleCount();
  for (int ch = 0; ch < CH_COUNT; ch++) {
    pinMode(PINS[ch], INPUT);
    chans[ch]->line = digitalRead(PINS[ch]);
  }
  // Direto no driver: o attachInterrupt do Arduino passa por uma rotina na flash, que atrasa
  // as bordas e fica parada enquanto a flash é gravada.
  gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  for (int ch = 0; ch < CH_COUNT; ch++) {
    // Chamado no núcleo 1: o Wi-Fi fica no núcleo 0 e não atrasa a interrupção.
    gpio_isr_handler_add((gpio_num_t)PINS[ch], onEdge, (void*)ch);
    gpio_set_intr_type((gpio_num_t)PINS[ch], GPIO_INTR_ANYEDGE);
    gpio_intr_enable((gpio_num_t)PINS[ch]);
  }
  xTaskCreatePinnedToCore(captureTask, "capture", 4096, nullptr, 3, nullptr, 1);
}

CaptureConfig config() { return cfg; }

void setManual(uint32_t baud, int fmt) {
  cfg.autoLock = false;
  cfg.fmt = fmt;
  cfg.baud = baud;
}

void setAuto() {
  cfg.autoLock = true;
  cfg.baud = 0;
  resetRequest = true;
}

void setGap(uint16_t gapMs) { cfg.gapMs = gapMs; }

void setFrameHook(void (*hook)(int, const uint8_t*, size_t)) { frameHook = hook; }

void resetAnalysis() { resetRequest = true; }

bool takeConfigChanged() {
  if (!configChanged) return false;
  configChanged = false;
  return true;
}

void stats(int ch, ChannelStats& out) {
  Channel& c = *chans[ch];
  xSemaphoreTake(mx, portMAX_DELAY);
  out = c.st;
  out.sampleEdges = c.sampleN;
  xSemaphoreGive(mx);
}

String scopeJson() {
  String o;
  o.reserve(14000);
  o = "{\"c\":[";
  xSemaphoreTake(mx, portMAX_DELAY);
  for (int ch = 0; ch < CH_COUNT; ch++) {
    Channel& c = *chans[ch];
    if (ch) o += ',';
    o += "{\"first\":";
    o += c.scopeFirst ? 1 : 0;
    o += ",\"p\":[";
    for (uint32_t i = 0; i < c.scopeN; i++) {
      if (i) o += ',';
      o += c.scope[i];
    }
    o += "]}";
  }
  xSemaphoreGive(mx);
  o += "]}";
  return o;
}

uint64_t nowUs() { return esp_timer_get_time(); }

RingbufHandle_t frames() { return ring; }

}  // namespace Capture
