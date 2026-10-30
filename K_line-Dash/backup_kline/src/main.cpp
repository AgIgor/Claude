// Dash web K-line para Ford Fiesta 2004 (ECU Magneti Marelli IAW 4AFR.BR)
// ESP32 + transceptor K-line (L9637D/MC33290) -> Wi-Fi AP -> dashboard no celular.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <stdarg.h>
#include "config.h"
#include "kline.h"
#include "obd.h"
#include "log.h"
#include "web_index.h"

#define MAX_PIDS 16

static KLine kl(Serial2, KLINE_RX_PIN, KLINE_TX_PIN);
static WebServer server(80);
static Preferences prefs;

// ---------------- Log ----------------
static SemaphoreHandle_t logMx;
static const int LOG_N = 80;
static String logBuf[LOG_N];
static int logHead = 0, logCount = 0;

static String jsonEsc(const String& in) {
  String o;
  o.reserve(in.length() + 4);
  for (char c : in) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

void logLine(const char* fmt, ...) {
  char tmp[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  char line[224];
  snprintf(line, sizeof(line), "[%7.1f] %s", millis() / 1000.0, tmp);
  Serial.println(line);
  xSemaphoreTake(logMx, portMAX_DELAY);
  logBuf[logHead] = line;
  logHead = (logHead + 1) % LOG_N;
  if (logCount < LOG_N) logCount++;
  xSemaphoreGive(logMx);
}

String logDump() {
  String s = "[";
  xSemaphoreTake(logMx, portMAX_DELAY);
  for (int i = 0; i < logCount; i++) {
    int idx = (logHead - logCount + i + LOG_N) % LOG_N;
    if (i) s += ',';
    s += '"'; s += jsonEsc(logBuf[idx]); s += '"';
  }
  xSemaphoreGive(logMx);
  return s + "]";
}

String hexStr(const uint8_t* d, size_t n) {
  String s;
  char b[4];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof(b), i ? " %02X" : "%02X", d[i]);
    s += b;
  }
  return s;
}

// ---------------- Configuração ----------------
enum : uint8_t { MODE_AUTO = 0, MODE_FAST = 1, MODE_SLOW = 2 };
struct Cfg {
  uint8_t mode = MODE_AUTO;
  uint8_t initAddr = 0x33;            // endereço da init 5 baud
  uint8_t target = KL_DEFAULT_TARGET; // alvo KWP (0x33 funcional ou físico, ex. 0x10)
  uint32_t baud = KLINE_BAUD;
  bool demo = false;
} cfg;

static void loadCfg() {
  prefs.begin("dash", true);
  cfg.mode = prefs.getUChar("mode", MODE_AUTO);
  cfg.initAddr = prefs.getUChar("init", 0x33);
  cfg.target = prefs.getUChar("tgt", KL_DEFAULT_TARGET);
  cfg.demo = prefs.getBool("demo", false);
  cfg.baud = prefs.getUInt("baud", KLINE_BAUD);
  prefs.end();
}

static void saveCfg() {
  prefs.begin("dash", false);
  prefs.putUChar("mode", cfg.mode);
  prefs.putUChar("init", cfg.initAddr);
  prefs.putUChar("tgt", cfg.target);
  prefs.putBool("demo", cfg.demo);
  prefs.putUInt("baud", cfg.baud);
  prefs.end();
}

// ---------------- Dados ao vivo ----------------
static SemaphoreHandle_t dataMx;
static float vals[MAX_PIDS];
static uint32_t stamps[MAX_PIDS];
static bool sup[MAX_PIDS];
static volatile bool obdOk = false;
static volatile float reqRate = 0;
static String statusMsg = "Iniciando";

static void setStatus(const char* s) {
  xSemaphoreTake(dataMx, portMAX_DELAY);
  statusMsg = s;
  xSemaphoreGive(dataMx);
}

static void storeVal(size_t i, float v) {
  xSemaphoreTake(dataMx, portMAX_DELAY);
  vals[i] = v;
  stamps[i] = millis();
  xSemaphoreGive(dataMx);
}

// ---------------- Comandos vindos da web ----------------
enum CmdType : uint8_t { CMD_NONE, CMD_RAW, CMD_DTC_READ, CMD_DTC_CLEAR, CMD_RECONNECT };
static volatile CmdType cmdType = CMD_NONE;
static uint8_t cmdData[64];
static uint8_t cmdLen = 0;
static String cmdResult;
static SemaphoreHandle_t cmdDone;

