#include "web.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <math.h>

#include "app.h"
#include "hw_io.h"

namespace web {
namespace {

constexpr uint32_t STATE_PERIOD_MS = 100;  // 10 atualizações/s para a página
constexpr size_t LOG_SYNC_COUNT = 40;      // eventos enviados a quem acaba de conectar

const IPAddress AP_IP(192, 168, 4, 1);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

// Clientes recém-conectados aguardando a sincronização completa (feita no loop).
portMUX_TYPE pending_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t pending_ids[8];
size_t pending_count = 0;

uint32_t log_cursor = 0;
uint32_t sent_config_version = 0;

double r3(double v) { return round(v * 1000.0) / 1000.0; }

const char* luxSrc(const app::IoSettings& io) { return io.lux_from_ldr ? "ldr" : "web"; }

void post(app::CmdType type, double value = 0, int index = 0, const char* key = nullptr) {
    app::Command c;
    c.type = type;
    c.value = value;
    c.index = static_cast<int8_t>(index);
    if (key) strlcpy(c.key, key, sizeof(c.key));
    app::postCommand(c);
}

// ------------------------------------------------------------------ comandos da página
void handleMessage(const char* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return;
    const char* cmd = doc["cmd"] | "";

    if (!strcmp(cmd, "lock")) post(app::CmdType::LOCK);
    else if (!strcmp(cmd, "unlock")) post(app::CmdType::UNLOCK);
    else if (!strcmp(cmd, "flash")) post(app::CmdType::FLASH);
    else if (!strcmp(cmd, "reset")) post(app::CmdType::RESET);
    else if (!strcmp(cmd, "defaults")) post(app::CmdType::CONFIG_DEFAULTS);
    else if (!strcmp(cmd, "input")) {
        if (doc["ign"].is<int>()) post(app::CmdType::SET_IGN, doc["ign"].as<int>());
        if (doc["sw"].is<const char*>()) {
            bcm::LightSwitch sw;
            if (bcm::parseSwitch(doc["sw"], &sw)) post(app::CmdType::SET_SWITCH, static_cast<int>(sw));
        }
        if (doc["lux"].is<double>()) post(app::CmdType::SET_LUX, doc["lux"].as<double>());
        if (doc["batt"].is<double>()) post(app::CmdType::SET_BATT, doc["batt"].as<double>());
        if (doc["door"].is<JsonArray>()) {
            post(app::CmdType::SET_DOOR, doc["door"][1].as<bool>() ? 1 : 0, doc["door"][0].as<int>());
        }
    } else if (!strcmp(cmd, "config")) {
        const char* key = doc["key"] | "";
        if (bcm::findConfigField(key)) post(app::CmdType::SET_CONFIG, doc["value"].as<double>(), 0, key);
    } else if (!strcmp(cmd, "sim")) {
        if (doc["speed"].is<int>()) post(app::CmdType::SET_SPEED, doc["speed"].as<int>());
        if (doc["paused"].is<bool>()) post(app::CmdType::SET_PAUSED, doc["paused"].as<bool>());
    } else if (!strcmp(cmd, "io")) {
        if (doc["lux_src"].is<const char*>()) post(app::CmdType::SET_LUX_SRC, !strcmp(doc["lux_src"], "ldr"));
        if (doc["phys"].is<bool>()) post(app::CmdType::SET_PHYS_INPUTS, doc["phys"].as<bool>());
    }
}

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg,
               uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        portENTER_CRITICAL(&pending_mux);
        if (pending_count < 8) pending_ids[pending_count++] = client->id();
        portEXIT_CRITICAL(&pending_mux);
    } else if (type == WS_EVT_DATA) {
        const AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
        // Mensagens pequenas chegam em um único quadro; fragmentadas são ignoradas.
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            handleMessage(reinterpret_cast<const char*>(data), len);
        }
    }
}

// ------------------------------------------------------------------ mensagens para a página
String configJson() {
    bcm::Config cfg;
    app::getConfig(cfg);
    app::Snapshot s;
    app::getSnapshot(s);

    JsonDocument doc;
    doc["type"] = "config";
    JsonArray fields = doc["fields"].to<JsonArray>();
    for (size_t i = 0; i < bcm::kConfigFieldCount; ++i) {
        const bcm::ConfigField& f = bcm::kConfigFields[i];
        JsonObject o = fields.add<JsonObject>();
        o["k"] = f.key;
        o["t"] = f.kind == bcm::ConfigField::BOOL ? "bool" : (f.kind == bcm::ConfigField::INT ? "int" : "num");
        o["min"] = f.min;
        o["max"] = f.max;
        o["v"] = r3(bcm::getConfigValue(cfg, f));
    }
    doc["io"]["lux_src"] = luxSrc(s.io);
    doc["io"]["phys"] = s.io.physical_inputs;
    String out;
    serializeJson(doc, out);
    return out;
}

void addInputs(JsonObject o, bcm::Ignition ign, bcm::LightSwitch sw, double lux, const bool* doors, double batt) {
    o["ign"] = static_cast<int>(ign);
    o["sw"] = bcm::switchName(sw);
    o["lux"] = r3(lux);
    JsonArray d = o["doors"].to<JsonArray>();
    for (int i = 0; i < 4; ++i) d.add(doors[i] ? 1 : 0);
    o["batt"] = r3(batt);
}

