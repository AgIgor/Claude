#include "display.h"
#include "settings.h"
#include "sensors.h"
#include "network.h"
#include <NeoPixelBus.h>
#include <time.h>
#include <sys/time.h>

// Saída para a fita. Dois métodos:
//  - GPIO0, bit-bang: a CPU gera o sinal; interrupções do Wi-Fi (NMI) podem
//    corromper bits e causar piscadas aleatórias.
//  - GPIO3/RX, DMA (I2S): o hardware gera o sinal, imune ao Wi-Fi.
class Strip {
 public:
  void begin(bool dma) {
    if (dma) {
      dmaBus = new NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod>((uint16_t)NUM_LEDS);
      dmaBus->Begin();
    } else {
      bbBus = new NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod>((uint16_t)NUM_LEDS, (uint8_t)PIN_LEDS);
      bbBus->Begin();
    }
    clear();
  }
  void clear() { memset(px, 0, sizeof(px)); }
  void setPixelColor(uint16_t i, uint32_t c) {
    if (i < NUM_LEDS) px[i] = c;
  }
  const uint32_t* getPixels() const { return px; }
  void show() {
    if (dmaBus) send(*dmaBus);
    else if (bbBus) send(*bbBus);
  }

 private:
  template <typename Bus>
  void send(Bus& bus) {
    for (uint16_t i = 0; i < NUM_LEDS; i++)
      bus.SetPixelColor(i, RgbColor(px[i] >> 16, px[i] >> 8, px[i]));
    bus.Show();
  }
  uint32_t px[NUM_LEDS];
  NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod>*     dmaBus = nullptr;
  NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod>* bbBus = nullptr;
};

static Strip strip;

// ---------- Fonte 3x5 ----------
// Cada linha (de cima para baixo) usa os bits mais baixos; o bit mais alto é a coluna da esquerda.
struct Glyph {
  char    c;
  uint8_t w;
  uint8_t rows[5];
};

static const Glyph FONT[] = {
  {'0', 3, {0b111, 0b101, 0b101, 0b101, 0b111}},
  {'1', 3, {0b010, 0b110, 0b010, 0b010, 0b111}},
  {'2', 3, {0b111, 0b001, 0b111, 0b100, 0b111}},
  {'3', 3, {0b111, 0b001, 0b111, 0b001, 0b111}},
  {'4', 3, {0b101, 0b101, 0b111, 0b001, 0b001}},
  {'5', 3, {0b111, 0b100, 0b111, 0b001, 0b111}},
  {'6', 3, {0b111, 0b100, 0b111, 0b101, 0b111}},
  {'7', 3, {0b111, 0b001, 0b001, 0b001, 0b001}},
  {'8', 3, {0b111, 0b101, 0b111, 0b101, 0b111}},
  {'9', 3, {0b111, 0b101, 0b111, 0b001, 0b111}},
  {' ', 3, {0, 0, 0, 0, 0}},
  {'-', 3, {0b000, 0b000, 0b111, 0b000, 0b000}},
  {'C', 3, {0b111, 0b100, 0b100, 0b100, 0b111}},
  {'F', 3, {0b111, 0b100, 0b111, 0b100, 0b100}},
  {'%', 3, {0b101, 0b001, 0b010, 0b100, 0b101}},
  {'A', 3, {0b010, 0b101, 0b111, 0b101, 0b101}},
  {'P', 3, {0b110, 0b101, 0b110, 0b100, 0b100}},
  {'*', 2, {0b11, 0b11, 0b00, 0b00, 0b00}},  // grau
  {'.', 1, {0, 0, 0, 0, 1}},
  {':', 1, {0, 1, 0, 1, 0}},
  {';', 1, {0, 0, 0, 0, 0}},                 // ":" apagado (mantém o espaço)
};

static const Glyph* findGlyph(char c) {
  for (const Glyph& g : FONT)
    if (g.c == c) return &g;
  return &FONT[10];  // espaço
}