// Chamado pelo servidor web: entrega o comando à tarefa K-line e espera.
static String runCommand(CmdType t, const uint8_t* d = nullptr, uint8_t n = 0) {
  xSemaphoreTake(cmdDone, 0);
  if (d) memcpy(cmdData, d, n);
  cmdLen = n;
  cmdType = t;
  if (xSemaphoreTake(cmdDone, pdMS_TO_TICKS(12000)) != pdTRUE)
    return "{\"ok\":false,\"err\":\"tempo esgotado\"}";
  return cmdResult;
}

// ---------------- Tarefa K-line ----------------
static uint32_t nextInitAt = 0;
static int failCount = 0;
static int rpmIdx = -1;
static int fastList[MAX_PIDS], nFast = 0;
static int slowList[MAX_PIDS], nSlow = 0;
static uint32_t slot = 0, fastPos = 0, slowPos = 0, otherCnt = 0;
static uint32_t rateWin = 0, rateCnt = 0, lastKeepAlive = 0;
static uint32_t attempt = 0;

// Modo automático: cada tentativa usa a próxima combinação desta lista.
struct Attempt { bool fast; uint8_t addr; const char* desc; };
// As duas primeiras seguem a ordem da OBD9141 (painelKline).
static const Attempt AUTO_SEQ[] = {
  {false, 0x33, "5 baud, OBD 33 (ISO 9141 / KWP)"},
  {true,  0x33, "init rápida, OBD funcional 33"},
  {true,  0x10, "init rápida, ECU motor 10"},
  {false, 0x10, "5 baud, ECU motor 10"},
  {true,  0x11, "init rápida, ECU motor 11"},
  {false, 0x01, "5 baud, endereço 01"},
};
static const int N_AUTO = sizeof(AUTO_SEQ) / sizeof(AUTO_SEQ[0]);
static int autoIdx = 0;

// Pedidos do botão BOOT (setados no loop(), atendidos na tarefa K-line)
static volatile bool reqForceConnect = false;
static volatile bool reqTestSignal = false;

static void led(bool on) {
#if LED_PIN >= 0
  digitalWrite(LED_PIN, on);
#endif
}

static void rebuildLists() {
  rpmIdx = -1; nFast = nSlow = 0;
  for (size_t i = 0; i < NPIDS; i++) {
    if (!sup[i]) continue;
    if (PIDS[i].pid == 0x0C) rpmIdx = i;
    else if (PIDS[i].fast) fastList[nFast++] = i;
    else slowList[nSlow++] = i;
  }
}

static void detectSupported() {
  bool bits[0x61] = {};
  bool any = false;
  for (uint8_t base = 0x00; base <= 0x40; base += 0x20) {
    uint8_t req[2] = {0x01, base};
    KFrame f[3];
    int n = kl.request(req, 2, f, 3);
    if (n <= 0 || f[0].d[0] != 0x41 || f[0].len < 6) break;
    any = true;
    uint32_t m = ((uint32_t)f[0].d[2] << 24) | ((uint32_t)f[0].d[3] << 16) | (f[0].d[4] << 8) | f[0].d[5];
    for (int b = 0; b < 32; b++) bits[base + 1 + b] = (m >> (31 - b)) & 1;
    if (!bits[base + 0x20]) break;
  }
  String list;
  for (size_t i = 0; i < NPIDS; i++) {
    sup[i] = any && bits[PIDS[i].pid];
    if (sup[i]) { list += ' '; list += PIDS[i].key; }
  }
  obdOk = any;
  rebuildLists();
  if (any) {
    logLine("PIDs suportados:%s", list.c_str());
    setStatus("Conectado");
  } else {
    logLine("ECU não respondeu ao modo 01 (OBD padrão). Pode ser protocolo proprietário Marelli: use o console.");
    setStatus("Conectado (sem OBD padrão)");
  }
}

