#include "web.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "config.h"
#include "plc.h"
#include "settings.h"

#define FW_VERSION "0.1.0"
#define STATE_PERIOD_MS 200

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// Comandos do WebSocket são executados no loop(), fora da tarefa do AsyncTCP,
// porque alguns gravam na flash (NVS).
struct WsCmd {
  enum Kind : uint8_t { RUN, STOP, FORCE, SETD, CLRMAX } kind;
  char area;
  uint8_t idx;
  int32_t value;
};
static QueueHandle_t cmdQueue;
static uint32_t restartAt = 0;

static void sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
  String s;
  serializeJson(doc, s);
  req->send(code, "application/json", s);
}

static void sendError(AsyncWebServerRequest* req, int code, const String& msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  sendJson(req, code, doc);
}

// Junta o corpo da requisição (que chega em pedaços) num buffer.
// Retorna true quando o corpo está completo em req->_tempObject.
static bool collectBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (total > PLC_MAX_PROGRAM_BYTES) return false;
  if (index == 0) {
    req->_tempObject = malloc(total + 1);
    if (!req->_tempObject) {
      sendError(req, 500, "sem memória");
      return false;
    }
  }
  if (!req->_tempObject) return false;
  memcpy((uint8_t*)req->_tempObject + index, data, len);
  if (index + len < total) return false;
  ((char*)req->_tempObject)[total] = '\0';
  return true;
}

// Chamado ao fim da requisição; a resposta normal já foi enviada pelo handler do corpo.
static void rejectEmptyBody(AsyncWebServerRequest* req) {
  if (req->contentLength() == 0) sendError(req, 400, "corpo vazio");
}

static void handleProgramBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (total > PLC_MAX_PROGRAM_BYTES) {
    if (index == 0) sendError(req, 413, "programa grande demais");
    return;
  }
  if (!collectBody(req, data, len, index, total)) return;
  const char* body = (const char*)req->_tempObject;

  String err;
  if (!g_plc.loadProgram(body, total, err)) {
    sendError(req, 400, err);
    return;
  }
  File f = LittleFS.open(PROGRAM_FILE, "w");
  if (!f || f.write((const uint8_t*)body, total) != total) {
    sendError(req, 500, "programa carregado, mas falhou ao gravar na flash");
    return;
  }
  f.close();
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(req, 200, doc);
}

static void handleConfigBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (total > 1024) {
    if (index == 0) sendError(req, 413, "requisição grande demais");
    return;
  }
  if (!collectBody(req, data, len, index, total)) return;
  JsonDocument in;
  if (deserializeJson(in, (const char*)req->_tempObject, total)) {
    sendError(req, 400, "JSON inválido");
    return;
  }
  bool needRestart = false;
  if (in["ssid"].is<const char*>()) {
    String ssid = in["ssid"].as<String>();
    needRestart |= ssid != g_settings.ssid;
    g_settings.ssid = ssid;
  }
  // Senha só é trocada se enviada (a interface não recebe a senha atual).
  if (in["pass"].is<const char*>()) {
    g_settings.pass = in["pass"].as<String>();
    needRestart = true;
  }
  if (in["hostname"].is<const char*>()) {
    String h = in["hostname"].as<String>();
    if (h.length() == 0 || h.length() > 31) {
      sendError(req, 400, "hostname inválido");
      return;
    }
    needRestart |= h != g_settings.hostname;
    g_settings.hostname = h;
  }
  if (in["diInvert"].is<uint32_t>()) g_settings.diInvert = in["diInvert"].as<uint32_t>() & ((1UL << PLC_NUM_DI) - 1);
  if (in["diPullUp"].is<bool>()) {
    bool pu = in["diPullUp"];
    needRestart |= pu != g_settings.diPullUp;
    g_settings.diPullUp = pu;
  }
  settingsSave();
  if (needRestart) restartAt = millis() + 1500;
  JsonDocument doc;
  doc["ok"] = true;
  doc["restart"] = needRestart;
  sendJson(req, 200, doc);
}

static void handleWsMessage(void* arg, uint8_t* data, size_t len) {
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)data, len)) return;
  const char* c = doc["cmd"] | "";
  WsCmd cmd{};
  if (!strcmp(c, "run")) cmd.kind = WsCmd::RUN;
  else if (!strcmp(c, "stop")) cmd.kind = WsCmd::STOP;
  else if (!strcmp(c, "clrmax")) cmd.kind = WsCmd::CLRMAX;
  else if (!strcmp(c, "force")) {
    const char* a = doc["a"] | "";
    int idx = doc["i"] | -1;
    int max = a[0] == 'I' ? PLC_NUM_DI : a[0] == 'Q' ? PLC_NUM_DO : 0;
    if (idx < 0 || idx >= max) return;
    cmd.kind = WsCmd::FORCE;
    cmd.area = a[0];
    cmd.idx = (uint8_t)idx;
    cmd.value = doc["m"] | -1;  // -1 libera, 0/1 força
  } else if (!strcmp(c, "setd")) {
    int idx = doc["i"] | -1;
    if (idx < 0 || idx >= PLC_NUM_D || !doc["v"].is<long>()) return;
    cmd.kind = WsCmd::SETD;
    cmd.idx = (uint8_t)idx;
    cmd.value = doc["v"].as<long>();
  } else {
    return;
  }
  xQueueSend(cmdQueue, &cmd, 0);
}