// ---------- Framebuffer ----------
static bool fb[MATRIX_H][MATRIX_W];  // y = 0 é a linha de cima

static uint8_t textWidth(const char* s) {
  uint8_t w = 0, n = 0;
  for (; *s; s++, n++) w += findGlyph(*s)->w;
  return n ? w + n - 1 : 0;
}

static void drawText(const char* s, int x) {
  for (; *s; s++) {
    const Glyph* g = findGlyph(*s);
    for (uint8_t y = 0; y < 5; y++)
      for (uint8_t i = 0; i < g->w; i++)
        if ((g->rows[y] >> (g->w - 1 - i)) & 1) {
          int px = x + i;
          if (px >= 0 && px < MATRIX_W) fb[y][px] = true;
        }
    x += g->w + 1;
  }
}

static void drawCentered(const char* s) {
  int w = textWidth(s);
  drawText(s, w >= MATRIX_W ? 0 : (MATRIX_W - w) / 2);
}

// Posição do pixel (x da esquerda, y de cima) na fita
static uint8_t ledIndex(uint8_t x, uint8_t y) {
  uint8_t col = L.startLeft ? x : MATRIX_W - 1 - x;
  uint8_t row = L.startTop ? y : MATRIX_H - 1 - y;
  if (L.columns) {
    if (L.zigzag && (col & 1)) row = MATRIX_H - 1 - row;
    return col * MATRIX_H + row;
  }
  if (L.zigzag && (row & 1)) col = MATRIX_W - 1 - col;
  return row * MATRIX_W + col;
}

// Contorno da matriz (40 pixels), usado na animação de espera
static void perimeter(uint8_t k, uint8_t& x, uint8_t& y) {
  if (k < 17)      { x = k;      y = 0; }
  else if (k < 21) { x = 16;     y = k - 16; }
  else if (k < 37) { x = 36 - k; y = 4; }
  else             { x = 0;      y = 40 - k; }
}
static const uint8_t PERIMETER_LEN = 40;

// ---------- Estado ----------
enum Screen : uint8_t { SCR_CLOCK = 0, SCR_TEMP = 1, SCR_HUMI = 2, SCR_COUNT = 3 };
enum Phase : uint8_t { PH_SHOW, PH_OUT, PH_IN };

static uint8_t  screen = SCR_CLOCK;
static uint8_t  pendingScreen = SCR_CLOCK;
static Phase    phase = PH_SHOW;
static uint32_t screenStart = 0;
static uint32_t phaseStart = 0;
static bool     wipeReverse = false;
static bool     forceNext = false;

static uint32_t lastFrame = 0;
static uint32_t lastShow = 0;
static uint8_t  diagMode = Display::DIAG_OFF;
static uint8_t  diagArg = 0;
static uint32_t diagUntil = 0;
static float    curBright = -1;
static bool     night = false;
static uint32_t lastBuf[NUM_LEDS];
static const char* curName = "boot";

static const uint16_t FADE_MS = 300;
static const uint16_t WIPE_MS = 400;

// ---------- Helpers ----------

// ---------- Cor em 16 bits ----------
// As cores são calculadas com 16 bits por canal e só no fim reduzidas para os
// 8 bits da fita. Com DMA, a sobra é distribuída entre quadros (dithering
// temporal), o que dá degradês e brilho baixo sem degraus visíveis.

struct C16 {
  uint16_t r, g, b;
};

static uint16_t gammaLut[256];  // sRGB 8 bits -> intensidade linear 16 bits
static bool     dither = false;
static uint8_t  ditherErr[NUM_LEDS][3];

static void initGamma() {
  for (int i = 0; i < 256; i++) gammaLut[i] = lroundf(powf(i / 255.0f, 2.2f) * 65535.0f);
}

static C16 fromRgb(uint32_t c) {
  return {gammaLut[(c >> 16) & 0xFF], gammaLut[(c >> 8) & 0xFF], gammaLut[c & 0xFF]};
}