static void connect() {
  setStatus("Conectando...");
  led(true);
  if (kl.baud() != cfg.baud) kl.setBaud(cfg.baud);
  bool ok = false;
  if (cfg.mode == MODE_AUTO) {
    const Attempt& a = AUTO_SEQ[autoIdx];
    logLine("Tentativa #%u [%d/%d]: %s, %u bps", ++attempt, autoIdx + 1, N_AUTO, a.desc, cfg.baud);
    ok = a.fast ? kl.initFast(a.addr) : kl.init5Baud(a.addr);
    if (ok) logLine("Funcionou com: %s", a.desc);  // próxima reconexão começa por esta
    else autoIdx = (autoIdx + 1) % N_AUTO;
  } else if (cfg.mode == MODE_FAST) {
    logLine("Tentativa #%u: init rápida, alvo %02X, %u bps", ++attempt, cfg.target, cfg.baud);
    ok = kl.initFast(cfg.target);
  } else {
    logLine("Tentativa #%u: 5 baud, endereço %02X, %u bps", ++attempt, cfg.initAddr, cfg.baud);
    ok = kl.init5Baud(cfg.initAddr);
  }
  if (ok) {
    failCount = 0;
    detectSupported();
  } else {
    setStatus("Sem resposta da ECU");
    nextInitAt = millis() + 3000;
  }
  led(false);
}

static void lostConnection() {
  logLine("ECU parou de responder, reconectando");
  kl.disconnect();
  obdOk = false;
  setStatus("Reconectando...");
  nextInitAt = millis() + 1000;
}

static int nextPid() {
  slot++;
  if ((slot & 1) && rpmIdx >= 0) return rpmIdx;  // RPM em metade dos pedidos
  otherCnt++;
  if ((otherCnt % 4 == 0 || nFast == 0) && nSlow) return slowList[slowPos++ % nSlow];
  if (nFast) return fastList[fastPos++ % nFast];
  return rpmIdx;
}

static void pollStep() {
  int i = nextPid();
  if (i < 0) { delay(200); return; }
  uint8_t req[2] = {0x01, PIDS[i].pid};
  KFrame f[2];
  int n = kl.request(req, 2, f, 2);
  if (n > 0 && f[0].d[0] == 0x41 && f[0].d[1] == PIDS[i].pid && f[0].len >= 2 + PIDS[i].nbytes) {
    storeVal(i, PIDS[i].decode(f[0].d + 2));
    failCount = 0;
    rateCnt++;
    led(rateCnt & 1);  // pisca durante a comunicação
  } else if (n > 0 && f[0].d[0] == 0x7F) {
    logLine("PID %02X recusado (%s), removido", PIDS[i].pid, hexStr(f[0].d, f[0].len).c_str());
    sup[i] = false;
    rebuildLists();
    failCount = 0;
  } else if (++failCount >= 5) {
    lostConnection();
  }
}

static void keepAlive() {
  if (millis() - lastKeepAlive < 2000) { delay(20); return; }
  lastKeepAlive = millis();
  uint8_t tp[] = {0x3E};
  uint8_t p0[] = {0x01, 0x00};
  KFrame f[2];
  int n = kl.proto() == KProto::ISO9141 ? kl.request(p0, 2, f, 2) : kl.request(tp, 1, f, 2);
  if (n > 0) failCount = 0;
  else if (++failCount >= 3) lostConnection();
}

static String doRaw() {
  KFrame f[8];
  int n = kl.request(cmdData, cmdLen, f, 8, true);
  String r = "{\"ok\":";
  r += n > 0 ? "true" : "false";
  r += ",\"tx\":\"" + hexStr(cmdData, cmdLen) + "\",\"frames\":[";
  for (int i = 0; i < n; i++) {
    char src[4];
    snprintf(src, sizeof(src), "%02X", f[i].src);
    if (i) r += ',';
    r += "{\"src\":\"" + String(src) + "\",\"data\":\"" + hexStr(f[i].d, f[i].len) + "\"}";
    logLine("RAW %s -> [%s] %s", hexStr(cmdData, cmdLen).c_str(), src, hexStr(f[i].d, f[i].len).c_str());
  }
  if (n <= 0) logLine("RAW %s -> sem resposta", hexStr(cmdData, cmdLen).c_str());
  return r + "]}";
}

