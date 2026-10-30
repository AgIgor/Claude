#include "MirrorSwitch.h"

#include <Preferences.h>

namespace mirsw {

// Valores em contagens do ADC de 12 bits (0..4095). O ADC do ESP32 e mais ruidoso
// que o do Arduino, por isso as janelas sao ~4x maiores que na versao de 10 bits.
static constexpr uint16_t kMinSeparation = 100;   // distancia minima entre niveis na calibracao
static constexpr uint16_t kMaxTolerance  = 240;   // janela maxima de aceitacao de cada nivel
static constexpr uint16_t kWarnTolerance = 60;    // abaixo disso, avisa para trocar o pull-up
static constexpr uint16_t kStableWindow  = 40;    // variacao admitida enquanto "parado"
static constexpr uint32_t kStableMs      = 1500;  // tempo parado para capturar um nivel
static constexpr uint32_t kJoyDebounceMs = 40;
static constexpr uint32_t kSelDebounceMs = 150;
static constexpr uint8_t  kCalVersion    = 1;

const char* const kJoyName[JOY_COUNT] = {"NEUTRO", "CIMA", "BAIXO", "ESQUERDA", "DIREITA"};
const char* const kSelName[SEL_COUNT] = {"ESPELHO ESQ", "CENTRO", "ESPELHO DIR", "REBATER"};

struct Calibration {
  uint8_t version;
  uint16_t joy[JOY_COUNT];
  uint16_t sel[SEL_COUNT];
};

struct Debounce {
  int8_t stable = UNKNOWN, candidate = UNKNOWN;
  uint32_t since = 0;
  bool update(int8_t v, uint32_t ms) {  // true quando o estado estavel muda
    uint32_t now = millis();
    if (v != candidate) { candidate = v; since = now; return false; }
    if (candidate != stable && now - since >= ms) { stable = candidate; return true; }
    return false;
  }
};

static Preferences prefs;
static int8_t joyPin_ = -1, selPin_ = -1;
static Calibration cal;
static bool valid = false, monitorOn = false;
static uint16_t tolJoy[JOY_COUNT], tolSel[SEL_COUNT];
static Debounce dbJoy, dbSel;
static bool selEvent = false;
static int8_t selFrom = UNKNOWN;

// ---------------------------------------------------------------------------
static uint16_t absDiff(uint16_t a, uint16_t b) { return a > b ? a - b : b - a; }

static uint16_t readAvg(int8_t pin, uint8_t n = 32) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) sum += analogRead(pin);
  return sum / n;
}

// Tolerancia de cada nivel = metade da distancia ate o vizinho mais proximo
static void computeTolerances(const uint16_t* levels, uint16_t* tol, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    uint16_t nearest = 0xFFFF;
    for (uint8_t k = 0; k < n; k++)
      if (k != i) nearest = min(nearest, absDiff(levels[i], levels[k]));
    tol[i] = min<uint16_t>(nearest / 2, kMaxTolerance);
  }
}

// Indice do nivel mais proximo, ou UNKNOWN se fora de todas as janelas
static int8_t classify(uint16_t v, const uint16_t* levels, const uint16_t* tol, uint8_t n) {
  int8_t best = UNKNOWN;
  uint16_t bestDiff = 0xFFFF;
  for (uint8_t i = 0; i < n; i++) {
    uint16_t d = absDiff(v, levels[i]);
    if (d <= tol[i] && d < bestDiff) { best = i; bestDiff = d; }
  }
  return best;
}

static bool farFromAll(uint16_t v, const uint16_t* list, uint8_t n) {
  for (uint8_t i = 0; i < n; i++)
    if (absDiff(v, list[i]) < kMinSeparation) return false;
  return true;
}

// ---------------------------------------------------------------------------
bool begin(int8_t joyPin, int8_t selPin) {
  joyPin_ = joyPin;
  selPin_ = selPin;
  if (!enabled()) return false;
  analogReadResolution(12);
  analogSetPinAttenuation(joyPin, ADC_11db);  // faixa ~0..3,1 V
  analogSetPinAttenuation(selPin, ADC_11db);

  prefs.begin("mirsw", false);
  valid = prefs.getBytes("cal", &cal, sizeof(cal)) == sizeof(cal) && cal.version == kCalVersion;
  if (valid) {
    computeTolerances(cal.joy, tolJoy, JOY_COUNT);
    computeTolerances(cal.sel, tolSel, SEL_COUNT);
  }
  return valid;
}

