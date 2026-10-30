#include "mqtt_client.h"
#include "settings.h"
#include "sensors.h"
#include "display.h"
#include "network.h"
#include <ESP8266WiFi.h>
#include <MQTT.h>

// Tópicos (base configurável, padrão "relogio"):
//   <base>/status  -> "online" / "offline" (retido)
//   <base>/state   -> JSON com sensores e estado (retido)
//   <base>/set     <- JSON com qualquer opção de exibição, ex:
//                     {"bMan":50,"bAuto":false,"cMode":0,"cClock":"#FF0000"}
//                     {"next":true} troca de tela, {"test":true} teste de LEDs
//                     {"save":true} grava as mudanças na flash

static WiFiClient net;
static MQTTClient client(512);
static bool       configured = false;
static uint32_t   lastAttempt = 0;
static uint32_t   lastPublish = 0;
static uint32_t   retryDelay = 5000;
static char       topicBuf[80];
static char       clientId[48];

static const char* topic(const char* sub) {
  snprintf(topicBuf, sizeof(topicBuf), "%s/%s", S.mqttTopic, sub);
  return topicBuf;
}

static void publishState() {
  JsonDocument d;
  if (Sensors::climateValid()) {
    d["temp"] = roundf(Sensors::temp() * 10) / 10;
    d["humi"] = roundf(Sensors::humi() * 10) / 10;
  }
  if (Sensors::luxValid()) d["lux"] = roundf(Sensors::lux() * 10) / 10;
  d["bright"] = Display::brightness();
  d["night"] = Display::nightActive();
  d["screen"] = Display::screenName();
  d["rssi"] = WiFi.RSSI();
  d["uptime"] = millis() / 1000;
  String s;
  serializeJson(d, s);
  client.publish(topic("state"), s, true, 0);
}

static void onMessage(String& t, String& payload) {
  if (!t.endsWith("/set")) return;
  JsonDocument doc;
  if (deserializeJson(doc, payload) || !doc.is<JsonObject>()) return;

  // Rede e credenciais não podem ser alteradas via MQTT
  static const char* const BLOCKED[] = {"hostname", "adminPass", "ssid", "pass", "mqEn",
                                        "mqHost", "mqPort", "mqUser", "mqPass", "mqTopic"};
  for (const char* k : BLOCKED) doc.remove(k);

  if (doc["next"] | false) Display::nextScreen();
  if (doc["test"] | false) Display::test(5000);

  uint16_t chg = Cfg::fromJson(doc.as<JsonObjectConst>());
  if (chg & CHG_TIME) Net::applyTime();
  if (doc["save"] | false) Cfg::save();
  lastPublish = 0;  // publica o novo estado logo
}

static void connectSetup() {
  snprintf(clientId, sizeof(clientId), "%s-%06X", S.hostname, (unsigned)ESP.getChipId());
  client.begin(S.mqttHost, S.mqttPort, net);
  client.onMessage(onMessage);
  client.setOptions(30, true, 1500);
  configured = true;
  retryDelay = 5000;
  lastAttempt = 0;
}

void Mqtt::begin() {
  if (S.mqttEnabled && S.mqttHost[0]) connectSetup();
}

void Mqtt::reconfigure() {
  if (configured) {
    client.disconnect();
    configured = false;
  }
  begin();
}

void Mqtt::loop() {
  if (!configured || !Net::connected()) return;
  uint32_t now = millis();

  if (!client.connected()) {
    if (lastAttempt && now - lastAttempt < retryDelay) return;
    lastAttempt = now;
    client.setWill(topic("status"), "offline", true, 1);
    bool ok = client.connect(clientId, S.mqttUser[0] ? S.mqttUser : nullptr,
                             S.mqttPass[0] ? S.mqttPass : nullptr);
    if (!ok) {
      retryDelay = min<uint32_t>(retryDelay * 2, 120000);  // até 2 min entre tentativas
      return;
    }
    retryDelay = 5000;
    client.publish(topic("status"), "online", true, 1);
    client.subscribe(topic("set"));
    lastPublish = 0;
  }

  client.loop();

  if (!lastPublish || now - lastPublish >= S.mqttInterval * 1000UL) {
    lastPublish = now;
    publishState();
  }
}

bool Mqtt::connected() { return configured && client.connected(); }
