#pragma once
// Modos salvos e programação por horário, comuns às duas placas. A parte de cima não depende do
// Arduino e tem teste no PC.
#include <stdint.h>
#include <string.h>

#define AC_PRESET_COUNT 6
#define AC_RULE_COUNT   10
#define AC_NAME_BYTES   25
#define AC_RULE_OFF     (-1)  // ação da regra: desligar o ar

struct AcPreset {
  char name[AC_NAME_BYTES];  // vazio: posição livre
  uint8_t mode, temp, fan, swing, vane, sleep, display, mute;
};

struct AcRule {
  uint8_t days;  // bit 0 = domingo; zero: posição livre
  uint8_t enabled, hour, minute;
  int8_t action;  // índice do modo, ou AC_RULE_OFF
};

inline bool acPresetValid(const AcPreset& p, int tempMin, int tempMax) {
  return (p.mode == 1 || p.mode == 2 || p.mode == 3 || p.mode == 6) && p.temp >= tempMin &&
         p.temp <= tempMax && (p.fan == 1 || p.fan == 2 || p.fan == 4 || p.fan == 8) &&
         p.swing <= 1 && (p.swing ? p.vane <= 3 : p.vane >= 1 && p.vane <= 6) && p.sleep <= 1 &&
         p.display <= 1 && p.mute <= 1;
}

inline bool acRuleValid(const AcRule& r) {
  return r.days <= 0x7F && r.enabled <= 1 && r.hour < 24 && r.minute < 60 &&
         r.action >= AC_RULE_OFF && r.action < AC_PRESET_COUNT;
}

// Devolve a regra daquele minuto, ou -1. Com duas no mesmo minuto vale a última da lista: uma
// fila cheia cortaria a segunda no meio.
inline int acRuleDue(const AcRule* rules, int count, int weekday, int hour, int minute) {
  int due = -1;
  for (int i = 0; i < count; i++) {
    const AcRule& r = rules[i];
    if (r.enabled && (r.days & (1 << weekday)) && r.hour == hour && r.minute == minute) due = i;
  }
  return due;
}

// Copia sem cortar um caractere UTF-8 no meio.
inline void acCopyName(char* out, const char* in) {
  size_t n = strlen(in);
  if (n >= AC_NAME_BYTES) {
    n = AC_NAME_BYTES - 1;
    while (n && ((uint8_t)in[n] & 0xC0) == 0x80) n--;
  }
  memcpy(out, in, n);
  out[n] = 0;
}

#ifdef ARDUINO
#include <Arduino.h>

namespace AcSchedule {
typedef String (*ArgFn)(const char* name);

void begin();  // lê a memória e inicia o relógio; chame depois de iniciar o Wi-Fi
void loop();
bool apply(int preset);
bool update(ArgFn arg);  // grava um modo ou regra enviado pela página
String json();
}
#endif