// Arco-íris balanceado (algoritmo "rainbow" do FastLED): r+g+b constante e
// faixa do amarelo alargada, para a passagem entre cores parecer uniforme.
static void rainbow8(uint8_t hue, uint8_t& r, uint8_t& g, uint8_t& b) {
  uint8_t offset8 = (hue & 0x1F) << 3;
  uint8_t third = (offset8 * 86) >> 8;       // 0..85
  uint8_t twoThirds = (offset8 * 171) >> 8;  // 0..170
  switch (hue >> 5) {
    case 0:  r = 255 - third;     g = third;            b = 0;               break;  // vermelho -> laranja
    case 1:  r = 171;             g = 85 + third;       b = 0;               break;  // laranja -> amarelo
    case 2:  r = 171 - twoThirds; g = 170 + third;      b = 0;               break;  // amarelo -> verde
    case 3:  r = 0;               g = 255 - third;      b = third;           break;  // verde -> água
    case 4:  r = 0;               g = 171 - twoThirds;  b = 85 + twoThirds;  break;  // água -> azul
    case 5:  r = third;           g = 0;                b = 255 - third;     break;  // azul -> roxo
    case 6:  r = 85 + third;      g = 0;                b = 171 - third;     break;  // roxo -> rosa
    default: r = 170 + third;     g = 0;                b = 85 - third;      break;  // rosa -> vermelho
  }
}

// Matiz de 16 bits: interpola entre os 256 passos para transição contínua
static C16 rainbow(uint16_t hue) {
  uint8_t r0, g0, b0, r1, g1, b1;
  rainbow8(hue >> 8, r0, g0, b0);
  rainbow8((hue >> 8) + 1, r1, g1, b1);
  uint16_t f = hue & 0xFF, nf = 256 - f;
  return {(uint16_t)(r0 * nf + r1 * f), (uint16_t)(g0 * nf + g1 * f), (uint16_t)(b0 * nf + b1 * f)};
}

static uint8_t quantize(uint32_t v, uint8_t& err) {  // v: 0..65535
  // abaixo de 1 nível o dithering viraria pisca-pisca: só arredonda
  if (!dither || v < 256) {
    err = 0;
    return min<uint32_t>((v + 128) >> 8, 255);
  }
  uint32_t t = v + err;
  err = t & 0xFF;
  return min<uint32_t>(t >> 8, 255);
}

// level: brilho * fade, 0..65025
static void put(uint16_t led, C16 c, uint32_t level) {
  if (led >= NUM_LEDS) return;
  uint32_t r = (uint32_t)c.r * level / 65025;
  uint32_t g = (uint32_t)c.g * level / 65025;
  uint32_t b = (uint32_t)c.b * level / 65025;
  uint8_t* e = ditherErr[led];
  uint8_t r8 = quantize(r, e[0]), g8 = quantize(g, e[1]), b8 = quantize(b, e[2]);
  // em brilho muito baixo mantém o LED aceso no canal dominante
  if (!(r8 | g8 | b8) && (r | g | b)) {
    if (r >= g && r >= b) r8 = 1;
    else if (g >= b) g8 = 1;
    else b8 = 1;
  }
  strip.setPixelColor(led, ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8);
}

static bool timeOk() { return Net::timeValid(); }

static bool screenAvailable(uint8_t s) {
  if (night && S.nightClockOnly) return s == SCR_CLOCK;
  switch (s) {
    case SCR_CLOCK: return S.tClock > 0;
    case SCR_TEMP:  return S.tTemp > 0 && Sensors::climateValid();
    case SCR_HUMI:  return S.tHumi > 0 && Sensors::climateValid();
  }
  return false;
}

static uint16_t screenDuration(uint8_t s) {
  switch (s) {
    case SCR_CLOCK: return S.tClock;
    case SCR_TEMP:  return S.tTemp;
    case SCR_HUMI:  return S.tHumi;
  }
  return 0;
}

