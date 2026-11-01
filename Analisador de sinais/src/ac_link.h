#pragma once
// Lado ativo: a placa faz o papel do módulo Wi-Fi original. Transporte em ac_link.cpp (ESP32) e
// esp01/ac_link_esp01.cpp; comandos e estado em ac_commands.cpp, comuns às duas placas.
// No ESP32, com o controle desligado, o pino TX fica em alta impedância e o estado do ar só é
// acompanhado pela escuta.
#include <Arduino.h>
#include "ac_protocol.h"

struct LinkStats {
  uint32_t heartbeats, heartbeatsAcked, acksSent, commandsOk, commandsFailed, lastRxMs;
  uint32_t rxBytes, rxErrors;  // só o ESP-01 conta; no ESP32 a captura tem contadores próprios
};

namespace AcLink {
#ifdef ESP8266
void begin();
void loop();  // lê a serial e envia o que estiver pendente
void testTxPin();  // alterna o pino TX devagar, para medir com multímetro
#else
void begin(bool enabled);
void setEnabled(bool on);
void onFrames(int ch, const uint8_t* data, size_t n);  // chamado pela tarefa de captura
#endif
bool enabled();
const AcState& state();
LinkStats stats();
bool enqueue(uint16_t prop, const uint8_t* value, uint8_t len);

bool power(bool on);
bool mode(int value);
bool fan(int value);
bool setpoint(int celsius);
bool direction(bool oscillate, int position);  // fixa 1 a 6, ou faixa 0 a 3 oscilando
bool sleep(bool on);
bool display(bool on);
bool mute(bool on);
bool indirect();
bool autoSense();
bool timerHours(int hours);

String json();
}
