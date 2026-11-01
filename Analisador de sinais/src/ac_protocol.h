#pragma once
// Protocolo serial do ar Electrolux (ver PROTOCOLO.md), sem dependência do Arduino: roda no ESP32
// e nos testes no PC.
#include <stddef.h>
#include <stdint.h>

static const uint8_t AC_START = 0xC6;
static const uint8_t AC_FIXED = 0xAD;
static const uint16_t AC_REQUEST_FLAG = 0x8000;
static const size_t AC_MAX_FRAME = 40;

static const uint8_t AC_OP_WRITE = 0x04;
static const uint8_t AC_OP_WRITE_ACK = 0x05;
static const uint8_t AC_OP_NOTIFY = 0x06;
static const uint8_t AC_OP_NOTIFY_ACK = 0x07;

static const uint16_t AC_PROP_HEARTBEAT = 0x0020;
static const uint16_t AC_PROP_STATE = 0x0401;
static const uint16_t AC_PROP_POWER = 0x0403;
static const uint16_t AC_PROP_TIMER = 0x0413;      // segundos até desligar
static const uint16_t AC_PROP_MUTE = 0x0425;       // bipe do ar: 01 com som, 00 silencioso
static const uint16_t AC_PROP_SLEEP = 0x0428;
static const uint16_t AC_PROP_DISPLAY = 0x0429;
static const uint16_t AC_PROP_AUTO_SENSE = 0x0438;
static const uint16_t AC_PROP_TEMP_CA30 = 0x0430;
static const uint16_t AC_PROP_SETPOINT = 0x0432;
static const uint16_t AC_PROP_TEMP_CA33 = 0x0433;
static const uint16_t AC_PROP_TEMP_AE35 = 0x0435;
static const uint16_t AC_PROP_TEMP_AE36 = 0x0436;
static const uint16_t AC_PROP_MODE = 0x1000;
static const uint16_t AC_PROP_INDIRECT = 0x1001;
static const uint16_t AC_PROP_FAN = 0x1002;
static const uint16_t AC_PROP_SWING = 0x1005;
static const uint16_t AC_PROP_VANE = 0x1007;

struct AcFrame {
  uint16_t counter;
  char dst[3];
  char src[3];
  uint16_t prop;
  uint8_t op;
  const uint8_t* value;  // aponta para dentro do quadro recebido
  size_t valueLen;
};

// -1 = ainda não avisado pelo ar
struct AcState {
  int16_t power, mode, fan, setpoint, swing, vane;
  int16_t sleep, display, indirect, autoSense, mute;
  int16_t tempCa30, tempCa33, tempAe35, tempAe36, tempAe30;
  int32_t timerSeconds;
};

// Remonta quadros de um fluxo de bytes, para quem lê a UART direto. Bytes soltos antes de um `C6`
// são ignorados. O quadro entregue vale até o próximo push.
class AcReader {
 public:
  bool push(uint8_t b, AcFrame& f);  // true quando completou um quadro válido
  void reset() { n_ = 0; }

 private:
  void resync();
  uint8_t buf_[AC_MAX_FRAME];
  size_t n_ = 0;
};

void acStateReset(AcState& s);
size_t acFrameSize(const uint8_t* raw, size_t n);  // 0 se não houver quadro completo no início
bool acParse(const uint8_t* raw, size_t n, AcFrame& f);
size_t acBuild(uint8_t* out, size_t cap, uint16_t counter, const char* dst, const char* src,
               uint16_t prop, uint8_t op, const uint8_t* value, size_t valueLen);
size_t acBuildAck(uint8_t* out, size_t cap, const AcFrame& f);
bool acApply(AcState& s, const AcFrame& f);  // true se o quadro era um aviso de estado conhecido