static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_DATA) handleWsMessage(arg, data, len);
}

static void broadcastState() {
  static PlcSnapshot s;  // estático: grande demais para a pilha do loop
  g_plc.snapshot(s);
  JsonDocument doc;
  doc["run"] = s.running;
  doc["scan"] = s.scanUs;
  doc["smax"] = s.scanMaxUs;
  doc["i"] = s.img.i;
  doc["q"] = s.img.q;
  doc["qp"] = s.qPhys;
  doc["s"] = s.img.s;
  doc["fie"] = s.forceIEn;
  doc["fiv"] = s.forceIVal;
  doc["fqe"] = s.forceQEn;
  doc["fqv"] = s.forceQVal;
  JsonArray m = doc["m"].to<JsonArray>();
  for (uint32_t w : s.img.m) m.add(w);
  JsonArray ai = doc["ai"].to<JsonArray>();
  for (int32_t v : s.img.ai) ai.add(v);
  JsonArray d = doc["d"].to<JsonArray>();
  for (int32_t v : s.img.d) d.add(v);
  JsonArray t = doc["t"].to<JsonArray>();
  for (const PlcTimer& x : s.img.t) {
    JsonArray e = t.add<JsonArray>();
    e.add(x.q ? 1 : 0);
    e.add(x.et);
    e.add(x.pt);
  }
  JsonArray c = doc["c"].to<JsonArray>();
  for (const PlcCounter& x : s.img.c) {
    JsonArray e = c.add<JsonArray>();
    e.add(x.q ? 1 : 0);
    e.add(x.cv);
    e.add(x.pv);
  }
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

void webBegin() {
  cmdQueue = xQueueCreate(16, sizeof(WsCmd));

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* req) {
    PlcSnapshot* s = new PlcSnapshot;
    g_plc.snapshot(*s);
    JsonDocument doc;
    doc["version"] = FW_VERSION;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["rungs"] = s->rungCount;
    doc["instr"] = s->instrCount;
    delete s;
    JsonObject n = doc["counts"].to<JsonObject>();
    n["I"] = PLC_NUM_DI;
    n["Q"] = PLC_NUM_DO;
    n["AI"] = PLC_NUM_AI;
    n["M"] = PLC_NUM_M;
    n["D"] = PLC_NUM_D;
    n["T"] = PLC_NUM_T;
    n["C"] = PLC_NUM_C;
    n["S"] = PLC_NUM_S;
    sendJson(req, 200, doc);
  });

  server.on("/api/program", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (LittleFS.exists(PROGRAM_FILE)) {
      req->send(LittleFS, PROGRAM_FILE, "application/json");
    } else {
      req->send(200, "application/json", "{\"rungs\":[],\"symbols\":{}}");
    }
  });
  server.on("/api/program", HTTP_POST, rejectEmptyBody, nullptr, handleProgramBody);

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["ssid"] = g_settings.ssid;
    doc["hostname"] = g_settings.hostname;
    doc["diInvert"] = g_settings.diInvert;
    doc["diPullUp"] = g_settings.diPullUp;
    doc["mode"] = WiFi.getMode() == WIFI_AP ? "ap" : "sta";
    doc["ip"] = WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    sendJson(req, 200, doc);
  });
  server.on("/api/config", HTTP_POST, rejectEmptyBody, nullptr, handleConfigBody);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
  server.onNotFound([](AsyncWebServerRequest* req) { req->send(404, "text/plain", "Nao encontrado"); });
  server.begin();
}

void webLoop() {
  WsCmd cmd;
  while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
    switch (cmd.kind) {
      case WsCmd::RUN: g_plc.setRunning(true); break;
      case WsCmd::STOP: g_plc.setRunning(false); break;
      case WsCmd::CLRMAX: g_plc.resetScanMax(); break;
      case WsCmd::FORCE: g_plc.setForce(cmd.area, cmd.idx, (int8_t)cmd.value); break;
      case WsCmd::SETD: g_plc.setD(cmd.idx, cmd.value); break;
    }
  }

  static uint32_t lastState = 0, lastCleanup = 0;
  uint32_t now = millis();
  if (now - lastState >= STATE_PERIOD_MS) {
    lastState = now;
    if (ws.count() > 0) broadcastState();
  }
  if (now - lastCleanup >= 1000) {
    lastCleanup = now;
    ws.cleanupClients();
  }
  if (restartAt && (int32_t)(now - restartAt) >= 0) ESP.restart();
}
