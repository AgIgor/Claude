#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "io_hal.h"
#include "plc.h"
#include "settings.h"
#include "web.h"

// Tarefa de scan: núcleo 1, prioridade alta, período fixo.
// Wi-Fi, AsyncTCP e o servidor web rodam no núcleo 0.
static void scanTask(void*) {
  esp_task_wdt_add(nullptr);
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    g_plc.scan();
    esp_task_wdt_reset();
    vTaskDelayUntil(&last, pdMS_TO_TICKS(PLC_SCAN_PERIOD_MS));
  }
}

static void loadProgramFromFlash() {
  File f = LittleFS.open(PROGRAM_FILE, "r");
  if (!f) {
    Serial.println("[plc] nenhum programa salvo");
    return;
  }
  size_t len = f.size();
  char* buf = (char*)malloc(len + 1);
  if (!buf) {
    Serial.println("[plc] sem memória para ler o programa");
    return;
  }
  f.readBytes(buf, len);
  buf[len] = '\0';
  f.close();
  String err;
  if (g_plc.loadProgram(buf, len, err)) {
    g_plc.setRunning(true);
    Serial.println("[plc] programa carregado, em RUN");
  } else {
    Serial.printf("[plc] erro no programa salvo: %s (em STOP)\n", err.c_str());
  }
  free(buf);
}

static void startWifi() {
  WiFi.setHostname(g_settings.hostname.c_str());
  if (g_settings.ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_settings.ssid.c_str(), g_settings.pass.c_str());
    Serial.printf("[wifi] conectando a '%s'", g_settings.ssid.c_str());
    for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] conectado, IP %s\n", WiFi.localIP().toString().c_str());
      return;
    }
    Serial.println("[wifi] falhou, abrindo ponto de acesso");
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[wifi] AP '%s' (senha '%s'), IP %s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[clp] iniciando");

  settingsLoad();
  ioInit(g_settings.diPullUp);
  if (!LittleFS.begin(true)) Serial.println("[fs] falha ao montar LittleFS");

  g_plc.begin();
  loadProgramFromFlash();
  // O scan começa antes do Wi-Fi: a lógica não depende da rede.
  xTaskCreatePinnedToCore(scanTask, "plc_scan", 4096, nullptr, 10, nullptr, 1);

  startWifi();
  if (MDNS.begin(g_settings.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mdns] http://%s.local\n", g_settings.hostname.c_str());
  }
  webBegin();
}

void loop() {
  webLoop();
  delay(5);
}
