#pragma once
#include <Arduino.h>

enum class KProto : uint8_t { NONE, KWP_FAST, KWP_SLOW, ISO9141 };

struct KFrame {
  uint8_t src;      // endereço de quem respondeu
  uint8_t len;      // bytes em d[]
  uint8_t d[64];    // dados (SID + parâmetros), sem cabeçalho/checksum
};

class KLine {
public:
  KLine(HardwareSerial& s, int rx, int tx) : _s(s), _rx(rx), _tx(tx) {}

  void begin();
  void setBaud(uint32_t b);
  uint32_t baud() const { return _baud; }

  // Diagnóstico do RX (contagem de bordas por interrupção, independente da UART)
  String rxReport() const;           // atividade na janela da última resposta
  uint8_t lastEchoMissing() const { return _echoMissing; }
  uint8_t lastSent() const { return _lastSent; }

  // Inicialização rápida ISO 14230 (KWP2000) + StartCommunication.
  bool initFast(uint8_t target);
  // Inicialização 5 baud (ISO 9141-2 ou KWP2000 lento).
  bool init5Baud(uint8_t addr);
  void disconnect() { _proto = KProto::NONE; }
  // Teste do circuito: alterna a K-line lentamente (visível no multímetro).
  void testSignal(uint32_t halfPeriodMs, int cycles);

  // Envia um pedido (ex.: {0x01,0x0C}) e devolve os quadros recebidos.
  // Retorna nº de quadros (0 = sem resposta, -1 = erro de barramento).
  int request(const uint8_t* payload, uint8_t n, KFrame* out, int maxFrames, bool multi = false);

  bool connected() const { return _proto != KProto::NONE; }
  KProto proto() const { return _proto; }
  const char* protoName() const;
  uint8_t ecuAddr() const { return _ecu; }
  uint8_t kb1() const { return _kb1; }
  uint8_t kb2() const { return _kb2; }
  void setTarget(uint8_t t) { _target = t; }
  void setTrace(bool t) { _trace = t; }
  bool trace() const { return _trace; }
  uint32_t echoErrors() const { return _echoErr; }

private:
  HardwareSerial& _s;
  int _rx, _tx;
  KProto _proto = KProto::NONE;
  uint8_t _target = 0x33, _ecu = 0, _kb1 = 0, _kb2 = 0;
  uint32_t _lastActivity = 0, _echoErr = 0;
  uint32_t _baud = 10400;
  uint32_t _winEdges = 0, _winMinUs = 0;
  uint8_t _echoMissing = 0, _lastSent = 0;
  void rxWinStart();
  void rxWinEnd();
  bool _trace = false;

  void flushRx();
  int readByte(uint32_t timeoutMs);
  bool sendBytes(const uint8_t* b, size_t n);
  size_t buildFrame(const uint8_t* p, uint8_t n, uint8_t* out);
  size_t receiveBurst(uint8_t* buf, size_t max, uint32_t firstTimeout);
  int parse(const uint8_t* buf, size_t n, KFrame* out, int maxF);
  void idleHigh(uint32_t ms);
  void txGpio(bool level);   // controla a K-line manualmente (pulsos de init)
  void txUart();             // devolve o pino TX para a UART
};
