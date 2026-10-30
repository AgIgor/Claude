#include "canbus.h"
#include "config.h"
#include "log.h"

const uint16_t ObdCan::RATE_KBPS[3] = {500, 250, 125};
static const CAN_SPEED RATE_SPEED[3] = {CAN_500KBPS, CAN_250KBPS, CAN_125KBPS};

bool ObdCan::begin(uint8_t xtalMHz) {
  _clk = xtalMHz == 16 ? MCP_16MHZ : MCP_8MHZ;
  if (!_mcp) {
    SPI.begin(CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN, CAN_CS_PIN);
    _mcp = new MCP2515(CAN_CS_PIN, CAN_SPI_HZ, &SPI);
  }
  _mcp->reset();
  // Depois do reset o chip fica em modo configuração; se não confirmar, não há MCP2515 no SPI.
  _chipOk = _mcp->setConfigMode() == MCP2515::ERROR_OK;
  if (!_chipOk) {
    logLine("MCP2515 não responde no SPI. Confira VCC, GND, CS(GPIO%d) SCK(%d) MISO(%d) MOSI(%d)",
            CAN_CS_PIN, CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN);
    return false;
  }
  logLine("MCP2515 OK (cristal %u MHz, SPI %u kHz)", xtalMHz, CAN_SPI_HZ / 1000);
  _mcp->setBitrate(CAN_500KBPS, _clk);
  _mcp->setListenOnlyMode();  // em repouso: só escuta, nunca interfere no barramento
  return true;
}

uint8_t ObdCan::tec() { return _chipOk ? _mcp->errorCountTX() : 0; }
uint8_t ObdCan::rec() { return _chipOk ? _mcp->errorCountRX() : 0; }

String ObdCan::protoName() const {
  if (_sniff && _rate >= 0) return "Escuta da rede CAN " + String(RATE_KBPS[_rate]) + " kbps";
  if (!_connected || _rate < 0) return "-";
  return "ISO 15765-4 CAN 11 bits " + String(RATE_KBPS[_rate]) + " kbps";
}

bool ObdCan::selfTest() {
  if (!_chipOk) return false;
  _mcp->reset();
  _mcp->setBitrate(CAN_500KBPS, _clk);
  _mcp->setLoopbackMode();
  can_frame tx = {};
  tx.can_id = 0x123;
  tx.can_dlc = 8;
  for (int i = 0; i < 8; i++) tx.data[i] = 0xA0 + i;
  bool ok = false;
  if (_mcp->sendMessage(&tx) == MCP2515::ERROR_OK) {
    uint32_t t0 = millis();
    can_frame rx;
    while (millis() - t0 < 100) {
      if (_mcp->readMessage(&rx) == MCP2515::ERROR_OK) {
        ok = rx.can_id == tx.can_id && rx.can_dlc == 8 && !memcmp(rx.data, tx.data, 8);
        break;
      }
      delay(1);
    }
  }
  _connected = false;
  _mcp->reset();
  _mcp->setBitrate(CAN_500KBPS, _clk);
  _mcp->setListenOnlyMode();
  return ok;
}

// Escuta o barramento sem transmitir nada (nem ACK). Se chegam quadros válidos,
// a velocidade está certa.
bool ObdCan::listen(int rateIdx, uint32_t ms) {
  _mcp->reset();
  _mcp->setBitrate(RATE_SPEED[rateIdx], _clk);
  _mcp->setListenOnlyMode();
  uint16_t ids[48];
  int nIds = 0;
  uint32_t frames = 0, t0 = millis();
  can_frame f;
  while (millis() - t0 < ms) {
    if (_mcp->readMessage(&f) != MCP2515::ERROR_OK) { delay(1); continue; }
    frames++;
    uint16_t id = f.can_id & CAN_SFF_MASK;
    bool seen = false;
    for (int i = 0; i < nIds; i++) if (ids[i] == id) { seen = true; break; }
    if (!seen && nIds < 48) ids[nIds++] = id;
  }
  if (!frames) return false;
  String list;
  for (int i = 0; i < nIds && i < 12; i++) {
    char b[6];
    snprintf(b, sizeof(b), " %03X", ids[i]);
    list += b;
  }
  if (nIds > 12) list += " ...";
  _bus = String(frames * 1000 / ms) + " quadros/s, " + nIds + " IDs";
  logLine("Tráfego em %u kbps: %u quadros em %u ms, %d IDs:%s", RATE_KBPS[rateIdx], frames, ms, nIds, list.c_str());
  return true;
}

bool ObdCan::isResponseId(uint16_t id) const {
  if (_reqId == 0x7DF) return id >= 0x7E8 && id <= 0x7EF;
  return id == _reqId + 8;
}