static String doDtcRead() {
  uint8_t req[] = {0x03};
  KFrame f[8];
  int n = kl.request(req, 1, f, 8, true);
  if (n <= 0) return "{\"ok\":false,\"err\":\"sem resposta ao modo 03\"}";
  String codes, raw;
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (raw.length()) raw += " | ";
    raw += hexStr(f[i].d, f[i].len);
    if (f[i].d[0] != 0x43) continue;
    for (int k = 1; k + 1 < f[i].len; k += 2) {
      if (!f[i].d[k] && !f[i].d[k + 1]) continue;
      if (count++) codes += ',';
      codes += "\"" + dtcToString(f[i].d[k], f[i].d[k + 1]) + "\"";
    }
  }
  logLine("DTCs lidos: %d", count);
  return "{\"ok\":true,\"codes\":[" + codes + "],\"raw\":\"" + raw + "\"}";
}

static String doDtcClear() {
  uint8_t req[] = {0x04};
  KFrame f[4];
  int n = kl.request(req, 1, f, 4, true);
  bool ok = n > 0 && f[0].d[0] == 0x44;
  logLine(ok ? "DTCs apagados" : "Falha ao apagar DTCs");
  return ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"ECU recusou o modo 04 (motor deve estar desligado, ignição ligada)\"}";
}

static void demoStep() {
  float t = millis() / 1000.0f;
  float acc = 0.5f + 0.5f * sinf(t * 0.6f);
  for (size_t i = 0; i < NPIDS; i++) {
    float v = 0;
    switch (PIDS[i].pid) {
      case 0x0C: v = 850 + 4200 * acc * acc + 40 * sinf(t * 7); break;
      case 0x0D: v = 20 + 90 * acc; break;
      case 0x11: v = 4 + 70 * acc; break;
      case 0x0B: v = 32 + 60 * acc; break;
      case 0x0E: v = 12 + 18 * acc; break;
      case 0x04: v = 22 + 60 * acc; break;
      case 0x05: v = 91 + 3 * sinf(t * 0.05f); break;
      case 0x0F: v = 38 + 2 * sinf(t * 0.03f); break;
      case 0x06: v = 3 * sinf(t * 2.3f); break;
      case 0x07: v = 2.3f; break;
      case 0x14: v = 0.45f + 0.4f * sinf(t * 5); break;
      case 0x42: v = 14.1f; break;
      case 0x01: v = 0; break;
    }
    sup[i] = true;
    storeVal(i, v);
  }
  obdOk = true;
  rateCnt += 10;
}

static void processCmd() {
  CmdType t = cmdType;
  if (t == CMD_NONE) return;
  String r;
  if (t == CMD_RECONNECT) {
    kl.disconnect();
    obdOk = false;
    nextInitAt = 0;
    r = "{\"ok\":true}";
  } else if (cfg.demo) {
    r = t == CMD_DTC_READ ? "{\"ok\":true,\"codes\":[\"P0115\",\"P0301\"],\"raw\":\"43 00 73 03 01 00 00 (demo)\"}"
                          : "{\"ok\":false,\"err\":\"modo demonstração ativo\"}";
  } else if (!kl.connected()) {
    r = "{\"ok\":false,\"err\":\"ECU desconectada\"}";
  } else if (t == CMD_RAW) {
    r = doRaw();
  } else if (t == CMD_DTC_READ) {
    r = doDtcRead();
  } else if (t == CMD_DTC_CLEAR) {
    r = doDtcClear();
  }
  cmdResult = r;
  cmdType = CMD_NONE;
  xSemaphoreGive(cmdDone);
}

static void handleButtonRequests() {
  if (reqTestSignal) {
    reqTestSignal = false;
    logLine("BOOT: teste do circuito (20 s). Meça a K-line (pino 7 OBD) com o multímetro.");
    setStatus("Teste do circuito");
    obdOk = false;
    led(true);
    kl.testSignal(1000, 10);
    led(false);
    logLine("Teste concluído");
    nextInitAt = 0;
  }
  if (reqForceConnect) {
    reqForceConnect = false;
    logLine("BOOT: conexão forçada");
    if (cfg.demo) { cfg.demo = false; saveCfg(); logLine("Modo demonstração desligado"); }
    autoIdx = 0;
    kl.disconnect();
    obdOk = false;
    xSemaphoreTake(dataMx, portMAX_DELAY);
    memset(stamps, 0, sizeof(stamps));
    xSemaphoreGive(dataMx);
    nextInitAt = 0;  // conecta imediatamente
  }
}

