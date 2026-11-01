// ACRemote no ESP32: substitui o módulo Wi-Fi NIU-LIGHT do ar-condicionado Electrolux e controla o ar
// por uma página web. Também escuta a serial, descobre baud e formato e mostra os quadros.
// Só transmite com o controle ativo; desligado, apenas lê as duas linhas.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>
#include "config.h"
#include "capture.h"
#include "ac_link.h"
#include "ac_schedule.h"
#include "web_index.h"
#include "web_control.h"

static WebServer server(80);
static WebSocketsServer ws(WS_PORT);
static Preferences prefs;

// O gerador de teste liga e desliga as UARTs na própria tarefa, nunca durante um envio.
static volatile bool testWanted = false;
static volatile uint32_t testBaud = 9600;
static volatile int testFmt = 0;
static volatile bool testRunning = false;

static String wifiSsid, wifiPass;
static uint32_t wifiLastTry = 0, wifiUpAt = 0, wifiDownAt = 0;
static bool apOn = false;
static bool updateAllowed = false;
static uint32_t bootId = 0;  // a página usa para notar que o relógio voltou a zero

static uint32_t serialConfig(const UartFormat& f) {
  const bool two = f.stopBits == 2;
  if (f.parity == 'E') return two ? SERIAL_8E2 : SERIAL_8E1;
  if (f.parity == 'O') return two ? SERIAL_8O2 : SERIAL_8O1;
  return two ? SERIAL_8N2 : SERIAL_8N1;
}

static size_t buildTestFrame(uint8_t* out, uint8_t type, uint8_t counter) {
  const uint8_t body[] = {0xAA, 0x0A, type, 0x03, counter, 0x18, 0x55, 0x00, 0xFF};
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(body); i++) {
    out[i] = body[i];
    sum += body[i];
  }
  out[sizeof(body)] = sum;
  return sizeof(body) + 1;
}

static void saveConfig();

static void setControl(bool on) {
  if (on) {
    Capture::setManual(AC_BAUD, AC_FORMAT);
    Capture::setGap(AC_GAP_MS);
    saveConfig();
  }
  AcLink::setEnabled(on);
  prefs.putBool("ctl", on);
}

static void stopTestUarts() {
  Serial1.end();
  Serial2.end();
  pinMode(PIN_TEST_A, INPUT);
  pinMode(PIN_TEST_B, INPUT);
  testRunning = false;
}

