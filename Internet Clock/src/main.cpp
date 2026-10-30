// Internet Clock v6 - ESP-01 + 29 LEDs WS2812 + AHT10 + BH1750
// Configuração pela página web: http://relogio.local (ou portal "Relogio-XXXX")

#include <Arduino.h>
#include "settings.h"
#include "display.h"
#include "sensors.h"
#include "network.h"
#include "mqtt_client.h"
#include "web.h"

void setup() {
  Cfg::load();
  Display::begin();
  Sensors::begin();
  Net::begin();
  Web::begin();
  Mqtt::begin();
}

void loop() {
  Net::loop();
  Web::loop();
  Sensors::loop();
  Mqtt::loop();
  Display::loop();
}
