#include "web.h"
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "engine.h"
#include "settings.h"
#include "web_index.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static void sendJsonOk(AsyncWebServerRequest *req, bool ok, const char *msg = "") {
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", msg);
  req->send(ok ? 200 : 409, "application/json", buf);
}

static void liveTask(void *) {
  char buf[1024];
  uint32_t n = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(50));  // 20 Hz
    if (++n % 20 == 0) ws.cleanupClients();
    if (!ws.count()) continue;
    const LiveData &l = live;
    unsigned knMax = 0;
    float krMax = 0;
    for (int i = 0; i < 4; i++) {
      knMax = max<unsigned>(knMax, l.knockLevel[i]);
      krMax = max(krMax, l.knockRetard[i]);
    }
    snprintf(buf, sizeof(buf),
             "{\"rpm\":%.0f,\"map\":%.1f,\"clt\":%.1f,\"iat\":%.1f,\"o2\":%.3f,\"tps\":%.1f,"
             "\"vbat\":%.2f,\"mapDot\":%.0f,\"ve\":%.1f,\"lam\":%.3f,\"warm\":%.1f,\"ae\":%.1f,"
             "\"ase\":%.1f,\"cl\":%.1f,\"req\":%.2f,\"pw\":%.2f,\"duty\":%.1f,\"adv\":%.1f,"
             "\"dwell\":%.2f,\"sync\":%u,\"cam\":%u,\"seq\":%u,\"crank\":%u,\"run\":%u,"
             "\"fan1\":%u,\"fan2\":%u,\"pump\":%u,\"revCut\":%u,\"dfco\":%u,\"clOn\":%u,"
             "\"fault\":%u,\"syncLoss\":%u,\"revs\":%u,\"loop\":%u,\"dirty\":%u,"
             "\"idle\":%.1f,\"idleT\":%.0f,\"idleAdv\":%.1f,\"idleCl\":%u,\"acReq\":%u,\"acOn\":%u,"
             "\"kn\":[%u,%u,%u,%u],\"kr\":[%.1f,%.1f,%.1f,%.1f],\"knMax\":%u,\"krMax\":%.1f,"
             "\"knCount\":%u}",
             l.rpm, l.map, l.clt, l.iat, l.o2, l.tps, l.vbat, l.mapDot, l.ve, l.lambdaTarget,
             l.warmPct, l.aePct, l.afterStartPct, l.clCorrPct, l.reqFuelMs, l.pwMs, l.dutyPct,
             l.advance, l.dwellMs, l.sync, l.camSync, l.sequential, l.cranking, l.running, l.fan1,
             l.fan2, l.pump, l.revCut, l.dfco, l.clActive, l.sensorFault, (unsigned)l.syncLoss,
             (unsigned)l.revs, (unsigned)l.loopUs, cfgDirty ? 1u : 0u, l.idlePos, l.idleTarget,
             l.idleAdvTrim, l.idleClosedLoop, l.acReq, l.acOn, l.knockLevel[0], l.knockLevel[1],
             l.knockLevel[2], l.knockLevel[3], l.knockRetard[0], l.knockRetard[1], l.knockRetard[2],
             l.knockRetard[3], knMax, krMax, (unsigned)l.knockCount);
    ws.textAll(buf);
  }
}

void webStart() {
  WiFi.persistent(false);  // nao gravar credenciais na flash
  WiFi.mode(WIFI_AP);
  WiFi.softAP(cfg.wifiSsid, cfg.wifiPass);
  Serial.printf("Wi-Fi AP '%s'  ->  http://%s\n", cfg.wifiSsid, WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *r = req->beginResponse(200, "text/html", WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
    r->addHeader("Content-Encoding", "gzip");
    req->send(r);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    settingsToJson(cfg, doc.to<JsonObject>());
    xSemaphoreGive(cfgMutex);
    AsyncResponseStream *r = req->beginResponseStream("application/json");
    serializeJson(doc, *r);
    req->send(r);
  });

  // Alteracao parcial: so as chaves enviadas sao aplicadas (vale na hora, em RAM)
  auto *cfgHandler = new AsyncCallbackJsonWebHandler(
      "/api/config", [](AsyncWebServerRequest *req, JsonVariant &json) {
        JsonObjectConst o = json.as<JsonObjectConst>();
        if (o.isNull()) return sendJsonOk(req, false, "json invalido");
        xSemaphoreTake(cfgMutex, portMAX_DELAY);
        settingsFromJson(cfg, o);
        xSemaphoreGive(cfgMutex);
        cfgDirty = true;
        sendJsonOk(req, true);
      });
  cfgHandler->setMaxContentLength(32768);
  server.addHandler(cfgHandler);

  server.on("/api/burn", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (live.rpm > 0 || live.sync) return sendJsonOk(req, false, "desligue o motor para gravar");
    bool ok = settingsSave();
    sendJsonOk(req, ok, ok ? "gravado" : "falha ao gravar");
  });

  server.on("/api/defaults", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (live.rpm > 0) return sendJsonOk(req, false, "desligue o motor");
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    settingsDefaults(cfg);
    xSemaphoreGive(cfgMutex);
    cfgDirty = true;
    sendJsonOk(req, true, "padroes carregados (nao gravados)");
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (live.rpm > 0) return sendJsonOk(req, false, "desligue o motor");
    sendJsonOk(req, true);
    delay(200);
    ESP.restart();
  });

  auto *testHandler = new AsyncCallbackJsonWebHandler(
      "/api/test", [](AsyncWebServerRequest *req, JsonVariant &json) {
        if (live.rpm > 0) return sendJsonOk(req, false, "teste so com motor parado");
        testReq.type = json["type"] | 0;
        testReq.ch = json["ch"] | 0;
        testReq.count = json["count"] | 1;
        testReq.ms = json["ms"] | 3.0f;
        testReq.intervalMs = json["interval"] | 500;
        testReq.pending = true;
        sendJsonOk(req, true);
      });
  server.addHandler(testHandler);

  // Posicao manual da valvula de marcha lenta (vale com motor ligado). pos < 0 = automatico
  auto *idleHandler = new AsyncCallbackJsonWebHandler(
      "/api/idle", [](AsyncWebServerRequest *req, JsonVariant &json) {
        idleOverride = json["pos"] | -1.0f;
        sendJsonOk(req, true);
      });
  server.addHandler(idleHandler);

  server.addHandler(&ws);
  server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "nao encontrado"); });
  server.begin();

  xTaskCreatePinnedToCore(liveTask, "live", 4096, nullptr, 2, nullptr, 0);
}
