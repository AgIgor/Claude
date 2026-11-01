// ACRemote no ESP-01: controle local do ar Electrolux por página web, em paralelo com o módulo Wi-Fi
// NIU-LIGHT. Sem monitor da serial, que depende da captura de bordas do ESP32.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include "ac_link.h"
#include "ac_schedule.h"
#include "config_esp01.h"
#include "pages_gz.h"

static const uint32_t WIFI_MAGIC = 0xAC0E5801;

struct WifiSaved {
  uint32_t magic;
  char ssid[33];
  char pass[65];
};

static ESP8266WebServer server(80);
static WifiSaved wifi;
static uint32_t wifiLastTry = 0, wifiUpAt = 0, wifiDownAt = 0;
static bool apOn = false, updateAllowed = false;

static void loadWifi() {
  EEPROM.begin(EEPROM_BYTES);
  EEPROM.get(0, wifi);
  if (wifi.magic != WIFI_MAGIC) memset(&wifi, 0, sizeof(wifi));
  wifi.ssid[sizeof(wifi.ssid) - 1] = 0;
  wifi.pass[sizeof(wifi.pass) - 1] = 0;
}

static void saveWifi(const String& ssid, const String& pass) {
  memset(&wifi, 0, sizeof(wifi));
  wifi.magic = WIFI_MAGIC;
  strlcpy(wifi.ssid, ssid.c_str(), sizeof(wifi.ssid));
  strlcpy(wifi.pass, pass.c_str(), sizeof(wifi.pass));
  EEPROM.put(0, wifi);
  EEPROM.commit();
}

static void connectWifi() {
  wifiLastTry = millis();
  if (!wifi.ssid[0]) return;
  WiFi.disconnect();
  WiFi.begin(wifi.ssid, wifi.pass);
}

static void setAp(bool on) {
  if (on == apOn) return;
  apOn = on;
  if (on) {
    WiFi.mode(wifi.ssid[0] ? WIFI_AP_STA : WIFI_AP);  // sem roteador, a estação só atrapalharia
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
  } else {
    WiFi.softAPdisconnect(true);
  }
}

// A rede própria é só reserva: abre se o roteador não responder e fecha depois que ele estabilizar,
// com tempo para a página mostrar o IP novo. Reconecta devagar: procurar o roteador a deixa instável.
static void wifiTick() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    wifiDownAt = now;
    if (apOn && now - wifiUpAt >= WIFI_AP_CLOSE_MS) setAp(false);
    return;
  }
  wifiUpAt = now;
  if (now - wifiDownAt >= WIFI_AP_OPEN_MS) setAp(true);
  if (wifi.ssid[0] && now - wifiLastTry >= WIFI_RETRY_MS) connectWifi();
}

