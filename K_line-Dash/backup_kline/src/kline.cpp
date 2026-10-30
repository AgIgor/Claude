#include "kline.h"
#include "config.h"
#include "log.h"
#include "soc/gpio_sig_map.h"

const char* KLine::protoName() const {
  switch (_proto) {
    case KProto::KWP_FAST: return "ISO 14230 (KWP2000 fast)";
    case KProto::KWP_SLOW: return "ISO 14230 (KWP2000 5 baud)";
    case KProto::ISO9141:  return "ISO 9141-2";
    default:               return "-";
  }
}

// ---- Monitor do RX por interrupção ----
static volatile uint32_t s_edges = 0, s_lastUs = 0, s_minUs = UINT32_MAX;

static void IRAM_ATTR rxIsr() {
  uint32_t t = micros();
  uint32_t d = t - s_lastUs;
  s_lastUs = t;
  s_edges++;
  if (d > 20 && d < s_minUs) s_minUs = d;
}

void KLine::rxWinStart() {
  noInterrupts();
  s_edges = 0; s_minUs = UINT32_MAX; s_lastUs = micros();
  interrupts();
}

void KLine::rxWinEnd() {
  _winEdges = s_edges;
  _winMinUs = s_minUs == UINT32_MAX ? 0 : s_minUs;
}

String KLine::rxReport() const {
  char b[120];
  if (!_winEdges) return "RX: linha parada, a ECU não transmitiu nada";
  // menor pulso ~ 1 bit -> estimativa da velocidade
  uint32_t est = _winMinUs ? 1000000UL / _winMinUs : 0;
  snprintf(b, sizeof(b), "RX: %u bordas, menor pulso %u us (~%u bps)", _winEdges, _winMinUs, est);
  return b;
}

void KLine::setBaud(uint32_t b) {
  _baud = b;
  _s.updateBaudRate(b);
}

void KLine::flushRx() {
  while (_s.available()) _s.read();
}

int KLine::readByte(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (!_s.available()) {
    if (millis() - t0 >= timeoutMs) return -1;
    delay(1);
  }
  return _s.read();
}

// A K-line é um fio só: tudo que enviamos volta como eco no RX.
bool KLine::sendBytes(const uint8_t* b, size_t n) {
  _echoMissing = 0;
  _lastSent = n;
  for (size_t i = 0; i < n; i++) {
    _s.write(b[i]);
    _s.flush();
    int e = readByte(15);
    if (e != b[i]) {
      _echoErr++;
      // Como a OBD9141: eco diferente não aborta, só fica registrado.
      if (e >= 0) logLine("Eco diferente: enviado %02X, lido %02X", b[i], e);
      else _echoMissing++;
    }
    if (i + 1 < n) delay(KL_P4_MS);
  }
  if (_echoMissing == n) logLine("Nenhum eco: o ESP32 não está lendo a K-line (confira o RX: R2, zener e divisor)");
  return true;
}

size_t KLine::buildFrame(const uint8_t* p, uint8_t n, uint8_t* out) {
  size_t i = 0;
  if (_proto == KProto::ISO9141) {
    out[i++] = 0x68; out[i++] = 0x6A; out[i++] = KL_TESTER_ADDR;
  } else {
    uint8_t fmt = (_target == 0x33) ? 0xC0 : 0x80;  // funcional : físico
    if (n < 64) {
      out[i++] = fmt | n; out[i++] = _target; out[i++] = KL_TESTER_ADDR;
    } else {
      out[i++] = fmt; out[i++] = _target; out[i++] = KL_TESTER_ADDR; out[i++] = n;
    }
  }
  memcpy(out + i, p, n);
  i += n;
  uint8_t cs = 0;
  for (size_t k = 0; k < i; k++) cs += out[k];
  out[i++] = cs;
  return i;
}

size_t KLine::receiveBurst(uint8_t* buf, size_t max, uint32_t firstTimeout) {
  size_t n = 0;
  int b = readByte(firstTimeout);
  if (b < 0) return 0;
  buf[n++] = b;
  while (n < max) {
    b = readByte(KL_P1_GAP_MS);
    if (b < 0) break;
    buf[n++] = b;
  }
  return n;
}

