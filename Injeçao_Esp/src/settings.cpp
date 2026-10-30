#include "settings.h"
#include <Preferences.h>

#define SETTINGS_MAGIC 0xEC0E5032

Settings cfg;
SemaphoreHandle_t cfgMutex;
volatile bool cfgDirty = false;

static void setCurve(Curve &c, const float *x, const float *y) {
  memcpy(c.x, x, sizeof(c.x));
  memcpy(c.y, y, sizeof(c.y));
}

static const float RPM_AXIS[TBL_N] = {500, 800, 1100, 1400, 1700, 2000, 2500, 3000,
                                      3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000};
static const float MAP_AXIS[TBL_N] = {20, 25, 30, 35, 40, 45, 50, 55,
                                      60, 65, 70, 75, 80, 85, 90, 100};

void settingsDefaults(Settings &s) {
  memset(&s, 0, sizeof(s));
  s.magic = SETTINGS_MAGIC;
  s.size = sizeof(Settings);
#define SF_DEF(n, d) s.n = d;
  SETTINGS_FIELDS(SF_DEF, SF_DEF)
#undef SF_DEF
  strcpy(s.wifiSsid, "ECU-ESP32");
  strcpy(s.wifiPass, "ecu12345");

  const float cltX[CURVE_N] = {-40, -20, 0, 10, 20, 40, 60, 70, 80, 100};
  const float warmY[CURVE_N] = {180, 160, 140, 130, 125, 115, 108, 104, 100, 100};
  const float crankY[CURVE_N] = {250, 220, 190, 170, 150, 130, 115, 110, 100, 100};
  const float advY[CURVE_N] = {8, 6, 5, 4, 3, 2, 1, 0, 0, -2};
  const float vbX[CURVE_N] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
  const float deadY[CURVE_N] = {2.0, 1.6, 1.35, 1.15, 1.0, 0.9, 0.8, 0.72, 0.66, 0.6};
  const float dwellY[CURVE_N] = {5.0, 4.5, 4.0, 3.6, 3.3, 3.0, 2.7, 2.5, 2.4, 2.3};
  setCurve(s.warmup, cltX, warmY);
  setCurve(s.crankEnrich, cltX, crankY);
  setCurve(s.cltAdvance, cltX, advY);
  setCurve(s.deadTime, vbX, deadY);
  setCurve(s.dwell, vbX, dwellY);
  const float idleRpmY[CURVE_N] = {1400, 1300, 1200, 1100, 1050, 1000, 950, 900, 850, 850};
  const float idlePosY[CURVE_N] = {55, 50, 45, 40, 37, 32, 28, 26, 25, 25};
  setCurve(s.idleTarget, cltX, idleRpmY);
  setCurve(s.idleBase, cltX, idlePosY);
  const float knkX[CURVE_N] = {1000, 1500, 2000, 2500, 3000, 3500, 4000, 5000, 6000, 7000};
  const float knkY[CURVE_N] = {600, 600, 650, 700, 750, 800, 850, 900, 950, 1000};
  setCurve(s.knockThreshold, knkX, knkY);

  Table3D *tbls[3] = {&s.ve, &s.ign, &s.lambda};
  for (Table3D *t : tbls) {
    memcpy(t->x, RPM_AXIS, sizeof(t->x));
    memcpy(t->y, MAP_AXIS, sizeof(t->y));
  }
  // Valores iniciais genericos e conservadores -- PRECISAM ser acertados.
  for (int r = 0; r < TBL_N; r++) {
    for (int c = 0; c < TBL_N; c++) {
      float rpm = RPM_AXIS[c], map = MAP_AXIS[r];
      float ve = 50 + 30 * min(rpm, 4500.0f) / 4500 + 10 * map / 100;
      s.ve.z[r][c] = roundf(constrain(ve, 40, 95));
      float adv = 6 + 24 * min(rpm, 4500.0f) / 4500 + (100 - map) * 0.08f;
      s.ign.z[r][c] = roundf(constrain(adv, 5, 32));
      s.lambda.z[r][c] = map >= 90 ? 0.88f : (map >= 75 ? 0.95f : 1.0f);
    }
  }
}

void settingsInit() {
  cfgMutex = xSemaphoreCreateMutex();
  Preferences p;
  p.begin("ecu", true);
  size_t len = p.getBytesLength("cfg");
  bool ok = false;
  if (len == sizeof(Settings)) {
    p.getBytes("cfg", &cfg, sizeof(Settings));
    ok = cfg.magic == SETTINGS_MAGIC && cfg.size == sizeof(Settings);
  }
  p.end();
  if (!ok) {
    Serial.println("Config invalida ou versao nova: carregando padroes");
    settingsDefaults(cfg);
  }
}

// So deve ser chamado com o motor parado: gravar na flash desliga o cache e
// atrasa as interrupcoes de tempo real.
bool settingsSave() {
  Preferences p;
  if (!p.begin("ecu", false)) return false;
  xSemaphoreTake(cfgMutex, portMAX_DELAY);
  size_t n = p.putBytes("cfg", &cfg, sizeof(Settings));
  xSemaphoreGive(cfgMutex);
  p.end();
  if (n == sizeof(Settings)) cfgDirty = false;
  return n == sizeof(Settings);
}

// ---------------------------------------------------------------- JSON

static void arrToJson(JsonArray a, const float *v, int n) {
  for (int i = 0; i < n; i++) a.add(roundf(v[i] * 1000) / 1000);
}