static String jsonEsc(const String& s) {
  String o;
  for (const char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

static String netJson() {
  const bool sta = WiFi.status() == WL_CONNECTED;
  String o = "{\"fw\":\"" FW_VERSION "\",\"ssid\":\"";
  o += jsonEsc(wifi.ssid);
  o += "\",\"sta\":";
  o += sta ? "true" : "false";
  o += ",\"ip\":\"";
  o += (sta ? WiFi.localIP() : WiFi.softAPIP()).toString();
  o += "\",\"rssi\":";
  o += WiFi.RSSI();
  o += ",\"reset\":\"";
  o += jsonEsc(ESP.getResetReason());
  o += "\",\"up\":";
  o += millis() / 1000;
  o += '}';
  return o;
}

static void handleCommand() {
  const String cmd = server.arg("cmd");
  const int v = server.arg("v").toInt();
  bool ok = false;
  if (cmd == "power" && (v == 0 || v == 1)) ok = AcLink::power(v);
  else if (cmd == "mode" && (v == 1 || v == 2 || v == 3 || v == 6)) ok = AcLink::mode(v);
  else if (cmd == "fan" && (v == 1 || v == 2 || v == 4 || v == 8)) ok = AcLink::fan(v);
  else if (cmd == "temp" && v >= AC_TEMP_MIN && v <= AC_TEMP_MAX) ok = AcLink::setpoint(v);
  else if (cmd == "fixed" && v >= 1 && v <= 6) ok = AcLink::direction(false, v);
  else if (cmd == "swing" && v >= 0 && v <= 3) ok = AcLink::direction(true, v);
  else if (cmd == "sleep" && (v == 0 || v == 1)) ok = AcLink::sleep(v);
  else if (cmd == "display" && (v == 0 || v == 1)) ok = AcLink::display(v);
  else if (cmd == "mute" && (v == 0 || v == 1)) ok = AcLink::mute(v);
  else if (cmd == "indirect" && v == 1) ok = AcLink::indirect();
  else if (cmd == "sense" && v == 1) ok = AcLink::autoSense();
  else if (cmd == "timer" && v >= 1 && v <= AC_TIMER_MAX_H) ok = AcLink::timerHours(v);
  else if (cmd == "preset") ok = AcSchedule::apply(v);
  server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleScan() {
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"busy\":true}");
    return;
  }
  if (n < 0) {
    WiFi.scanNetworks(true);
    server.send(200, "application/json", "{\"busy\":true}");
    return;
  }
  String o = "{\"nets\":[";
  for (int i = 0; i < n; i++) {
    if (i) o += ',';
    o += "[\"" + jsonEsc(WiFi.SSID(i)) + "\"," + String(WiFi.RSSI(i)) + "]";
  }
  o += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", o);
}

static void sendPage(const uint8_t* gz, size_t size) {
  StreamConstPtr page(gz, size);
  page.setTimeout(HTTP_PAGE_TIMEOUT_MS);
  server.sendHeader("Content-Encoding", "gzip");
  server.send(200, "text/html; charset=utf-8", &page, size);
}

static void setupRoutes() {
  server.on("/", HTTP_GET, [] { sendPage(CONTROL_GZ, sizeof(CONTROL_GZ)); });
  server.on("/ajustes", HTTP_GET, [] { sendPage(SETUP_GZ, sizeof(SETUP_GZ)); });
  server.on("/api/ac/state", HTTP_GET, [] { server.send(200, "application/json", AcLink::json()); });
  server.on("/api/ac", HTTP_POST, handleCommand);
  server.on("/api/schedule", HTTP_GET, [] { server.send(200, "application/json", AcSchedule::json()); });
  server.on("/api/schedule", HTTP_POST, [] {
    const bool ok = AcSchedule::update([](const char* name) { return server.arg(name); });
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/net", HTTP_GET, [] { server.send(200, "application/json", netJson()); });
  server.on("/api/txtest", HTTP_GET, [] {
    AcLink::testTxPin();
    server.send(200, "text/plain; charset=utf-8", "GPIO1 (TX) alternando a cada 3 s por 60 s");
  });
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, [] {
    saveWifi(server.arg("ssid"), server.arg("pass"));
    server.send(200, "application/json", "{\"ok\":true}");
    if (wifi.ssid[0]) {
      connectWifi();
    } else {
      WiFi.disconnect();
      setAp(true);
    }
  });
  server.on("/update", HTTP_POST, [] {
    const bool ok = updateAllowed && !Update.hasError();
    if (!ok) {
      server.send(updateAllowed ? 500 : 403, "application/json",
                  "{\"ok\":false,\"err\":\"" + jsonEsc(Update.getErrorString()) + "\"}");
      return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
  }, [] {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
      updateAllowed = server.arg("pass") == OTA_PASS;
      if (!updateAllowed) return;
      WiFiUDP::stopAll();
      Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
    } else if (!updateAllowed) {
      return;
    } else if (up.status == UPLOAD_FILE_WRITE) {
      Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
      Update.end(true);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
      Update.end();
    }
  });
  server.onNotFound([] {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });
}

void setup() {
  AcLink::begin();
  loadWifi();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(false);  // ignora a rede que outro firmware tenha deixado salva na flash
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  WiFi.hostname(HOSTNAME);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  setAp(!wifi.ssid[0]);
  connectWifi();

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);

  AcSchedule::begin();
  setupRoutes();
  server.begin();
}

void loop() {
  AcLink::loop();
  server.handleClient();
  AcLink::loop();
  AcSchedule::loop();
  ArduinoOTA.handle();
  wifiTick();
  delay(1);
}
