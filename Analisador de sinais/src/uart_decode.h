#pragma once
// Decodificador UART por software, sem dependência do Arduino: roda no ESP32 e nos testes no PC.
// Cada borda é um uint32_t: tempo << 1 | nível depois da borda.
#include <stddef.h>
#include <stdint.h>

struct UartFormat {
  const char* name;
  uint8_t dataBits;
  char parity;       // 'N', 'E' ou 'O'
  uint8_t stopBits;
};

extern const UartFormat UART_FORMATS[];
extern const int UART_FORMAT_COUNT;

struct DecodeStats {
  uint32_t good;     // bytes sem erro
  uint32_t framing;  // stop bit errado
  uint32_t parity;   // paridade errada
  uint32_t glitch;   // start bit que não se confirmou
  uint32_t onePulse; // bytes bons feitos de um único pulso, como ruído na linha produz
  uint32_t distinct; // valores diferentes entre os bytes bons
  float score() const;
};

struct UartCandidate {
  uint32_t baud;
  int8_t fmt;        // índice em UART_FORMATS
  float score;
  uint32_t good;
  uint32_t bad;
};

struct UartAnalysis {
  uint32_t pulses;
  uint32_t estBaud;     // 0 = ainda sem pulsos suficientes
  float minPulseUs;
  int count;
  UartCandidate top[4];
};

// Devolve quantos bytes gravou em out (out pode ser nullptr para só contar).
size_t uartDecode(const uint32_t* edges, size_t n, bool idle, uint32_t baud, const UartFormat& f,
                  uint32_t tickHz, uint8_t* out, size_t outMax, DecodeStats& st);

// scratch precisa ter espaço para n valores.
void uartAnalyze(const uint32_t* edges, size_t n, bool idle, uint32_t tickHz, uint32_t* scratch,
                 UartAnalysis& out);
