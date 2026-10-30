// Simulador de roda fonica 60-2 + sensor de fase para testes de bancada.
// Roda em um segundo ESP32 (env:simulador) ou Arduino Uno (env:simulador_uno).
//
// Comandos pela serial (115200):
//   r3000   -> 3000 RPM
//   r0      -> para
//   c0 / c1 -> desliga / liga o sinal de fase
//   j1      -> liga variacao de rotacao tipo partida (compressao)
//   a3 / b1 -> pulsos de fase na volta A / volta B (padrao 1 e 0)
#include <Arduino.h>

#if defined(ESP32)
#define OUT_CKP 25
#define OUT_CMP 26
#else
#define OUT_CKP 2
#define OUT_CMP 3
#endif

#define TEETH 60
#define MISSING 2
// Dentes onde podem aparecer pulsos de fase (cada um com 2 dentes de largura)
static const uint8_t CAM_TEETH[4] = {10, 22, 34, 46};
static uint8_t camA = 1, camB = 0;

static uint32_t rpm = 0;
static bool camOn = true;
static bool jitter = false;
static uint16_t pos = 0;  // meio-dente atual, 0..(TEETH*4-1) = 720 graus
static uint32_t nextUs = 0;

static uint32_t halfToothUs() {
  // 1 volta = 60e6/rpm us; meio dente = volta / (TEETH*2)
  uint32_t h = 60000000UL / rpm / (TEETH * 2);
  if (jitter) {
    // simula aceleracao/desaceleracao a cada compressao (2 por volta)
    uint16_t tooth = (pos / 2) % TEETH;
    int16_t phase = (tooth * 2) % TEETH;  // 0..59 dentro de meia volta
    int32_t var = (phase < TEETH / 2) ? 15 : -15;
    h = h + h * var / 100;
  }
  return h;
}

void setup() {
  Serial.begin(115200);
  pinMode(OUT_CKP, OUTPUT);
  pinMode(OUT_CMP, OUTPUT);
  Serial.println(F("Simulador 60-2. Comandos: r<rpm> c0/c1 j0/j1 a<n> b<n>"));
}

static void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  long v = Serial.parseInt();
  if (c == 'r') {
    rpm = constrain(v, 0, 9000);
    nextUs = micros();
  } else if (c == 'c') {
    camOn = v;
  } else if (c == 'j') {
    jitter = v;
  } else if (c == 'a') {
    camA = constrain(v, 0, 4);
  } else if (c == 'b') {
    camB = constrain(v, 0, 4);
  } else {
    return;
  }
  Serial.print(F("rpm="));
  Serial.print(rpm);
  Serial.print(F(" fase="));
  Serial.print(camOn);
  Serial.print(F(" partida="));
  Serial.print(jitter);
  Serial.print(F(" pulsos fase A/B="));
  Serial.print(camA);
  Serial.print('/');
  Serial.println(camB);
}

void loop() {
  handleSerial();
  if (rpm == 0) {
    digitalWrite(OUT_CKP, LOW);
    return;
  }
  if ((int32_t)(micros() - nextUs) < 0) return;
  nextUs += halfToothUs();

  uint16_t tooth = (pos / 2) % TEETH;  // 0..59 na volta atual
  bool rev1 = pos >= TEETH * 2;        // segunda volta do ciclo
  bool high = (pos % 2 == 0) && tooth < TEETH - MISSING;
  digitalWrite(OUT_CKP, high);
  // Pulsos de fase: camA na primeira volta do ciclo, camB na segunda
  bool cam = false;
  uint8_t n = rev1 ? camB : camA;
  for (uint8_t i = 0; i < n; i++)
    if (tooth >= CAM_TEETH[i] && tooth < (uint16_t)(CAM_TEETH[i] + 2)) cam = true;
  cam = cam && camOn;
  digitalWrite(OUT_CMP, cam);

  pos = (pos + 1) % (TEETH * 4);
}
