#include "network.h"
#include "settings.h"
#include "display.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <coredecls.h>
#include <time.h>

static DNSServer dns;
static bool     ap = false;
static bool     synced = false;
static bool     wasConnected = false;
static uint32_t lastConnected = 0;
static uint32_t connectedSince = 0;
static uint32_t lastRetry = 0;
static char     apSsid[24];

static const uint32_t AP_AFTER_MS    = 30000;  // sem conexão por 30s -> abre o portal
static const uint32_t AP_CLOSE_MS    = 30000;  // conectado por 30s e ninguém no portal -> fecha
static const uint32_t AP_RETRY_MS    = 60000;  // com portal aberto, tenta o roteador a cada 60s

static void startAP() {
  if (ap) return;
  WiFi.setAutoReconnect(false);  // evita troca de canal derrubando quem está no portal
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  ap = true;
  lastRetry = millis();
}

static void stopAP() {
  if (!ap) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  ap = false;
}

void Net::begin() {
  snprintf(apSsid, sizeof(apSsid), "Relogio-%04X", (unsigned)(ESP.getChipId() & 0xFFFF));

  WiFi.persistent(false);  // credenciais ficam nas nossas configurações
  WiFi.mode(WIFI_STA);
  WiFi.hostname(S.hostname);
  WiFi.setSleepMode(WIFI_MODEM_SLEEP);  // menos consumo = menos calor perto do sensor
  WiFi.setAutoReconnect(true);

  settimeofday_cb([](bool fromSntp) { if (fromSntp) synced = true; });
  applyTime();

  if (S.wifiSsid[0]) WiFi.begin(S.wifiSsid, S.wifiPass);
  else startAP();

  ArduinoOTA.setHostname(S.hostname);
  applyAuth();
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    Display::showProgress(done * 100ULL / total);
  });
  ArduinoOTA.begin(true);  // também inicia o mDNS (hostname.local)
  MDNS.addService("http", "tcp", 80);
}

void Net::loop() {
  uint32_t now = millis();
  bool c = WiFi.status() == WL_CONNECTED;
  if (c) {
    if (!wasConnected) connectedSince = now;
    lastConnected = now;
  }
  wasConnected = c;

  if (!ap) {
    if (!c && now - lastConnected > AP_AFTER_MS) startAP();
  } else {
    dns.processNextRequest();
    uint8_t clients = WiFi.softAPgetStationNum();
    if (c && now - connectedSince > AP_CLOSE_MS && clients == 0) {
      stopAP();
    } else if (!c && S.wifiSsid[0] && clients == 0 && now - lastRetry > AP_RETRY_MS) {
      lastRetry = now;
      WiFi.begin(S.wifiSsid, S.wifiPass);
    }
  }

  ArduinoOTA.handle();
}

bool Net::connected() { return WiFi.status() == WL_CONNECTED; }
bool Net::apActive() { return ap; }
bool Net::timeValid() { return time(nullptr) > 1700000000; }
bool Net::timeSynced() { return synced; }
const char* Net::apName() { return apSsid; }

void Net::applyTime() {
  configTime(S.tz, S.ntp[0] ? S.ntp : "pool.ntp.org", "time.google.com");
}

void Net::applyWifi() {
  WiFi.disconnect();
  if (S.wifiSsid[0]) WiFi.begin(S.wifiSsid, S.wifiPass);
  lastRetry = millis();
}

void Net::applyAuth() {
  ArduinoOTA.setPassword(S.adminPass);  // vazio = sem senha
}