static uint8_t nextAvailable(uint8_t from) {
  for (uint8_t i = 1; i <= SCR_COUNT; i++) {
    uint8_t c = (from + i) % SCR_COUNT;
    if (screenAvailable(c)) return c;
  }
  return SCR_CLOCK;  // nada habilitado: mostra o relógio
}

// ---------- Conteúdo das telas ----------

static void drawClock() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm tm;
  localtime_r(&tv.tv_sec, &tm);

  int h = tm.tm_hour;
  if (S.h12) {
    h %= 12;
    if (h == 0) h = 12;
  }
  bool colon = S.blinkColon ? tv.tv_usec < 500000 : true;
  char buf[8];
  snprintf(buf, sizeof(buf), "%c%d%c%02d",
           (h < 10 && !S.leadingZero) ? ' ' : '0' + h / 10, h % 10,
           colon ? ':' : ';', tm.tm_min);
  drawText(buf, 0);  // "12:34" ocupa exatamente as 17 colunas
}

static void drawTemp() {
  float t = Sensors::temp();
  if (S.fahrenheit) t = t * 9.0f / 5.0f + 32.0f;
  char buf[16];

  int v10 = lroundf(fabsf(t) * 10.0f);
  bool neg = t < 0 && v10 > 0;
  if (S.tempDecimal && v10 < (neg ? 100 : 1000)) {
    snprintf(buf, sizeof(buf), "%s%d.%d*", neg ? "-" : "", v10 / 10, v10 % 10);  // "23.5°"
  } else {
    snprintf(buf, sizeof(buf), "%ld*%c", lroundf(t), S.fahrenheit ? 'F' : 'C');  // "23°C"
    if (textWidth(buf) > MATRIX_W) buf[strlen(buf) - 1] = 0;                    // "-15°"
  }
  drawCentered(buf);
}

static void drawHumi() {
  float h = Sensors::humi();
  char buf[16];
  int v10 = lroundf(h * 10.0f);
  if (S.tempDecimal && v10 < 1000) snprintf(buf, sizeof(buf), "%d.%d%%", v10 / 10, v10 % 10);  // "65.4%"
  else snprintf(buf, sizeof(buf), "%ld%%", lroundf(h));                                         // "65%"
  drawCentered(buf);
}

static uint32_t screenColor(uint8_t s) {
  switch (s) {
    case SCR_TEMP: return S.cTemp;
    case SCR_HUMI: return S.cHumi;
    default:       return S.cClock;
  }
}

// ---------- Brilho e modo noturno ----------

static void updateNight() {
  switch (S.nightMode) {
    case NIGHT_LUX:
      if (!Sensors::luxValid()) { night = false; break; }
      // histerese para não ficar alternando no limiar
      if (night) night = Sensors::lux() <= S.nightLux * 1.5f + 0.5f;
      else       night = Sensors::lux() <= S.nightLux;
      break;
    case NIGHT_SCHEDULE: {
      if (!timeOk() || S.nightStart == S.nightEnd) { night = false; break; }
      time_t now = time(nullptr);
      struct tm tm;
      localtime_r(&now, &tm);
      uint8_t h = tm.tm_hour;
      night = S.nightStart < S.nightEnd ? (h >= S.nightStart && h < S.nightEnd)
                                        : (h >= S.nightStart || h < S.nightEnd);
      break;
    }
    default:
      night = false;
  }
}

static float targetBrightness() {
  if (night) return S.nightBright;
  if (S.autoBright && Sensors::luxValid()) {
    float l = Sensors::lux();
    if (l <= S.luxLow) return S.bMin;
    if (l >= S.luxHigh) return S.bMax;
    // escala logarítmica: o olho percebe luz de forma logarítmica
    float lo = log10f(S.luxLow + 1.0f), hi = log10f(S.luxHigh + 1.0f);
    float frac = (log10f(l + 1.0f) - lo) / (hi - lo);
    return S.bMin + frac * (S.bMax - S.bMin);
  }
  return S.bManual;
}

