#pragma once
#include <Arduino.h>

// Definição de PIDs do modo 01 (SAE J1979).
struct PidDef {
  uint8_t pid;
  const char* key;    // chave no JSON
  uint8_t nbytes;
  bool fast;          // atualizado com prioridade
  float (*decode)(const uint8_t* d);
};

extern const PidDef PIDS[];
extern const size_t NPIDS;

String dtcToString(uint8_t a, uint8_t b);
