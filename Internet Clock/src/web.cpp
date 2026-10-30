#include "web.h"
#include "web_page.h"
#include "settings.h"
#include "sensors.h"
#include "display.h"
#include "network.h"
#include "mqtt_client.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <StreamString.h>
#include <time.h>

static ESP8266WebServer server(80);

static uint32_t rebootAt = 0;
static uint32_t wifiAt = 0;
static bool     uploadAuth = false;
static String   updateError;

static bool auth() {
  if (!S.adminPass[0] || server.authenticate("admin", S.adminPass)) return true;
  server.requestAuthentication(BASIC_AUTH, "Relogio");
  return false;
}

static void sendJson(const JsonDocument& d, int code = 200) {
  String s;
  serializeJson(d, s);
  server.send(code, F("application/json"), s);
}

static void sendOk() {
  server.send(200, F("application/json"), F("{\"ok\":true}"));
}

static void handleRoot() {
  if (!auth()) return;
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML, sizeof(INDEX_HTML) - 1);
}

static float round1(float v) { return roundf(v * 10) / 10; }

static void handleStatus() {
  if (!auth()) return;
  JsonDocument d;
  d["ver"] = FW_VERSION;

  if (Net::timeValid()) {
    char buf[24];
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    d["time"] = buf;
    strftime(buf, sizeof(buf), "%d/%m/%Y", &tm);
    d["date"] = buf;
  }
  d["synced"] = Net::timeSynced();

  if (Sensors::climateValid()) {
    d["temp"] = round1(Sensors::temp());
    d["humi"] = round1(Sensors::humi());
    d["rawT"] = round1(Sensors::rawTemp());
    d["rawH"] = round1(Sensors::rawHumi());
  }
  if (Sensors::luxValid()) d["lux"] = round1(Sensors::lux());
  d["bright"] = Display::brightness();
  d["night"] = Display::nightActive();
  d["screen"] = Display::screenName();

  d["connected"] = Net::connected();
  d["ssid"] = WiFi.SSID();
  d["ip"] = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  d["ap"] = Net::apActive();
  d["apName"] = Net::apName();
  d["mqtt"] = Mqtt::connected();

  d["heap"] = ESP.getFreeHeap();
  d["frag"] = ESP.getHeapFragmentation();
  d["uptime"] = millis() / 1000;
  d["sketch"] = ESP.getSketchSize();
  d["free"] = ESP.getFreeSketchSpace();
  d["reset"] = ESP.getResetReason();
  sendJson(d);
}

static void handleConfigGet() {
  if (!auth()) return;
  JsonDocument d;
  Cfg::toJson(d.to<JsonObject>());
  sendJson(d);
}

static void handleConfigPost() {
  if (!auth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain")) || !d.is<JsonObject>()) {
    server.send(400, F("application/json"), F("{\"error\":\"json\"}"));
    return;
  }
  uint16_t chg = Cfg::fromJson(d.as<JsonObjectConst>());
  bool save = server.arg("save") == "1";

  if (chg & CHG_TIME) Net::applyTime();
  if (chg & CHG_MQTT) Mqtt::reconfigure();
  if (chg & CHG_AUTH) {
    Net::applyAuth();
  }
  if (chg & CHG_WIFI) wifiAt = millis() + 500;  // responde antes de trocar de rede
  if (save) Cfg::save();

  JsonDocument r;
  r["ok"] = true;
  r["reboot"] = (chg & CHG_HOST) != 0;
  sendJson(r);
}

static void handleLayoutGet() {
  if (!auth()) return;
  JsonDocument d;
  d["startLeft"] = L.startLeft;
  d["startTop"] = L.startTop;
  d["zigzag"] = L.zigzag;
  d["columns"] = L.columns;
  d["dma"] = L.dma;
  sendJson(d);
}

// POST /api/layout?save=0|1  {"startLeft":false,"startTop":false,"zigzag":false,"columns":false}
static void handleLayoutPost() {
  if (!auth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain")) || !d.is<JsonObject>()) {
    server.send(400, F("application/json"), F("{\"error\":\"json\"}"));
    return;
  }
  L.startLeft = d["startLeft"] | L.startLeft;
  L.startTop = d["startTop"] | L.startTop;
  L.zigzag = d["zigzag"] | L.zigzag;
  L.columns = d["columns"] | L.columns;
  bool dma = d["dma"] | L.dma;
  bool reboot = dma != L.dma;
  L.dma = dma;  // só tem efeito após reiniciar
  if (server.arg("save") == "1" || reboot) Cfg::saveLayout();
  if (reboot) rebootAt = millis() + 500;
  JsonDocument r;
  r["ok"] = true;
  r["reboot"] = reboot;
  sendJson(r);
}

