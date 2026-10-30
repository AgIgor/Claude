#pragma once
#include <Arduino.h>

namespace Sensors {
  void begin();
  void loop();

  bool  climateValid();
  float temp();       // °C, já com offset
  float humi();       // %, recalculada para a temperatura corrigida + offset
  float rawTemp();    // °C, leitura do sensor (média)
  float rawHumi();

  bool  luxValid();
  float lux();
}
