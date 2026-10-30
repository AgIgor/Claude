#pragma once
#include <Arduino.h>

namespace Net {
  void begin();
  void loop();

  bool connected();   // conectado ao roteador
  bool apActive();    // portal de configuração ativo
  bool timeValid();   // relógio do sistema com hora válida
  bool timeSynced();  // já recebeu hora do NTP
  const char* apName();

  void applyTime();   // após mudar fuso/servidor NTP
  void applyWifi();   // após mudar SSID/senha
  void applyAuth();   // após mudar senha de administrador
}