static void updateBrightness() {
  float target = targetBrightness();
  if (curBright < 0) curBright = target;
  else curBright += (target - curBright) * 0.08f;
  if (fabsf(target - curBright) < 0.5f) curBright = target;
}

// ---------- Sequência de telas ----------

static void startTransition(uint8_t next) {
  if (S.transition == TRANS_NONE) {
    screen = next;
    screenStart = millis();
    return;
  }
  pendingScreen = next;
  phase = PH_OUT;
  phaseStart = millis();
  wipeReverse = !wipeReverse;
}

static void updateSequence(uint32_t now) {
  uint16_t phaseMs = S.transition == TRANS_WIPE ? WIPE_MS : FADE_MS;

  if (phase == PH_OUT && now - phaseStart >= phaseMs) {
    screen = pendingScreen;
    phase = PH_IN;
    phaseStart = now;
  } else if (phase == PH_IN && now - phaseStart >= phaseMs) {
    phase = PH_SHOW;
    screenStart = now;
  }
  if (phase != PH_SHOW) return;

  uint16_t dur = screenDuration(screen);
  bool expired = dur > 0 && now - screenStart >= dur * 1000UL;
  if (forceNext || expired || !screenAvailable(screen)) {
    forceNext = false;
    uint8_t nxt = nextAvailable(screen);
    if (nxt != screen) startTransition(nxt);
    else screenStart = now;
  }
}

// ---------- Renderização ----------

static void commit(uint32_t now) {
  // Envia só quando a imagem muda (cada envio no GPIO0 é uma chance de glitch)
  // e reenvia a cada 1s para corrigir um quadro que tenha chegado corrompido.
  const uint32_t* px = strip.getPixels();
  if (memcmp(px, lastBuf, sizeof(lastBuf)) != 0 || now - lastShow >= 1000) {
    memcpy(lastBuf, px, sizeof(lastBuf));
    strip.show();
    lastShow = now;
  }
}

static uint16_t hueNow(uint32_t now) {
  // velocidade 1 = 60s por volta, 10 = 6s
  return (uint16_t)((uint64_t)now * S.rainbowSpeed * 65536ULL / 60000ULL);
}

// Desenha o framebuffer com uma cor fixa
static void blit(uint32_t color, uint32_t level) {
  strip.clear();
  C16 c = fromRgb(color);
  for (uint8_t y = 0; y < MATRIX_H; y++)
    for (uint8_t x = 0; x < MATRIX_W; x++)
      if (fb[y][x]) put(ledIndex(x, y), c, level);
}

static void renderDiag(uint32_t now) {
  const uint32_t level = 100U * 255U;
  memset(fb, 0, sizeof(fb));
  strip.clear();
  switch (diagMode) {
    case Display::DIAG_ALL: {
      // degradê da esquerda (vermelho) para a direita (azul)
      for (uint8_t y = 0; y < MATRIX_H; y++)
        for (uint8_t x = 0; x < MATRIX_W; x++)
          put(ledIndex(x, y), rainbow(x * 3855), level);
      break;
    }
    case Display::DIAG_LED:
      put(diagArg, fromRgb(0xFFFFFF), level);
      break;
    case Display::DIAG_ORIENT:
      drawText("12:34", 0);
      fb[0][0] = true;  // canto superior esquerdo
      blit(0xFFFFFF, level);
      put(ledIndex(0, 0), fromRgb(0xFF0000), level);
      break;
  }
  curName = "test";
}

static void renderWaiting(uint32_t now) {
  memset(fb, 0, sizeof(fb));
  uint32_t level = max(curBright, 20.0f) * 255;
  if (Net::apActive()) {
    // "AP": aguardando configuração pelo portal
    drawCentered("AP");
    blit(0xFF8000, level);
    curName = "ap";
    return;
  }
  // cometa girando no contorno: vermelho = sem Wi-Fi, azul = aguardando NTP
  strip.clear();
  C16 color = fromRgb(Net::connected() ? 0x0040FF : 0xFF0000);
  uint8_t head = (now / 40) % PERIMETER_LEN;
  for (uint8_t t = 0; t < 6; t++) {
    uint8_t x, y;
    perimeter((head + PERIMETER_LEN - t) % PERIMETER_LEN, x, y);
    put(ledIndex(x, y), color, level >> t);
  }
  curName = "wait";
}

