#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>

// Resposta OBD já remontada (ISO-TP): SID + dados, sem PCI.
struct ObdFrame {
  uint16_t src;     // ID CAN de quem respondeu (ex. 0x7E8)
  uint8_t len;
  uint8_t d[128];
};

// OBD-II sobre CAN (ISO 15765-4, ID 11 bits) usando o MCP2515.
class ObdCan {
public:
  // Inicia SPI e verifica se o MCP2515 responde.
  bool begin(uint8_t xtalMHz);
  bool chipOk() const { return _chipOk; }
  // Teste interno em loopback (não toca no barramento do carro).
  bool selfTest();

  // rateIdx: -1 = detecta ouvindo o barramento; 0..2 = 500/250/125 kbps.
  bool connect(int rateIdx, uint16_t reqId);
  void disconnect();

  // Modo escuta (rede ECU <-> painel): listen-only, sem filtros. O MCP2515 nunca
  // transmite nada, nem o ACK, então não interfere na comunicação entre os módulos.
  bool startSniff(int rateIdx);
  bool sniffing() const { return _sniff; }
  bool readFrame(can_frame* f);
  // Quadros perdidos por estouro do buffer do MCP2515 (desde o último pedido).
  uint32_t takeOverflows();
  int rateKbps() const { return _rate < 0 ? 0 : RATE_KBPS[_rate]; }

  // Envia um pedido (ex.: {0x01,0x0C}) e devolve as respostas recebidas.
  // Retorna nº de respostas (0 = nenhuma, -1 = erro de envio).
  int request(const uint8_t* payload, uint8_t n, ObdFrame* out, int maxFrames, bool multi = false);

  bool connected() const { return _connected; }
  String protoName() const;
  String busInfo() const { return _bus; }
  uint16_t ecuId() const { return _ecu; }
  uint16_t reqId() const { return _reqId; }
  uint32_t txErrors() const { return _txErr; }
  uint8_t tec();
  uint8_t rec();
  void setTrace(bool t) { _trace = t; }
  bool trace() const { return _trace; }

  static const uint16_t RATE_KBPS[3];

private:
  MCP2515* _mcp = nullptr;
  CAN_CLOCK _clk = MCP_8MHZ;
  bool _chipOk = false, _connected = false, _trace = false, _sniff = false;
  uint16_t _reqId = 0x7DF, _ecu = 0;
  int _rate = -1;
  uint32_t _txErr = 0;
  String _bus = "-";

  bool listen(int rateIdx, uint32_t ms);
  bool setupNormal(int rateIdx);
  void setFilters();
  bool isResponseId(uint16_t id) const;
  bool send(uint16_t id, const uint8_t* d8);
  void drain();
};