static void klineTask(void*) {
  uint32_t lastDemoMsg = 0;
  for (;;) {
    handleButtonRequests();
    processCmd();
    if (millis() - rateWin >= 1000) {
      reqRate = rateCnt * 1000.0f / (millis() - rateWin);
      rateCnt = 0;
      rateWin = millis();
    }
    if (cfg.demo) {
      if (kl.connected()) kl.disconnect();
      setStatus("Demonstração");
      if (millis() - lastDemoMsg > 15000) {
        lastDemoMsg = millis();
        logLine("Modo demonstração ativo: K-line parada. Toque no BOOT para conectar na ECU.");
      }
      demoStep();
      delay(100);
      continue;
    }
    if (!kl.connected()) {
      if ((int32_t)(millis() - nextInitAt) >= 0) connect();
      else delay(20);
      continue;
    }
    if (obdOk) pollStep();
    else keepAlive();
  }
}

// ---------------- Servidor web ----------------
static float readVbat() {
#if VBAT_PIN >= 0
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(VBAT_PIN);
  float v = mv / 8.0f / 1000.0f * VBAT_RATIO;
  return v < 1.0f ? NAN : v;
#else
  return NAN;
#endif
}

static void addNum(String& s, const char* key, float v, int dec) {
  s += '"'; s += key; s += "\":";
  if (isnan(v)) s += "null";
  else s += String(v, dec);
}

static void handleData() {
  String s;
  s.reserve(900);
  char buf[16];
  s += "{\"conn\":"; s += (kl.connected() || cfg.demo) ? "true" : "false";
  s += ",\"demo\":"; s += cfg.demo ? "true" : "false";
  s += ",\"obd\":"; s += obdOk ? "true" : "false";
  s += ",\"proto\":\""; s += cfg.demo ? "Demonstração" : kl.protoName(); s += '"';
  snprintf(buf, sizeof(buf), "%02X", kl.ecuAddr());
  s += ",\"ecu\":\""; s += buf; s += '"';
  snprintf(buf, sizeof(buf), "%02X %02X", kl.kb1(), kl.kb2());
  s += ",\"kb\":\""; s += buf; s += '"';
  s += ",\"hz\":"; s += String((float)reqRate, 1);
  s += ",\"echoErr\":"; s += kl.echoErrors();
  s += ",\"trace\":"; s += kl.trace() ? "true" : "false";
  s += ','; addNum(s, "vbat", readVbat(), 1);
  s += ",\"cfg\":{\"mode\":"; s += cfg.mode;
  snprintf(buf, sizeof(buf), "%02X", cfg.initAddr);
  s += ",\"init\":\""; s += buf;
  snprintf(buf, sizeof(buf), "%02X", cfg.target);
  s += "\",\"tgt\":\""; s += buf;
  s += "\",\"baud\":"; s += cfg.baud; s += "}";

  xSemaphoreTake(dataMx, portMAX_DELAY);
  s += ",\"status\":\""; s += jsonEsc(statusMsg); s += '"';
  String supList;
  s += ",\"v\":{";
  bool first = true;
  uint32_t now = millis();
  for (size_t i = 0; i < NPIDS; i++) {
    bool fresh = stamps[i] && now - stamps[i] < 5000;
    if (PIDS[i].pid == 0x01) {
      int a = fresh ? (int)vals[i] : -1;
      if (!first) s += ',';
      s += "\"mil\":"; s += a < 0 ? "null" : ((a & 0x80) ? "true" : "false");
      s += ",\"ndtc\":"; s += a < 0 ? "null" : String(a & 0x7F);
    } else {
      if (!first) s += ',';
      addNum(s, PIDS[i].key, fresh ? vals[i] : NAN, 2);
    }
    first = false;
    if (sup[i]) { if (supList.length()) supList += ','; supList += '"'; supList += PIDS[i].key; supList += '"'; }
  }
  xSemaphoreGive(dataMx);
  s += "},\"sup\":[" + supList + "]}";
  server.send(200, "application/json", s);
}

static int parseHex(const String& in, uint8_t* out, int max) {
  String h;
  for (char c : in) if (isxdigit((unsigned char)c)) h += c;
  if (h.length() % 2) return -1;
  int n = 0;
  for (unsigned i = 0; i < h.length() && n < max; i += 2)
    out[n++] = strtoul(h.substring(i, i + 2).c_str(), nullptr, 16);
  return n;
}