String stateJson(const app::Snapshot& s) {
    JsonDocument doc;
    doc["type"] = "state";
    doc["t"] = r3(s.t);
    doc["mode"] = bcm::modeName(s.mode);
    doc["timer"] = r3(s.mode_timer);
    doc["locked"] = s.locked;
    doc["dark"] = s.dark;
    doc["relock"] = r3(s.relock_timer);
    doc["itimer"] = r3(s.interior_timer);
    doc["saver"] = s.saver;

    JsonObject out = doc["out"].to<JsonObject>();
    out["low"] = r3(s.out.low_beam);
    out["park"] = r3(s.out.park_lamps);
    out["puddle"] = r3(s.out.puddle);
    out["int"] = r3(s.out.interior);
    out["ind"] = s.out.indicators || s.ind_seen;

    addInputs(doc["in"].to<JsonObject>(), s.eff.ignition, s.eff.light_switch, s.eff.ambient_lux,
              s.eff.doors, s.eff.battery_v);
    addInputs(doc["web"].to<JsonObject>(), s.web.ign, s.web.sw, s.web.lux, s.web.doors, s.web.batt);

    JsonObject phys = doc["phys"].to<JsonObject>();
    phys["ign"] = static_cast<int>(s.phys.ign);
    phys["lux"] = r3(s.phys.lux);
    JsonArray pd = phys["doors"].to<JsonArray>();
    for (int i = 0; i < 4; ++i) pd.add(s.phys.doors[i] ? 1 : 0);

    doc["sim"]["speed"] = s.speed;
    doc["sim"]["paused"] = s.paused;
    doc["sys"]["heap"] = ESP.getFreeHeap();
    doc["sys"]["up"] = millis() / 1000;
    doc["sys"]["clients"] = ws.count();
    String json;
    serializeJson(doc, json);
    return json;
}

String logJson(const app::LogEntry* entries, size_t n, const char* type) {
    JsonDocument doc;
    doc["type"] = type;
    JsonArray items = doc["items"].to<JsonArray>();
    for (size_t i = 0; i < n; ++i) {
        JsonArray item = items.add<JsonArray>();
        item.add(r3(entries[i].t));
        item.add(entries[i].msg);
    }
    String json;
    serializeJson(doc, json);
    return json;
}

void syncPendingClients() {
    uint32_t ids[8];
    size_t n;
    portENTER_CRITICAL(&pending_mux);
    n = pending_count;
    memcpy(ids, pending_ids, n * sizeof(uint32_t));
    pending_count = 0;
    portEXIT_CRITICAL(&pending_mux);
    if (!n) return;

    const String cfg = configJson();
    static app::LogEntry history[LOG_SYNC_COUNT];
    const uint32_t head = app::logHead();
    uint32_t cursor = head > LOG_SYNC_COUNT ? head - LOG_SYNC_COUNT : 0;
    const String logs = logJson(history, app::readLogs(cursor, history, LOG_SYNC_COUNT), "logs");
    for (size_t i = 0; i < n; ++i) {
        ws.text(ids[i], cfg);
        ws.text(ids[i], logs);
    }
}

}  // namespace

void begin() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    WiFi.setSleep(false);  // menor latência no WebSocket
    Serial.printf("Wi-Fi \"%s\" (senha %s) -> http://%s  ou  http://%s.local\n", AP_SSID, AP_PASSWORD,
                  AP_IP.toString().c_str(), HOSTNAME);

    dns.start(53, "*", AP_IP);  // portal cativo: qualquer domínio aponta para o ESP32
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

    if (!LittleFS.begin(true)) Serial.println("ERRO: LittleFS não montou");
    if (!LittleFS.exists("/index.html")) {
        Serial.println("AVISO: interface web ausente. Rode: pio run -t uploadfs");
    }

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
    server.onNotFound([](AsyncWebServerRequest* req) {
        // Testes de conectividade de celulares (generate_204, hotspot-detect...) caem
        // aqui; redirecionar faz o sistema abrir a página automaticamente.
        req->redirect("http://192.168.4.1/");
    });
    server.begin();
    log_cursor = app::logHead();
}

void loop() {
    static uint32_t last_state = 0;
    dns.processNextRequest();
    syncPendingClients();

    const uint32_t now = millis();
    if (now - last_state >= STATE_PERIOD_MS) {
        last_state = now;
        ws.cleanupClients();

        const uint32_t version = app::configVersion();
        if (version != sent_config_version) {
            sent_config_version = version;
            if (ws.count()) ws.textAll(configJson());
        }

        app::LogEntry entries[8];
        size_t n;
        while ((n = app::readLogs(log_cursor, entries, 8)) > 0) {
            if (ws.count()) ws.textAll(logJson(entries, n, "logs"));
        }

        if (ws.count()) {
            app::Snapshot s;
            app::getSnapshot(s);
            ws.textAll(stateJson(s));
        }
        // LED da placa: aceso com página conectada, piscando lento sem ninguém.
        hw::setStatusLed(ws.count() ? true : (now / 500) % 2);
    }
}

}  // namespace web