bool enabled() { return joyPin_ >= 0 && selPin_ >= 0; }
bool calibrated() { return valid; }
bool monitor() { return monitorOn; }
void setMonitor(bool on) { monitorOn = on; }

int8_t joy() { return valid && dbJoy.stable >= 0 ? dbJoy.stable : JOY_NEUTRAL; }
int8_t sel() { return valid ? dbSel.stable : UNKNOWN; }

bool selChanged(int8_t& from) {
  if (!selEvent) return false;
  selEvent = false;
  from = selFrom;
  return true;
}

void update() {
  if (!enabled()) return;
  uint16_t rawJoy = readAvg(joyPin_), rawSel = readAvg(selPin_);
  int8_t j = UNKNOWN, s = UNKNOWN;

  if (valid) {
    j = classify(rawJoy, cal.joy, tolJoy, JOY_COUNT);
    s = classify(rawSel, cal.sel, tolSel, SEL_COUNT);
    // Joystick fora de qualquer nivel conhecido = parar (seguro)
    dbJoy.update(j < 0 ? JOY_NEUTRAL : j, kJoyDebounceMs);
    // Seletor fora de nivel (transicao entre posicoes) mantem o ultimo estado valido
    int8_t prev = dbSel.stable;
    if (s != UNKNOWN && dbSel.update(s, kSelDebounceMs)) {
      selEvent = true;
      selFrom = prev;
    }
  }

  static uint32_t lastPrint = 0;
  if (monitorOn && millis() - lastPrint >= 200) {
    lastPrint = millis();
    Serial.printf("JOY %4u -> %-8s | SEL %4u -> %s\n", rawJoy, j >= 0 ? kJoyName[j] : "???",
                  rawSel, s >= 0 ? kSelName[s] : "???");
  }
}

// ---------------------------------------------------------------------------
// Calibracao
// ---------------------------------------------------------------------------
static int8_t led_ = -1;
static void (*idle_)() = nullptr;

// Pisca 'code' vezes e pausa: indica o passo da calibracao sem PC
static void blinkCode(uint8_t code) {
  uint32_t period = code * 400UL + 1000;
  uint32_t t = millis() % period;
  digitalWrite(led_, t < code * 400UL && (t % 400) < 200);
}

static void waitMs(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (idle_) idle_();
    delay(1);
  }
}

static void flashFast(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(led_, HIGH); waitMs(60);
    digitalWrite(led_, LOW);  waitMs(60);
  }
}

// Um passo do laco de espera. Retorna true se o usuario pediu para cancelar.
static bool tick(uint8_t code, const char* state) {
  if (idle_) idle_();
  blinkCode(code);
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 300) {
    lastPrint = millis();
    Serial.printf("    joy=%4u  sel=%4u  %s\n", readAvg(joyPin_), readAvg(selPin_), state);
  }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'x' || c == 'X') return true;
  }
  delay(1);
  return false;
}

// Espera a leitura ficar parada por kStableMs e diferente dos niveis ja capturados
static bool captureStable(int8_t pin, const uint16_t* exclude, uint8_t nExclude, uint8_t code,
                          uint16_t& out) {
  uint16_t ref = readAvg(pin);
  uint32_t since = millis();
  for (;;) {
    uint16_t v = readAvg(pin);
    const char* state;
    if (!farFromAll(v, exclude, nExclude)) {
      ref = v;
      since = millis();
      state = "igual a nivel ja gravado (aguardando mudar)";
    } else if (absDiff(v, ref) > kStableWindow) {
      ref = v;
      since = millis();
      state = "instavel";
    } else if (millis() - since >= kStableMs) {
      out = readAvg(pin, 128);
      return true;
    } else {
      state = "estavel, capturando...";
    }
    if (tick(code, state)) return false;
  }
}

static bool cancelled() {
  Serial.println(">> Calibracao cancelada. Mantida a calibracao anterior.");
  return false;
}