void ObdCan::setFilters() {
  // Só deixa passar as respostas de diagnóstico; o resto do tráfego do carro é descartado no chip.
  uint32_t mask = _reqId == 0x7DF ? 0x7F8 : 0x7FF;
  uint32_t filt = _reqId == 0x7DF ? 0x7E8 : _reqId + 8;
  _mcp->setFilterMask(MCP2515::MASK0, false, mask);
  _mcp->setFilterMask(MCP2515::MASK1, false, mask);
  const MCP2515::RXF rxf[] = {MCP2515::RXF0, MCP2515::RXF1, MCP2515::RXF2,
                              MCP2515::RXF3, MCP2515::RXF4, MCP2515::RXF5};
  for (auto r : rxf) _mcp->setFilter(r, false, filt);
}

bool ObdCan::setupNormal(int rateIdx) {
  _mcp->reset();
  _mcp->setBitrate(RATE_SPEED[rateIdx], _clk);
  setFilters();
  // One-shot: cada quadro é transmitido uma única vez. Se ninguém confirmar (velocidade
  // errada, fio trocado), o chip não fica repetindo o quadro no barramento do carro.
  // A biblioteca sempre retorna erro aqui (o bit OSM não aparece no CANSTAT), por isso
  // o retorno é ignorado.
  _mcp->setNormalOneShotMode();
  _rate = rateIdx;
  return true;
}

bool ObdCan::connect(int rateIdx, uint16_t reqId) {
  if (!_chipOk) return false;
  _connected = false;
  _bus = "-";
  int rate = rateIdx;
  if (rate < 0) {
    for (int i = 0; i < 3 && rate < 0; i++)
      if (listen(i, CAN_LISTEN_MS)) rate = i;
    if (rate < 0) {
      logLine("Nenhum tráfego CAN em 500/250/125 kbps. Ignição ligada? CANH/CANL trocados? "
              "Cristal certo (%s MHz)?", _clk == MCP_16MHZ ? "16" : "8");
      rate = 0;  // tenta mesmo assim no padrão OBD (500 kbps)
    }
  }

  // Tenta o ID pedido; no modo funcional (7DF) também tenta direto a ECU do motor (7E0).
  const uint16_t tries[2] = {reqId, (uint16_t)(reqId == 0x7DF ? 0x7E0 : 0)};
  for (uint16_t id : tries) {
    if (!id) continue;
    _reqId = id;
    if (!setupNormal(rate)) break;
    _connected = true;
    logLine("Pedido OBD 01 00 para %03X em %u kbps...", id, RATE_KBPS[rate]);
    uint8_t q[] = {0x01, 0x00};
    ObdFrame f;
    int n = request(q, 2, &f, 1);
    if (n > 0) {
      _ecu = f.src;
      logLine("ECU %03X respondeu: %s%s", _ecu, hexStr(f.d, f.len).c_str(),
              (f.len >= 3 && f.d[0] == 0x7F) ? "  <- RECUSA (a ECU existe, mas não aceitou o OBD 01)" : "");
      if (_bus == "-") _bus = String(RATE_KBPS[rate]) + " kbps";
      return true;
    }
    logLine("Sem resposta de %03X (TEC %u, REC %u)", id, tec(), rec());
    _connected = false;
  }
  _mcp->setListenOnlyMode();
  return false;
}

void ObdCan::disconnect() {
  _connected = false;
  _sniff = false;
  if (_chipOk) _mcp->setListenOnlyMode();
}

bool ObdCan::startSniff(int rateIdx) {
  if (!_chipOk) return false;
  _connected = false;
  _sniff = false;
  _bus = "-";
  int rate = rateIdx;
  if (rate < 0) {
    for (int i = 0; i < 3 && rate < 0; i++)
      if (listen(i, CAN_LISTEN_MS)) rate = i;
    if (rate < 0) {
      logLine("Escuta: nenhum tráfego em 500/250/125 kbps. Ignição ligada? CAN-H/CAN-L trocados? "
              "Cristal certo (%s MHz)?", _clk == MCP_16MHZ ? "16" : "8");
      return false;
    }
  }
  _mcp->reset();  // reset deixa filtros e máscaras zerados: aceita todos os IDs
  _mcp->setBitrate(RATE_SPEED[rate], _clk);
  _mcp->setListenOnlyMode();
  _rate = rate;
  _sniff = true;
  if (_bus == "-") _bus = String(RATE_KBPS[rate]) + " kbps";
  logLine("Escuta ativa em %u kbps (listen-only: o ESP32 não transmite nada)", RATE_KBPS[rate]);
  return true;
}