static void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/api/data", HTTP_GET, handleData);
  server.on("/api/log", HTTP_GET, [] { server.send(200, "application/json", logDump()); });
  server.on("/api/dtc", HTTP_GET, [] { server.send(200, "application/json", runCommand(CMD_DTC_READ)); });
  server.on("/api/dtc/clear", HTTP_POST, [] { server.send(200, "application/json", runCommand(CMD_DTC_CLEAR)); });
  server.on("/api/raw", HTTP_POST, [] {
    uint8_t d[63];
    int n = parseHex(server.arg("hex"), d, sizeof(d));
    if (n <= 0) { server.send(400, "application/json", "{\"ok\":false,\"err\":\"hex inválido\"}"); return; }
    server.send(200, "application/json", runCommand(CMD_RAW, d, n));
  });
  server.on("/api/connect", HTTP_POST, [] {
    uint8_t b;
    if (server.hasArg("mode")) cfg.mode = constrain(server.arg("mode").toInt(), 0, 2);
    if (parseHex(server.arg("init"), &b, 1) == 1) cfg.initAddr = b;
    if (parseHex(server.arg("tgt"), &b, 1) == 1) cfg.target = b;
    if (server.hasArg("baud")) {
      long bd = server.arg("baud").toInt();
      if (bd >= 1200 && bd <= 115200) cfg.baud = bd;
    }
    autoIdx = 0;
    saveCfg();
    logLine("Config: modo %d, init %02X, alvo %02X, %u bps", cfg.mode, cfg.initAddr, cfg.target, cfg.baud);
    server.send(200, "application/json", runCommand(CMD_RECONNECT));
  });
  server.on("/api/demo", HTTP_POST, [] {
    cfg.demo = server.arg("on") == "1";
    saveCfg();
    if (!cfg.demo) {
      xSemaphoreTake(dataMx, portMAX_DELAY);
      memset(stamps, 0, sizeof(stamps));
      xSemaphoreGive(dataMx);
      obdOk = false;
      nextInitAt = 0;
    }
    logLine(cfg.demo ? "Modo demonstração ligado" : "Modo demonstração desligado");
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/trace", HTTP_POST, [] {
    kl.setTrace(server.arg("on") == "1");
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.onNotFound([] { server.sendHeader("Location", "/"); server.send(302); });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  logMx = xSemaphoreCreateMutex();
  dataMx = xSemaphoreCreateMutex();
  cmdDone = xSemaphoreCreateBinary();
  loadCfg();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT);
  led(false);
#endif

  logLine("===== Fiesta Dash =====");
  logLine("K-line: RX GPIO%d, TX GPIO%d, TX %s, %d bps", KLINE_RX_PIN, KLINE_TX_PIN,
          KLINE_TX_INVERT ? "invertido" : "normal", KLINE_BAUD);
  logLine("Config salva: modo %d, init %02X, alvo %02X, %u bps, demonstração %s", cfg.mode, cfg.initAddr,
          cfg.target, cfg.baud, cfg.demo ? "LIGADA" : "desligada");
  logLine("BOOT: toque = forçar conexão | segurar 3 s = teste do circuito");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  logLine("Wi-Fi \"%s\" ativo. Abra http://%s", WIFI_SSID, WiFi.softAPIP().toString().c_str());

#if VBAT_PIN >= 0
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);
#endif
  kl.begin();
  setupWeb();
  // Tarefa da K-line no core 1, prioridade acima do loop() para manter as temporizações.
  xTaskCreatePinnedToCore(klineTask, "kline", 8192, nullptr, 2, nullptr, 1);
}

// Botão BOOT: toque curto força a conexão, segurar 3 s inicia o teste do circuito.
static void pollButton() {
  static bool wasDown = false, longFired = false;
  static uint32_t downAt = 0;
  bool down = digitalRead(BUTTON_PIN) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) { downAt = now; longFired = false; }
  if (down && !longFired && now - downAt >= BUTTON_LONG_MS) {
    longFired = true;
    reqTestSignal = true;
  }
  if (!down && wasDown && !longFired && now - downAt >= 50) reqForceConnect = true;
  wasDown = down;
}

void loop() {
  server.handleClient();
  pollButton();
  delay(2);
}