static void testTask(void*) {
  uint8_t counter = 0;
  uint8_t frame[16];
  for (;;) {
    if (!testWanted) {
      if (testRunning) stopTestUarts();
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (!testRunning) {
      const uint32_t mode = serialConfig(UART_FORMATS[testFmt]);
      Serial1.begin(testBaud, mode, -1, PIN_TEST_A);
      Serial2.begin(testBaud, mode, -1, PIN_TEST_B);
      testRunning = true;
    }
    size_t n = buildTestFrame(frame, 0x40, counter);
    Serial1.write(frame, n);
    Serial1.flush();
    vTaskDelay(pdMS_TO_TICKS(60));
    n = buildTestFrame(frame, 0xC0, counter);
    Serial2.write(frame, n);
    Serial2.flush();
    counter++;
    vTaskDelay(pdMS_TO_TICKS(400));
  }
}

static void saveConfig() {
  const CaptureConfig c = Capture::config();
  prefs.putUInt("baud", c.baud);
  prefs.putChar("fmt", c.fmt);
  prefs.putUShort("gap", c.gapMs);
  prefs.putBool("auto", c.autoLock);
}

static String jsonEsc(const String& in) {
  String o;
  o.reserve(in.length() + 4);
  for (char c : in) {
    if (c == '"' || c == '\\') o += '\\';
    if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

static void connectWifi() {
  wifiLastTry = millis();
  if (wifiSsid.isEmpty()) return;
  WiFi.disconnect();
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
}

static void setAp(bool on) {
  if (on == apOn) return;
  apOn = on;
  if (on) WiFi.softAP(WIFI_SSID, WIFI_PASS);
  else WiFi.softAPdisconnect(true);
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
  if (!wifiSsid.isEmpty() && now - wifiLastTry >= WIFI_RETRY_MS) connectWifi();
}

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "energia ligada";
    case ESP_RST_EXT: return "pino de reset";
    case ESP_RST_SW: return "reinício por software";
    case ESP_RST_PANIC: return "erro do firmware";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "queda de tensão";
    default: return "desconhecido";
  }
}

static String statusJson() {
  const CaptureConfig c = Capture::config();
  String o;
  o.reserve(2400);
  o = "{\"t\":\"st\",\"u\":";
  o += (double)Capture::nowUs();
  o += ",\"baud\":";
  o += c.baud;
  o += ",\"fmt\":\"";
  o += UART_FORMATS[c.fmt].name;
  o += "\",\"gap\":";
  o += c.gapMs;
  o += ",\"auto\":";
  o += c.autoLock ? "true" : "false";
  o += ",\"test\":";
  o += testWanted ? "true" : "false";
  o += ",\"ac\":";
  o += AcLink::json();
  o += ",\"boot\":";
  o += bootId;
  o += ",\"reset\":\"";
  o += resetReason();
  o += "\",\"fw\":\"" FW_VERSION "\",\"ssid\":\"";
  o += jsonEsc(wifiSsid);
  o += "\",\"sta\":";
  o += WiFi.status() == WL_CONNECTED ? "true" : "false";
  o += ",\"ip\":\"";
  o += WiFi.localIP().toString();
  o += "\",\"rssi\":";
  o += WiFi.RSSI();
  o += ",\"heap\":";
  o += ESP.getFreeHeap();
  o += ",\"ch\":[";
  for (int i = 0; i < CH_COUNT; i++) {
    static ChannelStats s;
    Capture::stats(i, s);
    if (i) o += ',';
    o += "{\"lvl\":" + String(s.level ? 1 : 0);
    o += ",\"edges\":" + String(s.edges);
    o += ",\"bursts\":" + String(s.bursts);
    o += ",\"frames\":" + String(s.frames);
    o += ",\"bytes\":" + String(s.bytes);
    o += ",\"ferr\":" + String(s.framing);
    o += ",\"perr\":" + String(s.parity);
    o += ",\"ovf\":" + String(s.overflows);
    o += ",\"drop\":" + String(s.droppedFrames);
    o += ",\"samples\":" + String(s.sampleEdges);
    o += ",\"est\":" + String(s.analysis.estBaud);
    o += ",\"pulse\":" + String(s.analysis.minPulseUs, 2);
    o += ",\"hist\":[";
    for (int k = 0; k < PULSE_BUCKETS; k++) {
      if (k) o += ',';
      o += s.hist[k];
    }
    o += "],\"cand\":[";
    for (int k = 0; k < s.analysis.count; k++) {
      const UartCandidate& a = s.analysis.top[k];
      if (k) o += ',';
      o += "[" + String(a.baud) + ",\"" + UART_FORMATS[a.fmt].name + "\",";
      o += String(a.score * 100, 1) + "," + String(a.good) + "," + String(a.bad) + "]";
    }
    o += "]}";
  }
  o += "]}";
  return o;
}

// Só retira quadros com alguém conectado: assim o boot do ar fica guardado até a página abrir.
static void flushFrames() {
  static uint32_t last = 0;
  if (millis() - last < WS_FLUSH_MS || !ws.connectedClients()) return;
  last = millis();

  static const char HEX_CHARS[] = "0123456789ABCDEF";
  String o;
  o.reserve(WS_BATCH_BYTES + FRAME_MAX_BYTES * 2 + 64);
  o = "{\"t\":\"f\",\"boot\":";
  o += bootId;
  o += ",\"l\":[";
  bool any = false;
  while (o.length() < WS_BATCH_BYTES) {
    size_t size;
    uint8_t* item = (uint8_t*)xRingbufferReceive(Capture::frames(), &size, 0);
    if (!item) break;
    FrameHeader h;
    memcpy(&h, item, sizeof(h));
    if (any) o += ',';
    any = true;
    o += '[';
    o += (int)h.ch;
    o += ',';
    o += String((double)h.tUs, 0);
    o += ",\"";
    for (size_t i = 0; i < h.len; i++) {
      const uint8_t b = item[sizeof(h) + i];
      o += HEX_CHARS[b >> 4];
      o += HEX_CHARS[b & 15];
    }
    o += "\",";
    o += (int)h.errors;
    o += ']';
    vRingbufferReturnItem(Capture::frames(), item);
  }
  if (!any) return;
  o += "]}";
  ws.broadcastTXT(o);
}

static void setupRoutes() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", CONTROL_HTML); });
  server.on("/monitor", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/ajustes", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/api/ac/state", HTTP_GET, [] { server.send(200, "application/json", AcLink::json()); });
  server.on("/api/scope", HTTP_GET, [] { server.send(200, "application/json", Capture::scopeJson()); });
  server.on("/api/config", HTTP_POST, [] {
    if (server.hasArg("gap")) Capture::setGap(constrain(server.arg("gap").toInt(), 1, 2000));
    if (server.hasArg("baud")) {
      const long baud = server.arg("baud").toInt();
      const int fmt = server.arg("fmt").toInt();
      if (baud == 0) Capture::setAuto();
      else if (baud >= 50 && baud <= 250000 && fmt >= 0 && fmt < UART_FORMAT_COUNT) {
        Capture::setManual(baud, fmt);
      } else {
        server.send(400, "application/json", "{\"ok\":false,\"err\":\"baud ou formato inválido\"}");
        return;
      }
    }
    saveConfig();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/analyze", HTTP_POST, [] {
    Capture::resetAnalysis();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/test", HTTP_POST, [] {
    const long baud = server.arg("baud").toInt();
    const int fmt = server.arg("fmt").toInt();
    const bool on = server.arg("on") == "1";
    if (on && AcLink::enabled()) {
      server.send(409, "application/json", "{\"ok\":false,\"err\":\"desligue o controle\"}");
      return;
    }
    if (on && (baud < 300 || baud > 230400 || fmt < 0 || fmt >= UART_FORMAT_COUNT)) {
      server.send(400, "application/json", "{\"ok\":false,\"err\":\"baud ou formato inválido\"}");
      return;
    }
    testWanted = false;
    while (on && testRunning) delay(5);
    if (on) {
      testBaud = baud;
      testFmt = fmt;
      testWanted = true;
      Capture::setAuto();  // o teste só vale se a detecção partir do zero
    }
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/ac/enable", HTTP_POST, [] {
    const bool on = server.arg("on") == "1";
    if (on && (testWanted || testRunning)) {
      server.send(409, "application/json", "{\"ok\":false,\"err\":\"desligue o gerador\"}");
      return;
    }
    setControl(on);
    server.send(200, "application/json", "{\"ok\":true}");
  });
  // Lista fechada. Fora das capturas ficam só a faixa de temperatura abaixo de 22 e os timers
  // acima de 1 h, que usam o mesmo formato com outro número.
  server.on("/api/ac", HTTP_POST, [] {
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
  });
  server.on("/api/schedule", HTTP_GET, [] { server.send(200, "application/json", AcSchedule::json()); });
  server.on("/api/schedule", HTTP_POST, [] {
    const bool ok = AcSchedule::update([](const char* name) { return server.arg(name); });
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/wifi", HTTP_POST, [] {
    wifiSsid = server.arg("ssid");
    wifiPass = server.arg("pass");
    prefs.putString("ssid", wifiSsid);
    prefs.putString("wpass", wifiPass);
    server.send(200, "application/json", "{\"ok\":true}");
    if (wifiSsid.isEmpty()) {
      WiFi.disconnect();
      setAp(true);
    } else {
      connectWifi();
    }
  });
  server.on("/api/wifi/scan", HTTP_GET, [] {
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
  });
  server.on("/update", HTTP_POST, [] {
    const bool ok = updateAllowed && !Update.hasError();
    server.send(ok ? 200 : (updateAllowed ? 500 : 403), "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false}");
    if (!ok) return;
    delay(500);
    ESP.restart();
  }, [] {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
      updateAllowed = server.arg("pass") == OTA_PASS;
      if (!updateAllowed) return;
      testWanted = false;
      Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    } else if (!updateAllowed) {
      return;
    } else if (up.status == UPLOAD_FILE_WRITE) {
      Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
      Update.end(true);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
      Update.abort();
    }
  });
  server.onNotFound([] {
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  bootId = esp_random();
  prefs.begin("analisador", false);  // nome antigo mantido: guarda o Wi-Fi e a configuração já salvos
  CaptureConfig cfg;
  cfg.baud = prefs.getUInt("baud", 0);
  cfg.fmt = constrain(prefs.getChar("fmt", 0), 0, UART_FORMAT_COUNT - 1);
  cfg.gapMs = prefs.getUShort("gap", DEFAULT_GAP_MS);
  cfg.autoLock = prefs.getBool("auto", true);

  Capture::setFrameHook(AcLink::onFrames);
  Capture::begin(cfg);
  AcLink::begin(prefs.getBool("ctl", false));

  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("wpass", "");
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  setAp(wifiSsid.isEmpty());
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  connectWifi();
  AcSchedule::begin();

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([] { testWanted = false; });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);

  ws.begin();
  ws.enableHeartbeat(WS_PING_MS, WS_PONG_MS, WS_PING_MISSES);
  ws.onEvent([](uint8_t, WStype_t, uint8_t*, size_t) {});
  setupRoutes();
  xTaskCreatePinnedToCore(testTask, "test", 3072, nullptr, 1, nullptr, 0);
  Serial.printf("Firmware %s, http://%s.local\n", FW_VERSION, HOSTNAME);
}

void loop() {
  server.handleClient();
  ws.loop();
  ArduinoOTA.handle();
  wifiTick();
  AcSchedule::loop();
  flushFrames();
  if (Capture::takeConfigChanged()) saveConfig();

  static uint32_t lastStatus = 0;
  if (millis() - lastStatus >= 1000 && ws.connectedClients()) {
    lastStatus = millis();
    String st = statusJson();
    ws.broadcastTXT(st);
  }
  delay(1);
}