int KLine::parse(const uint8_t* buf, size_t n, KFrame* out, int maxF) {
  int nf = 0;
  if (_proto == KProto::ISO9141) {
    // 48 6B <src> dados... <cs>
    if (n < 5) return 0;
    uint8_t cs = 0;
    for (size_t i = 0; i < n - 1; i++) cs += buf[i];
    if (cs != buf[n - 1]) { logLine("Checksum inválido (9141)"); return 0; }
    out[0].src = buf[2];
    out[0].len = min<size_t>(n - 4, sizeof(out[0].d));
    memcpy(out[0].d, buf + 3, out[0].len);
    return 1;
  }
  // KWP2000: <fmt> [tgt src] [len] dados... <cs>  (pode haver vários quadros)
  size_t i = 0;
  while (i < n && nf < maxF) {
    uint8_t fmt = buf[i];
    size_t hdr = 1;
    uint8_t src = 0;
    if (fmt & 0xC0) {
      if (i + 3 > n) break;
      src = buf[i + 2];
      hdr += 2;
    }
    size_t len = fmt & 0x3F;
    if (len == 0) {
      if (i + hdr >= n) break;
      len = buf[i + hdr];
      hdr++;
    }
    size_t total = hdr + len + 1;
    if (i + total > n) break;
    uint8_t cs = 0;
    for (size_t k = 0; k < total - 1; k++) cs += buf[i + k];
    if (cs == buf[i + total - 1] && len <= sizeof(out[nf].d)) {
      out[nf].src = src;
      out[nf].len = len;
      memcpy(out[nf].d, buf + i + hdr, len);
      nf++;
    } else {
      logLine("Checksum inválido (KWP)");
    }
    i += total;
  }
  return nf;
}

int KLine::request(const uint8_t* p, uint8_t n, KFrame* out, int maxF, bool multi) {
  if (_proto == KProto::NONE) return -1;
  uint32_t since = millis() - _lastActivity;
  if (since < KL_P3_MIN_MS) delay(KL_P3_MIN_MS - since);
  flushRx();

  uint8_t tx[72];
  size_t tl = buildFrame(p, n, tx);
  if (_trace) logLine("TX %s", hexStr(tx, tl).c_str());
  if (!sendBytes(tx, tl)) { _lastActivity = millis(); return -1; }
  rxWinStart();

  int nf = 0;
  uint32_t firstTo = KL_P2_MAX_MS;
  while (nf < maxF) {
    uint8_t buf[160];
    size_t bl = receiveBurst(buf, sizeof(buf), firstTo);
    if (!bl) break;
    if (_trace) logLine("RX %s", hexStr(buf, bl).c_str());
    KFrame tmp[6];
    int k = parse(buf, bl, tmp, 6);
    if (k == 0) break;
    bool pending = false;
    for (int i = 0; i < k; i++) {
      // 7F <sid> 78 = "resposta pendente": a ECU pede mais tempo
      if (tmp[i].len >= 3 && tmp[i].d[0] == 0x7F && tmp[i].d[2] == 0x78) { pending = true; continue; }
      if (nf < maxF) out[nf++] = tmp[i];
    }
    if (pending) { firstTo = 5000; continue; }
    if (!multi && nf > 0) break;
    firstTo = 60;  // aguarda quadros adicionais (outras ECUs / múltiplas DTCs)
  }
  rxWinEnd();
  _lastActivity = millis();
  return nf;
}

void KLine::begin() {
  _s.begin(_baud, SERIAL_8N1, _rx, _tx);
  txUart();
  attachInterrupt(digitalPinToInterrupt(_rx), rxIsr, CHANGE);
}

// Nível lógico da K-line (HIGH = repouso / 12 V).
void KLine::txGpio(bool level) {
  digitalWrite(_tx, KLINE_TX_INVERT ? !level : level);
}

// Liga o pino de volta à UART; com MOSFET a saída é invertida na matriz de GPIO.
void KLine::txUart() {
  pinMatrixOutAttach(_tx, KLINE_TX_SIG, KLINE_TX_INVERT, false);
}

void KLine::idleHigh(uint32_t ms) {
  txGpio(HIGH);          // define o nível antes de trocar a função do pino (sem glitch)
  pinMode(_tx, OUTPUT);
  pinMatrixOutDetach(_tx, false, false);  // pino como GPIO comum, sem inversão
  txGpio(HIGH);
  delay(ms);
}