static void arrFromJson(JsonVariantConst v, float *dst, int n) {
  JsonArrayConst a = v.as<JsonArrayConst>();
  if (a.isNull() || (int)a.size() != n) return;
  for (int i = 0; i < n; i++) dst[i] = a[i].as<float>();
}

static void curveToJson(JsonObject o, const Curve &c) {
  arrToJson(o["x"].to<JsonArray>(), c.x, CURVE_N);
  arrToJson(o["y"].to<JsonArray>(), c.y, CURVE_N);
}

static void curveFromJson(JsonVariantConst v, Curve &c) {
  if (v.isNull()) return;
  arrFromJson(v["x"], c.x, CURVE_N);
  arrFromJson(v["y"], c.y, CURVE_N);
}

static void tableToJson(JsonObject o, const Table3D &t) {
  arrToJson(o["x"].to<JsonArray>(), t.x, TBL_N);
  arrToJson(o["y"].to<JsonArray>(), t.y, TBL_N);
  arrToJson(o["z"].to<JsonArray>(), &t.z[0][0], TBL_N * TBL_N);
}

static void tableFromJson(JsonVariantConst v, Table3D &t) {
  if (v.isNull()) return;
  arrFromJson(v["x"], t.x, TBL_N);
  arrFromJson(v["y"], t.y, TBL_N);
  arrFromJson(v["z"], &t.z[0][0], TBL_N * TBL_N);
}

#define CURVES(X) \
  X(warmup) X(crankEnrich) X(deadTime) X(dwell) X(cltAdvance) X(idleTarget) X(idleBase) X(knockThreshold)
#define TABLES(X) X(ve) X(ign) X(lambda)

void settingsToJson(const Settings &s, JsonObject o) {
#define SF_OUT(n, d) o[#n] = s.n;
  SETTINGS_FIELDS(SF_OUT, SF_OUT)
#undef SF_OUT
  o["wifiSsid"] = s.wifiSsid;
  o["wifiPass"] = s.wifiPass;
#define C_OUT(n) curveToJson(o[#n].to<JsonObject>(), s.n);
  CURVES(C_OUT)
#undef C_OUT
#define T_OUT(n) tableToJson(o[#n].to<JsonObject>(), s.n);
  TABLES(T_OUT)
#undef T_OUT
}

void settingsFromJson(Settings &s, JsonObjectConst o) {
#define SF_IN(n, d) \
  if (!o[#n].isNull()) s.n = o[#n].as<decltype(s.n)>();
  SETTINGS_FIELDS(SF_IN, SF_IN)
#undef SF_IN
  if (o["wifiSsid"].is<const char *>()) strlcpy(s.wifiSsid, o["wifiSsid"], sizeof(s.wifiSsid));
  if (o["wifiPass"].is<const char *>()) strlcpy(s.wifiPass, o["wifiPass"], sizeof(s.wifiPass));
#define C_IN(n) curveFromJson(o[#n], s.n);
  CURVES(C_IN)
#undef C_IN
#define T_IN(n) tableFromJson(o[#n], s.n);
  TABLES(T_IN)
#undef T_IN

  // Limites de sanidade
  if (s.teeth < 4) s.teeth = 4;
  if (s.missingTeeth < 1) s.missingTeeth = 1;
  if (s.missingTeeth > 3) s.missingTeeth = 3;
  s.tdcOffset = fmodf(fmodf(s.tdcOffset, 720) + 720, 720);
  if (s.maxDwellMs > 10) s.maxDwellMs = 10;
  if (s.maxDutyPct > 95) s.maxDutyPct = 95;
  if (s.camCountA == s.camCountB) s.camCountB = s.camCountA ? 0 : 1;  // precisam diferir
  if (s.knockGainIdx > 63) s.knockGainIdx = 63;
  if (s.knockIntegIdx > 31) s.knockIntegIdx = 31;
  if (s.idleType > 2) s.idleType = 0;
}

// ---------------------------------------------------------------- interpolacao

float curveLookup(const Curve &c, float x) {
  if (x <= c.x[0]) return c.y[0];
  for (int i = 1; i < CURVE_N; i++) {
    if (x < c.x[i]) {
      float dx = c.x[i] - c.x[i - 1];
      if (dx <= 0) return c.y[i];
      return c.y[i - 1] + (c.y[i] - c.y[i - 1]) * (x - c.x[i - 1]) / dx;
    }
  }
  return c.y[CURVE_N - 1];
}

// Indice e fracao no eixo (com saturacao nas pontas)
static void axisPos(const float *a, float v, int &i, float &f) {
  if (v <= a[0]) { i = 0; f = 0; return; }
  if (v >= a[TBL_N - 1]) { i = TBL_N - 2; f = 1; return; }
  for (i = 0; i < TBL_N - 2 && v >= a[i + 1]; i++) {}
  float d = a[i + 1] - a[i];
  f = d > 0 ? (v - a[i]) / d : 0;
}

float tableLookup(const Table3D &t, float rpm, float map) {
  int c, r;
  float fc, fr;
  axisPos(t.x, rpm, c, fc);
  axisPos(t.y, map, r, fr);
  float a = t.z[r][c] + (t.z[r][c + 1] - t.z[r][c]) * fc;
  float b = t.z[r + 1][c] + (t.z[r + 1][c + 1] - t.z[r + 1][c]) * fc;
  return a + (b - a) * fr;
}