// POST /api/diag  {"mode":"led"|"orient"|"all"|"off","n":0}
static void handleDiag() {
  if (!auth()) return;
  JsonDocument d;
  deserializeJson(d, server.arg("plain"));
  const char* mode = d["mode"] | "off";
  uint8_t n = d["n"] | 0;  // índice do LED (0..84)
  uint8_t m = !strcmp(mode, "led") ? Display::DIAG_LED
            : !strcmp(mode, "orient") ? Display::DIAG_ORIENT
            : !strcmp(mode, "all") ? Display::DIAG_ALL
                                   : Display::DIAG_OFF;
  Display::diag(m, n, 120000);  // volta ao normal sozinho após 2 min
  sendOk();
}

static void handleScan() {
  if (!auth()) return;
  JsonDocument d;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    d["scanning"] = true;
  } else if (n == WIFI_SCAN_RUNNING) {
    d["scanning"] = true;
  } else {
    JsonArray list = d["list"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      bool dup = false;
      for (JsonObject o : list)
        if (ssid == o["ssid"].as<const char*>()) { dup = true; break; }
      if (dup) continue;
      JsonObject o = list.add<JsonObject>();
      o["ssid"] = ssid;
      o["rssi"] = WiFi.RSSI(i);
      o["lock"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    }
    WiFi.scanDelete();
  }
  sendJson(d);
}

static void handleAction(void (*fn)()) {
  if (!auth()) return;
  fn();
  sendOk();
}

static void setUpdateError() {
  StreamString s;
  Update.printError(s);
  updateError = s;
  updateError.trim();
}

static void handleUpload() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START: {
      updateError = "";
      uploadAuth = !S.adminPass[0] || server.authenticate("admin", S.adminPass);
      if (!uploadAuth) return;
      WiFiUDP::stopAll();
      uint32_t space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(space, U_FLASH)) setUpdateError();
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (!uploadAuth || Update.hasError()) return;
      if (Update.write(up.buf, up.currentSize) != up.currentSize) setUpdateError();
      else if (up.contentLength) Display::showProgress(min<uint64_t>(up.totalSize * 100ULL / up.contentLength, 99));
      break;
    case UPLOAD_FILE_END:
      if (uploadAuth && !Update.end(true)) setUpdateError();
      break;
    case UPLOAD_FILE_ABORTED:
      Update.end();
      updateError = F("upload interrompido");
      break;
  }
}

static void handleUpdateDone() {
  if (!auth()) return;
  JsonDocument d;
  bool ok = updateError.isEmpty() && Update.isFinished();
  d["ok"] = ok;
  if (!ok) d["error"] = updateError.length() ? updateError : String(F("nenhum firmware recebido"));
  sendJson(d);
  if (ok) rebootAt = millis() + 500;
}

static bool isIp(const String& s) {
  for (char c : s)
    if (c != '.' && (c < '0' || c > '9')) return false;
  return s.length() > 0;
}

static void handleNotFound() {
  // Portal cativo: qualquer domínio leva à página de configuração
  if (Net::apActive() && server.client().localIP() == WiFi.softAPIP() && !isIp(server.hostHeader())) {
    server.sendHeader(F("Location"), F("http://192.168.4.1/"), true);
    server.send(302, F("text/plain"), "");
    return;
  }
  server.send(404, F("text/plain"), F("Not found"));
}

void Web::begin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/layout", HTTP_GET, handleLayoutGet);
  server.on("/api/layout", HTTP_POST, handleLayoutPost);
  server.on("/api/diag", HTTP_POST, handleDiag);
  server.on("/api/test", HTTP_POST, [] { handleAction([] { Display::test(5000); }); });
  server.on("/api/next", HTTP_POST, [] { handleAction([] { Display::nextScreen(); }); });
  server.on("/api/reboot", HTTP_POST, [] { handleAction([] { rebootAt = millis() + 500; }); });
  server.on("/api/factory", HTTP_POST, [] {
    handleAction([] {
      Cfg::defaults();
      Cfg::save();
      rebootAt = millis() + 500;
    });
  });
  server.onNotFound(handleNotFound);

  // OTA pela página: POST multipart com o firmware.bin
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpload);

  server.begin();
}

void Web::loop() {
  server.handleClient();
  uint32_t now = millis();
  if (wifiAt && (int32_t)(now - wifiAt) >= 0) {
    wifiAt = 0;
    Net::applyWifi();
  }
  if (rebootAt && (int32_t)(now - rebootAt) >= 0) ESP.restart();
}