bool calibrate(int8_t ledPin, void (*idle)()) {
  if (!enabled()) {
    Serial.println("! Botao de espelhos desativado (sem portas dianteiras)");
    return false;
  }
  led_ = ledPin;
  idle_ = idle;
  Calibration c = {};  // so substitui a calibracao atual no final
  uint16_t tol[JOY_COUNT];

  Serial.println("\n===== CALIBRACAO DO BOTAO DE ESPELHOS =====");
  Serial.println("Envie 'x' para cancelar. O LED pisca o numero do passo.");

  // --- Joystick: captura automatica quando a leitura estabiliza ---
  Serial.println("[1] Solte o joystick (NEUTRO) e aguarde...");
  if (!captureStable(joyPin_, nullptr, 0, 1, c.joy[JOY_NEUTRAL])) return cancelled();
  Serial.printf("    NEUTRO = %u\n", c.joy[JOY_NEUTRAL]);
  flashFast(3);

  for (uint8_t j = JOY_UP; j < JOY_COUNT; j++) {
    Serial.printf("[%u] Segure o joystick para %s ate o LED piscar rapido...\n", j + 1,
                  kJoyName[j]);
    if (!captureStable(joyPin_, c.joy, j, j + 1, c.joy[j])) return cancelled();
    Serial.printf("    %s = %u\n", kJoyName[j], c.joy[j]);
    flashFast(3);

    Serial.println("    Solte o joystick.");
    while (absDiff(readAvg(joyPin_), c.joy[JOY_NEUTRAL]) > kMinSeparation / 2)
      if (tick(j + 1, "aguardando soltar")) return cancelled();
    waitMs(300);
  }
  computeTolerances(c.joy, tol, JOY_COUNT);

  // --- Seletor: as posicoes travam, entao confirma com um toque no joystick ---
  for (uint8_t s = 0; s < SEL_COUNT; s++) {
    uint8_t step = JOY_COUNT + s + 1;
    Serial.printf("[%u] Gire o seletor para %s e toque o joystick em qualquer direcao.\n", step,
                  kSelName[s]);
    for (;;) {
      uint32_t since = millis();
      while (millis() - since < 200) {  // toque firme
        if (tick(step, "aguardando toque no joystick")) return cancelled();
        int8_t j = classify(readAvg(joyPin_), c.joy, tol, JOY_COUNT);
        if (j <= JOY_NEUTRAL) since = millis();
      }
      uint16_t v = readAvg(selPin_, 128);
      since = millis();
      while (millis() - since < 300) {  // espera soltar
        if (tick(step, "solte o joystick")) return cancelled();
        if (classify(readAvg(joyPin_), c.joy, tol, JOY_COUNT) != JOY_NEUTRAL) since = millis();
      }
      if (farFromAll(v, c.sel, s)) { c.sel[s] = v; break; }
      Serial.printf("    Leitura %u igual a uma posicao ja gravada. Confira e tente de novo.\n", v);
      for (uint8_t k = 0; k < 3; k++) {
        digitalWrite(led_, HIGH); waitMs(500);
        digitalWrite(led_, LOW);  waitMs(200);
      }
    }
    Serial.printf("    %s = %u\n", kSelName[s], c.sel[s]);
    flashFast(3);
  }

  c.version = kCalVersion;
  cal = c;
  prefs.putBytes("cal", &cal, sizeof(cal));
  valid = true;
  computeTolerances(cal.joy, tolJoy, JOY_COUNT);
  computeTolerances(cal.sel, tolSel, SEL_COUNT);
  dbJoy = Debounce();
  dbSel = Debounce();
  Serial.println(">> Calibracao salva na flash.");
  printCalibration();
  flashFast(15);
  return true;
}

void printCalibration() {
  if (!valid) {
    Serial.println("Botao de espelhos: SEM CALIBRACAO (envie 'calibrar')");
    return;
  }
  Serial.println("--- Botao de espelhos (valor ADC +/- tolerancia) ---");
  bool warnJoy = false, warnSel = false;
  for (uint8_t i = 0; i < JOY_COUNT; i++) {
    Serial.printf("  JOY %-11s %4u +/- %u\n", kJoyName[i], cal.joy[i], tolJoy[i]);
    warnJoy |= tolJoy[i] < kWarnTolerance;
  }
  for (uint8_t i = 0; i < SEL_COUNT; i++) {
    Serial.printf("  SEL %-11s %4u +/- %u\n", kSelName[i], cal.sel[i], tolSel[i]);
    warnSel |= tolSel[i] < kWarnTolerance;
  }
  if (warnJoy) Serial.println("  AVISO: niveis do joystick muito proximos; ajuste o pull-up.");
  if (warnSel) Serial.println("  AVISO: niveis do seletor muito proximos; ajuste o pull-up.");
}

}  // namespace mirsw
