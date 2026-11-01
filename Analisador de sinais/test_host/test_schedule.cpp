// Teste no PC dos modos salvos e da programação por horário.
// Compile com: g++ -std=c++17 -I../src test_schedule.cpp -o test_schedule
#include <cstdio>
#include <string>
#include "ac_schedule.h"

static const int TEMP_MIN = 16, TEMP_MAX = 30;
static const uint8_t SUNDAY = 1 << 0, MONDAY = 1 << 1, ALL_DAYS = 0x7F;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  if (ok) return;
  printf("FALHOU: %s\n", what.c_str());
  failures++;
}

static AcPreset sleepPreset() {
  AcPreset p = {};
  p.mode = 1;
  p.temp = 22;
  p.fan = 1;
  p.vane = 1;
  return p;
}

static void testPresetValidation() {
  check(acPresetValid(sleepPreset(), TEMP_MIN, TEMP_MAX), "modo válido");
  AcPreset p = sleepPreset();
  p.mode = 4;
  check(!acPresetValid(p, TEMP_MIN, TEMP_MAX), "modo inexistente");
  p = sleepPreset();
  p.temp = 31;
  check(!acPresetValid(p, TEMP_MIN, TEMP_MAX), "temperatura acima do máximo");
  p = sleepPreset();
  p.fan = 3;
  check(!acPresetValid(p, TEMP_MIN, TEMP_MAX), "velocidade inexistente");
  p = sleepPreset();
  p.vane = 0;
  check(!acPresetValid(p, TEMP_MIN, TEMP_MAX), "posição fixa 0 não existe");
  p.swing = 1;
  check(acPresetValid(p, TEMP_MIN, TEMP_MAX), "oscilar aceita faixa 0");
  p.vane = 4;
  check(!acPresetValid(p, TEMP_MIN, TEMP_MAX), "oscilar só vai até a faixa 3");
}

static void testRuleValidation() {
  check(acRuleValid({ALL_DAYS, 1, 23, 59, 0}), "regra válida");
  check(acRuleValid({0, 0, 0, 0, AC_RULE_OFF}), "posição livre é aceita para apagar");
  check(!acRuleValid({0x80, 1, 22, 0, 0}), "dia inexistente");
  check(!acRuleValid({ALL_DAYS, 1, 24, 0, 0}), "hora 24");
  check(!acRuleValid({ALL_DAYS, 1, 22, 60, 0}), "minuto 60");
  check(!acRuleValid({ALL_DAYS, 1, 22, 0, AC_PRESET_COUNT}), "modo fora da lista");
  check(!acRuleValid({ALL_DAYS, 1, 22, 0, -2}), "ação abaixo de desligar");
}

static void testRuleDue() {
  const AcRule rules[] = {
      {MONDAY, 1, 22, 0, 0},
      {SUNDAY, 1, 22, 0, 1},
      {ALL_DAYS, 0, 8, 0, 2},
      {ALL_DAYS, 1, 7, 30, AC_RULE_OFF},
      {ALL_DAYS, 1, 7, 30, 3},
      {0, 1, 12, 0, 0},
  };
  const int n = sizeof(rules) / sizeof(rules[0]);
  check(acRuleDue(rules, n, 1, 22, 0) == 0, "segunda às 22:00");
  check(acRuleDue(rules, n, 0, 22, 0) == 1, "domingo às 22:00");
  check(acRuleDue(rules, n, 2, 22, 0) == -1, "terça não está nos dias");
  check(acRuleDue(rules, n, 1, 22, 1) == -1, "minuto seguinte não dispara");
  check(acRuleDue(rules, n, 3, 8, 0) == -1, "regra desativada não dispara");
  check(acRuleDue(rules, n, 3, 7, 30) == 4, "no mesmo minuto vale a última");
  check(acRuleDue(rules, n, 3, 12, 0) == -1, "posição livre não dispara");
}

static void testName() {
  char name[AC_NAME_BYTES];
  acCopyName(name, "Dormir");
  check(std::string(name) == "Dormir", "nome curto");
  acCopyName(name, "12345678901234567890123456789");
  check(std::string(name) == "123456789012345678901234", "nome longo corta no limite");
  // "ç" tem 2 bytes e cairia dividido entre os bytes 23 e 24
  acCopyName(name, "12345678901234567890123\xC3\xA7" "9");
  check(std::string(name) == "12345678901234567890123", "não corta caractere UTF-8 no meio");
}

int main() {
  testPresetValidation();
  testRuleValidation();
  testRuleDue();
  testName();
  if (failures) {
    printf("%d falhas\n", failures);
    return 1;
  }
  puts("Todos os testes passaram");
  return 0;
}
