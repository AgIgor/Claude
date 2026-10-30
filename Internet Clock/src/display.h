#pragma once
#include <Arduino.h>

namespace Display {
  void begin();
  void loop();

  void    showProgress(uint8_t percent);  // usado durante OTA (desenha imediatamente)
  void    test(uint16_t ms);              // acende todos os segmentos

  // Diagnóstico da matriz
  enum : uint8_t { DIAG_OFF = 0, DIAG_ALL, DIAG_LED, DIAG_ORIENT };
  void    diag(uint8_t mode, uint8_t arg, uint32_t ms);
  void    nextScreen();

  uint8_t brightness();  // brilho atual aplicado
  bool    nightActive();
  const char* screenName();
}
