// Botao resistivo de espelhos (VW Tiguan 2015) com 2 saidas analogicas:
//   joystick: neutro, cima, baixo, esquerda, direita
//   seletor : espelho esquerdo, centro, espelho direito, rebater
// Cada saida e um divisor: 3V3 -> resistor de pull-up -> pino ADC1 -> botao -> GND.
// Os niveis de cada posicao sao calibrados e salvos na NVS.
#pragma once
#include <Arduino.h>

namespace mirsw {

enum Joy : int8_t { JOY_NEUTRAL, JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_COUNT };
enum Sel : int8_t { SEL_LEFT, SEL_CENTER, SEL_RIGHT, SEL_FOLD, SEL_COUNT };
constexpr int8_t UNKNOWN = -1;

extern const char* const kJoyName[JOY_COUNT];
extern const char* const kSelName[SEL_COUNT];

// Pino -1 desativa o botao. Retorna true se carregou uma calibracao valida.
bool begin(int8_t joyPin, int8_t selPin);
bool enabled();
bool calibrated();

void update();                  // chamar no loop()
int8_t joy();                   // posicao estavel; NEUTRAL sem calibracao ou leitura desconhecida
int8_t sel();                   // posicao estavel; UNKNOWN ate a primeira leitura valida
bool selChanged(int8_t& from);  // true uma vez a cada mudanca do seletor

// Calibracao guiada pelo Serial e pelo LED. Bloqueia ate terminar ou receber 'x'.
// idle() e chamada continuamente para manter o radio funcionando.
bool calibrate(int8_t ledPin, void (*idle)());
void printCalibration();
void setMonitor(bool on);
bool monitor();

}  // namespace mirsw
