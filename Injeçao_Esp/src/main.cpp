// ECU programavel ESP32 - 4 cilindros, roda fonica 60-2 + fase,
// injecao sequencial / semissequencial e ignicao sequencial / centelha perdida.
#include <Arduino.h>
#include "engine.h"
#include "settings.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  Serial.println("\nECU ESP32 iniciando");
  settingsInit();
  engineStart();  // tarefa de tempo real no nucleo 1
  webStart();     // Wi-Fi + pagina de configuracao no nucleo 0
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