static void renderScreen(uint32_t now) {
  memset(fb, 0, sizeof(fb));
  switch (screen) {
    case SCR_TEMP: drawTemp(); break;
    case SCR_HUMI: drawHumi(); break;
    default:       drawClock();
  }

  uint16_t hue = hueNow(now);
  uint16_t phaseMs = S.transition == TRANS_WIPE ? WIPE_MS : FADE_MS;
  float prog = phase == PH_SHOW ? 1.0f : min(1.0f, (now - phaseStart) / (float)phaseMs);

  uint16_t fade = 255;
  if (S.transition == TRANS_FADE) {
    if (phase == PH_OUT) fade = 255 * (1.0f - prog);
    else if (phase == PH_IN) fade = 255 * prog;
  }
  uint8_t wipeN = prog * MATRIX_W;
  uint32_t level = curBright * fade + 0.5f;

  strip.clear();
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      if (!fb[y][x]) continue;
      if (S.transition == TRANS_WIPE && phase != PH_SHOW) {
        uint8_t ord = wipeReverse ? MATRIX_W - 1 - x : x;  // varredura por coluna
        if (phase == PH_OUT ? ord < wipeN : ord >= wipeN) continue;
      }
      C16 c;
      if (night) c = fromRgb(S.nightColor);
      else if (S.colorMode == COLOR_RAINBOW) c = rainbow(hue);
      else if (S.colorMode == COLOR_RAINBOW_DIGITS) c = rainbow(hue + x * 3000);
      else c = fromRgb(screenColor(screen));
      put(ledIndex(x, y), c, level);
    }
  }

  static const char* const NAMES[] = {"clock", "temp", "humi"};
  curName = NAMES[screen];
}

// ---------- API ----------

void Display::begin() {
  initGamma();
  dither = L.dma;  // no GPIO0 cada envio extra é uma chance de glitch
  strip.begin(L.dma);
  strip.show();
  memset(lastBuf, 0, sizeof(lastBuf));
}

void Display::loop() {
  uint32_t now = millis();
  if (now - lastFrame < (dither ? 10U : 20U)) return;  // 100 quadros/s com DMA, 50 sem
  lastFrame = now;

  updateNight();
  updateBrightness();

  if (diagMode != DIAG_OFF && (int32_t)(diagUntil - now) <= 0) diagMode = DIAG_OFF;

  if (diagMode != DIAG_OFF) {
    renderDiag(now);
  } else if (!timeOk()) {
    renderWaiting(now);
  } else {
    updateSequence(now);
    renderScreen(now);
  }
  commit(now);
}

void Display::showProgress(uint8_t pct) {
  static uint8_t last = 255;
  if (pct == last) return;
  last = pct;
  memset(fb, 0, sizeof(fb));
  char buf[6];
  snprintf(buf, sizeof(buf), "%d%%", min(pct, (uint8_t)99));
  drawCentered(buf);
  blit(0x00FF40, max(curBright, 30.0f) * 255);
  strip.show();
}

void Display::test(uint16_t ms) { diag(DIAG_ALL, 0, ms); }

void Display::diag(uint8_t mode, uint8_t arg, uint32_t ms) {
  diagMode = mode;
  diagArg = arg;
  diagUntil = millis() + ms;
}

void Display::nextScreen() { forceNext = true; }
uint8_t Display::brightness() { return curBright < 0 ? 0 : (uint8_t)(curBright + 0.5f); }
bool Display::nightActive() { return night; }
const char* Display::screenName() { return curName; }
