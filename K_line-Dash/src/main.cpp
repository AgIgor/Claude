// Dash web CAN para Ford Fiesta 2004 (ECU Magneti Marelli IAW 4AFR.BR)
// ESP32 + MCP2515 -> Wi-Fi AP -> dashboard no celular.
// Dois modos: diagnóstico OBD (pede os dados à ECU) ou escuta da rede ECU <-> painel
// de instrumentos (só ouve as mensagens que a ECU manda para o painel).
// A versão K-line anterior está em backup_kline/.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <stdarg.h>
#include "config.h"
#include "canbus.h"
#include "obd.h"
#include "sniff.h"
#include "log.h"
#include "web_index.h"

#define MAX_PIDS 16

static ObdCan bus;
static WebServer server(80);
static WebSocketsServer ws(WS_PORT);
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

// Como jsonEsc, mas preserva as quebras de linha (texto dos sinais).
static String jsonEsc2(const String& in) {
  String o;
  for (char c : in) {
    if (c == '\n') o += "\\n";
    else if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

void logLine(const char* fmt, ...) {
  char tmp[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  char line[232];
  snprintf(line, sizeof(line), "[%7.1f] %s", millis() / 1000.0, tmp);
  size_t n = strlen(line);
  line[n] = '\n';  // a linha inteira numa só escrita (cabe: tmp tem no máximo 199 caracteres)
  Serial.write((const uint8_t*)line, n + 1);
  line[n] = 0;
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
enum : uint8_t { MODE_OBD = 0, MODE_SNIFF = 1 };

struct Cfg {
  int8_t rate = -1;                 // -1 automático; 0/1/2 = 500/250/125 kbps
  uint16_t reqId = CAN_DEFAULT_REQ; // 7DF funcional ou ID físico (7E0...)
  uint8_t xtal = CAN_XTAL_MHZ;      // cristal do MCP2515
  uint8_t mode = MODE_OBD;          // MODE_OBD ou MODE_SNIFF
  bool demo = false;
} cfg;

static void loadCfg() {
  prefs.begin("dashcan", true);
  cfg.rate = prefs.getChar("rate", -1);
  cfg.reqId = prefs.getUShort("req", CAN_DEFAULT_REQ);
  cfg.xtal = prefs.getUChar("xtal", CAN_XTAL_MHZ);
  cfg.demo = prefs.getBool("demo", false);
  cfg.mode = prefs.getUChar("mode", MODE_OBD) == MODE_SNIFF ? MODE_SNIFF : MODE_OBD;
  prefs.end();
}

static void saveCfg() {
  prefs.begin("dashcan", false);
  prefs.putChar("rate", cfg.rate);
  prefs.putUShort("req", cfg.reqId);
  prefs.putUChar("xtal", cfg.xtal);
  prefs.putBool("demo", cfg.demo);
  prefs.putUChar("mode", cfg.mode);
  prefs.end();
}

static const char* modeName(uint8_t m) { return m == MODE_SNIFF ? "escuta da rede" : "diagnóstico OBD"; }

static String rateName(int8_t r) {
  return r < 0 ? String("auto") : String(ObdCan::RATE_KBPS[r]) + " kbps";
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

// ---------------- Transmissão contínua (WebSocket, porta WS_PORT) ----------------
// Ativa enquanto houver alguma página conectada. Cada mensagem traz várias linhas:
//   F <ms> <ID> <dlc> <dados hex>   cada quadro CAN recebido no modo escuta
//   V <ms> <chave> <valor>          cada valor novo (escuta ou OBD)
//   J <json>                        estado completo, 2x por segundo (mesmo de /api/data)
// A tarefa CAN só copia as linhas para um buffer; o envio acontece no loop(), que é quem
// cuida do servidor WebSocket.
static volatile bool streamOn = false;
static SemaphoreHandle_t streamMx;
static char streamBuf[12288];
static size_t streamLen = 0;
static uint32_t streamDrop = 0;

static void streamPut(const char* s, size_t n) {
  if (!streamOn) return;
  xSemaphoreTake(streamMx, portMAX_DELAY);
  if (streamLen + n <= sizeof(streamBuf)) {
    memcpy(streamBuf + streamLen, s, n);
    streamLen += n;
  } else {
    streamDrop++;  // página lenta demais: descarta em vez de travar a leitura do CAN
  }
  xSemaphoreGive(streamMx);
}

static void streamFlush() {
  static char out[sizeof(streamBuf)];
  xSemaphoreTake(streamMx, portMAX_DELAY);
  size_t n = streamLen;
  memcpy(out, streamBuf, n);
  streamLen = 0;
  xSemaphoreGive(streamMx);
  if (n) ws.broadcastTXT((uint8_t*)out, n);
}

static void storeVal(size_t i, float v) {
  uint32_t now = millis();
  xSemaphoreTake(dataMx, portMAX_DELAY);
  vals[i] = v;
  stamps[i] = now;
  xSemaphoreGive(dataMx);
  if (streamOn && PIDS[i].key[0] != '_') {
    char b[40];
    int n = snprintf(b, sizeof(b), "V %lu %s %.3f\n", (unsigned long)now, PIDS[i].key, v);
    streamPut(b, n);
  }
}

// ---------------- Comandos vindos da web ----------------
enum CmdType : uint8_t { CMD_NONE, CMD_RAW, CMD_DTC_READ, CMD_DTC_CLEAR, CMD_RECONNECT, CMD_PROBE };
static volatile CmdType cmdType = CMD_NONE;
static uint8_t cmdData[8];
static uint8_t cmdLen = 0;
static String cmdResult;
static SemaphoreHandle_t cmdDone;

// Chamado pelo servidor web: entrega o comando à tarefa OBD e espera.
static String runCommand(CmdType t, const uint8_t* d = nullptr, uint8_t n = 0) {
  xSemaphoreTake(cmdDone, 0);
  if (d) memcpy(cmdData, d, n);
  cmdLen = n;
  cmdType = t;
  if (xSemaphoreTake(cmdDone, pdMS_TO_TICKS(20000)) != pdTRUE)
    return "{\"ok\":false,\"err\":\"tempo esgotado\"}";
  return cmdResult;
}

// ---------------- Tarefa OBD ----------------
static uint32_t nextInitAt = 0;
static int failCount = 0;
static int rpmIdx = -1;
static int fastList[MAX_PIDS], nFast = 0;
static int slowList[MAX_PIDS], nSlow = 0;
static uint32_t slot = 0, fastPos = 0, slowPos = 0, otherCnt = 0;
static uint32_t rateWin = 0, rateCnt = 0, lastKeepAlive = 0, lastReq = 0;
static uint32_t attempt = 0;
static volatile bool reapplyXtal = false;
static bool probePending = false;

// Pedidos do botão BOOT (setados no loop(), atendidos na tarefa OBD)
static volatile bool reqForceConnect = false;
static volatile bool reqSelfTest = false;

static void led(bool on) {
#if LED_PIN >= 0
  digitalWrite(LED_PIN, on);
#endif
}

static int obdRequest(const uint8_t* p, uint8_t n, ObdFrame* f, int maxF, bool multi = false) {
  uint32_t since = millis() - lastReq;
  if (since < CAN_GAP_MS) delay(CAN_GAP_MS - since);
  int r = bus.request(p, n, f, maxF, multi);
  lastReq = millis();
  return r;
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
    ObdFrame f[2];
    int n = obdRequest(req, 2, f, 2);
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
    logLine("ECU não aceitou o OBD padrão (01 00). Rodando sondagem de serviços...");
    setStatus("Conectado (sem OBD padrão)");
    probePending = true;
  }
}

static void connect() {
  setStatus("Conectando...");
  led(true);
  if (!bus.chipOk() && !bus.begin(cfg.xtal)) {
    setStatus("MCP2515 não encontrado");
    nextInitAt = millis() + 5000;
    led(false);
    return;
  }
  logLine("Tentativa #%u: velocidade %s, pedido em %03X", ++attempt, rateName(cfg.rate).c_str(), cfg.reqId);
  if (bus.connect(cfg.rate, cfg.reqId)) {
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
  bus.disconnect();
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
  ObdFrame f[2];
  int n = obdRequest(req, 2, f, 2);
  if (n > 0 && f[0].d[0] == 0x41 && f[0].d[1] == PIDS[i].pid && f[0].len >= 2 + PIDS[i].nbytes) {
    storeVal(i, PIDS[i].decode(f[0].d + 2));
    failCount = 0;
    rateCnt++;
    led(rateCnt & 8);  // pisca durante a comunicação
  } else if (n > 0 && f[0].d[0] == 0x7F) {
    logLine("PID %02X recusado (%s), removido", PIDS[i].pid, hexStr(f[0].d, f[0].len).c_str());
    sup[i] = false;
    rebuildLists();
    failCount = 0;
  } else if (++failCount >= 8) {
    lostConnection();
  }
}

static void keepAlive() {
  if (millis() - lastKeepAlive < 2000) { delay(20); return; }
  lastKeepAlive = millis();
  uint8_t tp[] = {0x3E, 0x00};
  ObdFrame f[1];
  if (obdRequest(tp, 2, f, 1) > 0) failCount = 0;
  else if (++failCount >= 3) lostConnection();
}

// ---------------- Modo escuta ----------------
// Valor decodificado de um sinal mapeado -> mesmo lugar dos PIDs, o painel não muda.
static void sniffValue(const char* key, float v) {
  for (size_t i = 0; i < NPIDS; i++) {
    if (strcmp(PIDS[i].key, key)) continue;
    sup[i] = true;
    storeVal(i, v);
    return;
  }
}

static uint32_t lastFrameAt = 0, lastSniffTick = 0;

static void sniffStep() {
  if (!bus.sniffing()) {
    if ((int32_t)(millis() - nextInitAt) < 0) { delay(20); return; }
    setStatus("Procurando tráfego CAN...");
    led(true);
    if (!bus.chipOk() && !bus.begin(cfg.xtal)) {
      setStatus("MCP2515 não encontrado");
      nextInitAt = millis() + 5000;
      led(false);
      return;
    }
    logLine("Escuta #%u: velocidade %s", ++attempt, rateName(cfg.rate).c_str());
    if (bus.startSniff(cfg.rate)) {
      lastFrameAt = lastSniffTick = millis();
      memset(sup, 0, sizeof(sup));
      setStatus("Escutando a rede");
    } else {
      setStatus("Sem tráfego CAN");
      nextInitAt = millis() + 3000;
    }
    led(false);
    return;
  }
  can_frame f;
  int got = 0;
  while (got < 32 && bus.readFrame(&f)) {
    Sniff::onFrame(f);
    got++;
    if (streamOn && !(f.can_id & CAN_RTR_FLAG)) {
      static const char H[] = "0123456789ABCDEF";
      char b[48];
      uint8_t dlc = min<uint8_t>(f.can_dlc, 8);
      int n = (f.can_id & CAN_EFF_FLAG)
                  ? snprintf(b, sizeof(b), "F %lu %08lX %u ", (unsigned long)millis(), (unsigned long)(f.can_id & CAN_EFF_MASK), dlc)
                  : snprintf(b, sizeof(b), "F %lu %03lX %u ", (unsigned long)millis(), (unsigned long)(f.can_id & CAN_SFF_MASK), dlc);
      for (int k = 0; k < dlc; k++) { b[n++] = H[f.data[k] >> 4]; b[n++] = H[f.data[k] & 15]; }
      b[n++] = '\n';
      streamPut(b, n);
    }
  }
  uint32_t now = millis();
  if (got) {
    lastFrameAt = now;
    rateCnt += got;
    led((rateCnt >> 6) & 1);
    Sniff::addOverflows(bus.takeOverflows());
  }
  if (now - lastSniffTick >= 1000) {
    lastSniffTick = now;
    Sniff::tick();
    char st[64];
    snprintf(st, sizeof(st), "Escutando: %u IDs, %.0f quadros/s", Sniff::idCount(), Sniff::fps());
    setStatus(st);
  }
  if (now - lastFrameAt > 3000) {
    logLine("Escuta: o tráfego parou (ignição desligada?). Procurando de novo.");
    bus.disconnect();
    setStatus("Sem tráfego CAN");
    nextInitAt = now + 1000;
    return;
  }
  // Sem quadro pendente: cede 1 ms (o MCP2515 guarda 2 quadros nesse intervalo).
  if (!got) vTaskDelay(1);
}

// Códigos de resposta negativa (7F <serviço> <NRC>), ISO 14229 / 14230
static const char* nrcName(uint8_t c) {
  switch (c) {
    case 0x10: return "recusa geral";
    case 0x11: return "serviço não suportado";
    case 0x12: return "subfunção/parâmetro não suportado";
    case 0x13: return "tamanho/formato inválido";
    case 0x21: return "ocupada, repita";
    case 0x22: return "condições não atendidas";
    case 0x24: return "sequência errada";
    case 0x31: return "fora da faixa (ID não existe)";
    case 0x33: return "acesso de segurança negado";
    case 0x35: return "chave inválida";
    case 0x78: return "resposta pendente";
    case 0x7E: return "subfunção não suportada nesta sessão";
    case 0x7F: return "serviço não suportado nesta sessão";
    default:   return "desconhecido";
  }
}

static String describeResp(const ObdFrame& f) {
  char src[6];
  snprintf(src, sizeof(src), "%03X", f.src);
  String s = String(src) + ": " + hexStr(f.d, f.len);
  if (f.len >= 3 && f.d[0] == 0x7F) s += String(" (recusado: ") + nrcName(f.d[2]) + ")";
  else s += " (aceito)";
  return s;
}

// Testa os serviços de diagnóstico mais comuns para descobrir o que a ECU entende.
static String doProbe() {
  struct Probe { uint8_t n; uint8_t d[4]; const char* what; };
  static const Probe P[] = {
    {2, {0x01, 0x00}, "OBD modo 01 (PIDs suportados)"},
    {2, {0x09, 0x02}, "OBD modo 09 (VIN)"},
    {1, {0x03},       "OBD modo 03 (falhas)"},
    {2, {0x3E, 0x00}, "TesterPresent"},
    {2, {0x10, 0x81}, "KWP2000 sessão padrão"},
    {2, {0x1A, 0x80}, "KWP2000 identificação da ECU"},
    {2, {0x1A, 0x90}, "KWP2000 VIN"},
    {2, {0x21, 0x01}, "KWP2000 bloco local 01"},
    {4, {0x18, 0x00, 0xFF, 0x00}, "KWP2000 ler falhas"},
    {2, {0x10, 0x01}, "UDS sessão padrão"},
    {3, {0x22, 0xF1, 0x90}, "UDS VIN (DID F190)"},
    {3, {0x22, 0xF1, 0x88}, "UDS software (DID F188)"},
    {3, {0x19, 0x02, 0xFF}, "UDS ler falhas"},
  };
  logLine("=== Sondagem de serviços (pedido em %03X) ===", bus.reqId());
  String json = "{\"ok\":true,\"lines\":[";
  int idx = 0;
  for (const Probe& p : P) {
    ObdFrame f[3];
    int n = obdRequest(p.d, p.n, f, 3, true);
    String line = hexStr(p.d, p.n) + " " + p.what + " -> ";
    if (n <= 0) line += "sem resposta";
    for (int i = 0; i < n; i++) line += (i ? " | " : "") + describeResp(f[i]);
    logLine("%s", line.c_str());
    if (idx++) json += ',';
    json += "\"" + jsonEsc(line) + "\"";
  }
  logLine("=== Fim da sondagem ===");
  return json + "]}";
}

static String doRaw() {
  ObdFrame f[8];
  int n = obdRequest(cmdData, cmdLen, f, 8, true);
  String r = "{\"ok\":";
  r += n > 0 ? "true" : "false";
  r += ",\"tx\":\"" + hexStr(cmdData, cmdLen) + "\",\"frames\":[";
  for (int i = 0; i < n; i++) {
    char src[6];
    snprintf(src, sizeof(src), "%03X", f[i].src);
    if (i) r += ',';
    r += "{\"src\":\"" + String(src) + "\",\"data\":\"" + hexStr(f[i].d, f[i].len) + "\"}";
    logLine("RAW %s -> %s", hexStr(cmdData, cmdLen).c_str(), describeResp(f[i]).c_str());
  }
  if (n <= 0) logLine("RAW %s -> sem resposta", hexStr(cmdData, cmdLen).c_str());
  return r + "]}";
}

static String doDtcRead() {
  uint8_t req[] = {0x03};
  ObdFrame f[8];
  int n = obdRequest(req, 1, f, 8, true);
  if (n <= 0) return "{\"ok\":false,\"err\":\"sem resposta ao modo 03\"}";
  String codes, raw;
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (raw.length()) raw += " | ";
    raw += hexStr(f[i].d, f[i].len);
    if (f[i].d[0] != 0x43) continue;
    // No CAN a resposta traz o nº de códigos logo após o 43 (comprimento par).
    int k = (f[i].len % 2 == 0) ? 2 : 1;
    for (; k + 1 < f[i].len; k += 2) {
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
  ObdFrame f[4];
  int n = obdRequest(req, 1, f, 4, true);
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
    bus.disconnect();
    if (reapplyXtal) { reapplyXtal = false; bus.begin(cfg.xtal); }
    obdOk = false;
    nextInitAt = 0;
    r = "{\"ok\":true}";
  } else if (cfg.mode == MODE_SNIFF && !cfg.demo) {
    r = "{\"ok\":false,\"err\":\"modo escuta ativo: o ESP32 não transmite. Mude para diagnóstico OBD.\"}";
  } else if (cfg.demo) {
    r = t == CMD_DTC_READ ? "{\"ok\":true,\"codes\":[\"P0115\",\"P0301\"],\"raw\":\"43 02 01 15 03 01 (demo)\"}"
                          : "{\"ok\":false,\"err\":\"modo demonstração ativo\"}";
  } else if (!bus.connected()) {
    r = "{\"ok\":false,\"err\":\"ECU desconectada\"}";
  } else if (t == CMD_RAW) {
    r = doRaw();
  } else if (t == CMD_DTC_READ) {
    r = doDtcRead();
  } else if (t == CMD_DTC_CLEAR) {
    r = doDtcClear();
  } else if (t == CMD_PROBE) {
    r = doProbe();
  }
  cmdResult = r;
  cmdType = CMD_NONE;
  xSemaphoreGive(cmdDone);
}

static void handleButtonRequests() {
  if (reqSelfTest) {
    reqSelfTest = false;
    logLine("BOOT: autoteste do MCP2515 (loopback, não transmite no carro)");
    bus.disconnect();
    obdOk = false;
    setStatus("Autoteste");
    led(true);
    if (!bus.chipOk()) bus.begin(cfg.xtal);
    bool ok = bus.selfTest();
    led(false);
    logLine(ok ? "Autoteste OK: SPI e MCP2515 funcionando"
               : "Autoteste FALHOU: confira fiação SPI, alimentação e conversor de nível");
    nextInitAt = 0;
  }
  if (reqForceConnect) {
    reqForceConnect = false;
    logLine("BOOT: conexão forçada");
    if (cfg.demo) { cfg.demo = false; saveCfg(); logLine("Modo demonstração desligado"); }
    bus.disconnect();
    obdOk = false;
    xSemaphoreTake(dataMx, portMAX_DELAY);
    memset(stamps, 0, sizeof(stamps));
    xSemaphoreGive(dataMx);
    nextInitAt = 0;  // conecta imediatamente
  }
}

static void obdTask(void*) {
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
      if (bus.connected()) bus.disconnect();
      setStatus("Demonstração");
      if (millis() - lastDemoMsg > 15000) {
        lastDemoMsg = millis();
        logLine("Modo demonstração ativo: CAN parado. Toque no BOOT para conectar na ECU.");
      }
      demoStep();
      delay(100);
      continue;
    }
    if (cfg.mode == MODE_SNIFF) { sniffStep(); continue; }
    if (bus.sniffing()) bus.disconnect();
    if (!bus.connected()) {
      if ((int32_t)(millis() - nextInitAt) >= 0) connect();
      else delay(20);
      continue;
    }
    if (probePending) { probePending = false; doProbe(); continue; }
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

static String dataJson() {
  String s;
  s.reserve(1000);
  char buf[16];
  s += "{\"conn\":"; s += (bus.connected() || bus.sniffing() || cfg.demo) ? "true" : "false";
  s += ",\"mode\":"; s += cfg.mode;
  s += ",\"demo\":"; s += cfg.demo ? "true" : "false";
  s += ",\"obd\":"; s += obdOk ? "true" : "false";
  s += ",\"proto\":\""; s += cfg.demo ? String("Demonstração") : bus.protoName(); s += '"';
  snprintf(buf, sizeof(buf), "%03X", bus.ecuId());
  s += ",\"ecu\":\""; s += bus.ecuId() ? buf : "-"; s += '"';
  s += ",\"bus\":\""; s += jsonEsc(bus.busInfo()); s += '"';
  s += ",\"chip\":"; s += bus.chipOk() ? "true" : "false";
  s += ",\"hz\":"; s += String((float)reqRate, 1);
  s += ",\"txErr\":"; s += bus.txErrors();
  s += ",\"tec\":"; s += bus.tec();
  s += ",\"rec\":"; s += bus.rec();
  s += ",\"trace\":"; s += bus.trace() ? "true" : "false";
  s += ','; addNum(s, "vbat", readVbat(), 1);
  snprintf(buf, sizeof(buf), "%03X", cfg.reqId);
  s += ",\"cfg\":{\"rate\":"; s += cfg.rate;
  s += ",\"req\":\""; s += buf;
  s += "\",\"xtal\":"; s += cfg.xtal;
  s += ",\"mode\":"; s += cfg.mode; s += "}";

  xSemaphoreTake(dataMx, portMAX_DELAY);
  s += ",\"status\":\""; s += jsonEsc(statusMsg); s += '"';
  String supList;
  s += ",\"v\":{";
  bool first = true;
  uint32_t now = millis();
  for (size_t i = 0; i < NPIDS; i++) {
    bool fresh = stamps[i] && now - stamps[i] < 5000;
    if (!first) s += ',';
    if (PIDS[i].pid == 0x01) {
      int a = fresh ? (int)vals[i] : -1;
      s += "\"mil\":"; s += a < 0 ? "null" : ((a & 0x80) ? "true" : "false");
      s += ",\"ndtc\":"; s += a < 0 ? "null" : String(a & 0x7F);
    } else {
      addNum(s, PIDS[i].key, fresh ? vals[i] : NAN, 2);
    }
    first = false;
    if (sup[i]) { if (supList.length()) supList += ','; supList += '"'; supList += PIDS[i].key; supList += '"'; }
  }
  xSemaphoreGive(dataMx);
  s += "},\"sup\":[" + supList + "]}";
  return s;
}

static void handleData() { server.send(200, "application/json", dataJson()); }

static void setMode(int mode) {
  uint8_t md = mode == MODE_SNIFF ? MODE_SNIFF : MODE_OBD;
  if (md == cfg.mode) return;
  cfg.mode = md;
  xSemaphoreTake(dataMx, portMAX_DELAY);
  memset(stamps, 0, sizeof(stamps));
  memset(sup, 0, sizeof(sup));
  xSemaphoreGive(dataMx);
}

static bool saveSignals(const String& text, String& err) {
  if (!Sniff::setSignals(text, err)) return false;
  prefs.begin("dashcan", false);
  prefs.putString("sigs", Sniff::signals());
  prefs.end();
  xSemaphoreTake(dataMx, portMAX_DELAY);
  memset(stamps, 0, sizeof(stamps));
  memset(sup, 0, sizeof(sup));
  xSemaphoreGive(dataMx);
  logLine("Sinais mapeados salvos");
  return true;
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
    uint8_t d[8];
    int n = parseHex(server.arg("hex"), d, sizeof(d));
    if (n <= 0 || n > 7) {
      server.send(400, "application/json", "{\"ok\":false,\"err\":\"hex inválido (1 a 7 bytes)\"}");
      return;
    }
    server.send(200, "application/json", runCommand(CMD_RAW, d, n));
  });
  server.on("/api/connect", HTTP_POST, [] {
    if (server.hasArg("rate")) cfg.rate = constrain(server.arg("rate").toInt(), -1, 2);
    if (server.hasArg("req")) {
      long id = strtol(server.arg("req").c_str(), nullptr, 16);
      if (id > 0 && id <= 0x7FF) cfg.reqId = id;
    }
    if (server.hasArg("xtal")) {
      int x = server.arg("xtal").toInt();
      if (x == 8 || x == 16) cfg.xtal = x;
    }
    if (server.hasArg("mode")) setMode(server.arg("mode").toInt());
    saveCfg();
    reapplyXtal = true;  // a tarefa OBD reinicia o MCP2515 com o cristal novo
    logLine("Config: modo %s, velocidade %s, pedido %03X, cristal %u MHz", modeName(cfg.mode),
            rateName(cfg.rate).c_str(), cfg.reqId, cfg.xtal);
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
  server.on("/api/sniff", HTTP_GET, [] { server.send(200, "application/json", Sniff::json()); });
  server.on("/api/sniff/reset", HTTP_POST, [] {
    Sniff::reset();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/sigs", HTTP_GET, [] {
    server.send(200, "application/json", "{\"text\":\"" + jsonEsc2(Sniff::signals()) + "\"}");
  });
  server.on("/api/sigs", HTTP_POST, [] {
    String err;
    if (!saveSignals(server.arg("s"), err)) {
      server.send(200, "application/json", "{\"ok\":false,\"err\":\"" + jsonEsc(err) + "\"}");
      return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/probe", HTTP_POST, [] { server.send(200, "application/json", runCommand(CMD_PROBE)); });
  server.on("/api/trace", HTTP_POST, [] {
    bus.setTrace(server.arg("on") == "1");
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.onNotFound([] { server.sendHeader("Location", "/"); server.send(302); });
  server.begin();
}

// Botão BOOT: toque curto força a conexão, segurar 3 s roda o autoteste.
static void pollButton() {
  static bool wasDown = false, longFired = false;
  static uint32_t downAt = 0;
  bool down = digitalRead(BUTTON_PIN) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) { downAt = now; longFired = false; }
  if (down && !longFired && now - downAt >= BUTTON_LONG_MS) {
    longFired = true;
    reqSelfTest = true;
  }
  if (!down && wasDown && !longFired && now - downAt >= 50) reqForceConnect = true;
  wasDown = down;
}

void setup() {
  Serial.begin(115200);
  streamMx = xSemaphoreCreateMutex();
  logMx = xSemaphoreCreateMutex();
  dataMx = xSemaphoreCreateMutex();
  cmdDone = xSemaphoreCreateBinary();
  loadCfg();
  Sniff::begin(sniffValue);
  {
    prefs.begin("dashcan", true);
    String sg = prefs.getString("sigs", DEFAULT_SIGNALS);
    prefs.end();
    String err;
    if (sg.length() && !Sniff::setSignals(sg, err)) Serial.printf("Sinais salvos inválidos: %s\n", err.c_str());
  }
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT);
  led(false);
#endif

  logLine("===== Fiesta Dash (CAN / MCP2515) =====");
  logLine("SPI: SCK %d, MISO %d, MOSI %d, CS %d @ %u kHz", CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN,
          CAN_CS_PIN, CAN_SPI_HZ / 1000);
  logLine("Config salva: modo %s, velocidade %s, pedido %03X, cristal %u MHz, demonstração %s",
          modeName(cfg.mode), rateName(cfg.rate).c_str(), cfg.reqId, cfg.xtal, cfg.demo ? "LIGADA" : "desligada");
  logLine("BOOT: toque = forçar conexão | segurar 3 s = autoteste do MCP2515");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  logLine("Wi-Fi \"%s\" ativo. Abra http://%s", WIFI_SSID, WiFi.softAPIP().toString().c_str());

#if VBAT_PIN >= 0
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);
#endif
  bus.begin(cfg.xtal);
  setupWeb();
  ws.begin();
  ws.onEvent([](uint8_t num, WStype_t type, uint8_t*, size_t) {
    if (type == WStype_CONNECTED || type == WStype_DISCONNECTED) {
      streamOn = ws.connectedClients() > 0;
      logLine("Transmissão contínua: %u página(s) conectada(s)", ws.connectedClients());
    }
  });
  logLine("Transmissão contínua em ws://%s:%d", WiFi.softAPIP().toString().c_str(), WS_PORT);
  // Tarefa OBD no core 1, prioridade acima do loop().
  xTaskCreatePinnedToCore(obdTask, "obd", 12288, nullptr, 2, nullptr, 1);
}

void loop() {
  server.handleClient();
  pollButton();
  ws.loop();
  static uint32_t lastJ = 0, lastFlush = 0;
  uint32_t now = millis();
  if (streamOn && now - lastJ >= 500) {
    lastJ = now;
    String j = "J " + dataJson() + "\n";
    streamPut(j.c_str(), j.length());
  }
  if (now - lastFlush >= WS_FLUSH_MS) {
    lastFlush = now;
    streamFlush();
  }
  delay(1);
}