void KLine::testSignal(uint32_t halfPeriodMs, int cycles) {
  _proto = KProto::NONE;
  idleHigh(100);
  for (int i = 0; i < cycles; i++) {
    txGpio(LOW);
    delay(halfPeriodMs);
    int rxLow = digitalRead(_rx);
    txGpio(HIGH);
    delay(halfPeriodMs);
    int rxHigh = digitalRead(_rx);
    logLine("Teste %d/%d: TX 0 -> RX lê %d | TX 1 -> RX lê %d %s", i + 1, cycles, rxLow, rxHigh,
            (rxLow == 0 && rxHigh == 1) ? "(OK)" : "(FALHA: confira Q1/Q2, pull-up e divisor do RX)");
  }
  txUart();
  flushRx();
}

bool KLine::initFast(uint8_t target) {
  _target = target;
  logLine("Init rápida (KWP2000) alvo %02X...", target);
  idleHigh(KL_IDLE_BEFORE_INIT_MS);    // linha parada 3 s (como a OBD9141)
  txGpio(LOW);  delay(25);  // TiniL
  txGpio(HIGH); delay(25);  // TWuP - TiniL
  txUart();
  flushRx();                // descarta o eco dos pulsos

  _proto = KProto::KWP_FAST;
  _lastActivity = 0;
  const uint8_t startComm[] = {0x81};
  KFrame f[2];
  int n = request(startComm, 1, f, 2);
  if (n > 0 && f[0].d[0] == 0xC1) {
    _ecu = f[0].src;
    _kb1 = f[0].len > 1 ? f[0].d[1] : 0;
    _kb2 = f[0].len > 2 ? f[0].d[2] : 0;
    logLine("Conectado KWP fast. ECU %02X, keybytes %02X %02X", _ecu, _kb1, _kb2);
    delay(KL_POST_INIT_MS);
    return true;
  }
  if (n > 0) logLine("StartCommunication recusado: %s", hexStr(f[0].d, f[0].len).c_str());
  else logLine("Sem resposta à init rápida. %s", rxReport().c_str());
  _proto = KProto::NONE;
  return false;
}

bool KLine::init5Baud(uint8_t addr) {
  logLine("Init 5 baud endereço %02X...", addr);
  idleHigh(KL_IDLE_BEFORE_INIT_MS);  // linha parada 3 s (como a OBD9141)
  txGpio(LOW); delay(200);                                  // start bit
  for (int i = 0; i < 8; i++) { txGpio((addr >> i) & 1); delay(200); }
  txGpio(HIGH);                                             // stop bit
  txUart();
  flushRx();
  rxWinStart();

  int sync = readByte(200 + 500);  // stop bit + W1 (como a OBD9141)
  if (sync != 0x55) {
    delay(150);  // deixa terminar o que a ECU estiver mandando, para medir
    rxWinEnd();
    if (sync < 0) logLine("Sem byte de sincronismo. %s", rxReport().c_str());
    else logLine("Sync inesperado: %02X (esperado 55). %s", sync, rxReport().c_str());
    return false;
  }
  int kb1 = readByte(50), kb2 = readByte(50);
  if (kb1 < 0 || kb2 < 0) { logLine("Keybytes não recebidos"); return false; }
  delay(30);  // W4
  _proto = KProto::KWP_SLOW;  // provisório, só para enviar o complemento
  uint8_t inv = ~kb2;
  if (!sendBytes(&inv, 1)) { _proto = KProto::NONE; return false; }
  int echoAddr = readByte(100);
  if (echoAddr != (uint8_t)~addr) {
    logLine("Complemento do endereço inválido: %02X", echoAddr);
    _proto = KProto::NONE;
    return false;
  }
  _kb1 = kb1; _kb2 = kb2; _ecu = addr;
  // Keybytes iguais (08 08, 94 94...) = ISO 9141-2; diferentes (ex. 8F E9) = KWP2000.
  _proto = (kb1 == kb2) ? KProto::ISO9141 : KProto::KWP_SLOW;
  _target = addr;
  delay(KL_POST_INIT_MS);
  _lastActivity = millis();
  logLine("Conectado %s, keybytes %02X %02X", protoName(), kb1, kb2);
  return true;
}