bool ObdCan::readFrame(can_frame* f) {
  return _sniff && _mcp->readMessage(f) == MCP2515::ERROR_OK;
}

uint32_t ObdCan::takeOverflows() {
  if (!_chipOk) return 0;
  uint8_t ef = _mcp->getErrorFlags();
  uint32_t n = ((ef & MCP2515::EFLG_RX0OVR) ? 1 : 0) + ((ef & MCP2515::EFLG_RX1OVR) ? 1 : 0);
  if (n) _mcp->clearRXnOVRFlags();
  return n;
}

void ObdCan::drain() {
  can_frame f;
  for (int i = 0; i < 8 && _mcp->readMessage(&f) == MCP2515::ERROR_OK; i++) {}
}

bool ObdCan::send(uint16_t id, const uint8_t* d8) {
  can_frame f = {};
  f.can_id = id;
  f.can_dlc = 8;
  memcpy(f.data, d8, 8);
  if (_trace) logLine("TX %03X %s", id, hexStr(d8, 8).c_str());
  MCP2515::ERROR e = _mcp->sendMessage(&f);
  if (e != MCP2515::ERROR_OK) {
    _txErr++;
    uint8_t ef = _mcp->getErrorFlags();
    logLine("Falha ao transmitir (erro %d, flags %02X, TEC %u)%s", e, ef, tec(),
            (ef & MCP2515::EFLG_TXBO) ? " BUS-OFF: ninguém confirmou o quadro" : "");
    return false;
  }
  return true;
}

int ObdCan::request(const uint8_t* p, uint8_t n, ObdFrame* out, int maxF, bool multi) {
  if (!_connected || n == 0 || n > 7) return -1;
  drain();
  uint8_t d[8] = {0};
  d[0] = n;  // PCI: quadro único
  memcpy(d + 1, p, n);
  if (!send(_reqId, d)) return -1;

  int nf = 0;
  uint32_t deadline = millis() + CAN_P2_MS;
  // Remontagem ISO-TP (uma mensagem multi-quadro por vez)
  uint16_t asmId = 0, asmLen = 0, asmGot = 0;
  uint8_t asmSeq = 0, asmBuf[sizeof(out[0].d)];
  can_frame f;

  auto finish = [&](uint16_t id, const uint8_t* data, uint16_t len) {
    out[nf].src = id;
    out[nf].len = min<uint16_t>(len, sizeof(out[nf].d));
    memcpy(out[nf].d, data, out[nf].len);
    nf++;
    deadline = millis() + CAN_MULTI_MS;  // espera respostas de outras ECUs
  };

  while ((int32_t)(deadline - millis()) > 0 && nf < maxF) {
    if (_mcp->readMessage(&f) != MCP2515::ERROR_OK) { delay(1); continue; }
    if (f.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG)) continue;
    uint16_t id = f.can_id & CAN_SFF_MASK;
    if (!isResponseId(id) || f.can_dlc < 2) continue;
    if (_trace) logLine("RX %03X %s", id, hexStr(f.data, f.can_dlc).c_str());

    uint8_t pci = f.data[0] >> 4;
    if (pci == 0) {                                   // quadro único
      uint8_t len = f.data[0] & 0x0F;
      if (len == 0 || len > 7) continue;
      if (len >= 3 && f.data[1] == 0x7F && f.data[3] == 0x78) { deadline = millis() + 5000; continue; }
      finish(id, f.data + 1, len);
      if (!multi) break;
    } else if (pci == 1) {                            // primeiro quadro
      asmLen = ((f.data[0] & 0x0F) << 8) | f.data[1];
      asmId = id;
      asmGot = min<uint16_t>(6, asmLen);
      memcpy(asmBuf, f.data + 2, asmGot);
      asmSeq = 1;
      const uint8_t fc[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};  // flow control: pode mandar tudo
      send(id - 8, fc);
      deadline = millis() + 1000;
    } else if (pci == 2 && asmLen && id == asmId) {   // quadro consecutivo
      if ((f.data[0] & 0x0F) != (asmSeq & 0x0F)) {
        logLine("ISO-TP: sequência perdida de %03X", id);
        asmLen = 0;
        continue;
      }
      asmSeq++;
      uint16_t k = min<uint16_t>(7, asmLen - asmGot);
      if (asmGot + k <= sizeof(asmBuf)) memcpy(asmBuf + asmGot, f.data + 1, k);
      asmGot += k;
      if (asmGot >= asmLen) {
        finish(id, asmBuf, asmLen);
        asmLen = 0;
        if (!multi) break;
      } else {
        deadline = millis() + 1000;
      }
    }
  }
  return nf;
}
