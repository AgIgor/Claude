#pragma once
#include <Arduino.h>
#include "config.h"

struct Settings {
  String ssid;
  String pass;
  String hostname = DEFAULT_HOSTNAME;
  uint32_t diInvert = 0;   // bit n = inverte a entrada n (ex.: optoacoplador puxando p/ GND)
  bool diPullUp = false;   // false = pull-down (testes com 3,3 V), true = pull-up
};

extern Settings g_settings;

void settingsLoad();
void settingsSave();

// Registradores D retentivos (gravados na NVS quando alterados pela web)
void retainLoad(int32_t* d, size_t n);
void retainSave(const int32_t* d, size_t n);
