#include "ac_protocol.h"
#include <string.h>

static const size_t HEADER_LEN = 5;  // início, tamanho, contador (2), fixo
static const size_t ADDR_LEN = 3;
static const size_t MIN_FRAME = HEADER_LEN + ADDR_LEN * 2 + 3 + 1;  // propriedade, operação, checksum
static const size_t TEMP_VALUE_LEN = 4;

static uint8_t xorBytes(const uint8_t* d, size_t n) {
  uint8_t x = 0;
  for (size_t i = 0; i < n; i++) x ^= d[i];
  return x;
}

void acStateReset(AcState& s) { memset(&s, 0xFF, sizeof(s)); }  // todos os campos em -1

size_t acFrameSize(const uint8_t* raw, size_t n) {
  if (n < 2 || raw[0] != AC_START) return 0;
  const size_t size = (size_t)raw[1] + 3;
  return size <= n ? size : 0;
}

bool acParse(const uint8_t* raw, size_t n, AcFrame& f) {
  const size_t size = acFrameSize(raw, n);
  if (size < MIN_FRAME || raw[4] != AC_FIXED) return false;
  if (xorBytes(raw + 1, size - 2) != raw[size - 1]) return false;
  f.counter = (uint16_t)raw[2] << 8 | raw[3];
  memcpy(f.dst, raw + HEADER_LEN, ADDR_LEN);
  memcpy(f.src, raw + HEADER_LEN + ADDR_LEN, ADDR_LEN);
  const uint8_t* p = raw + HEADER_LEN + ADDR_LEN * 2;
  f.prop = (uint16_t)p[0] << 8 | p[1];
  f.op = p[2];
  f.value = p + 3;
  f.valueLen = size - MIN_FRAME;
  return true;
}

size_t acBuild(uint8_t* out, size_t cap, uint16_t counter, const char* dst, const char* src,
               uint16_t prop, uint8_t op, const uint8_t* value, size_t valueLen) {
  const size_t size = MIN_FRAME + valueLen;
  if (size > cap) return 0;
  uint8_t* p = out;
  *p++ = AC_START;
  *p++ = (uint8_t)(size - 3);
  *p++ = counter >> 8;
  *p++ = counter & 0xFF;
  *p++ = AC_FIXED;
  memcpy(p, dst, ADDR_LEN);
  p += ADDR_LEN;
  memcpy(p, src, ADDR_LEN);
  p += ADDR_LEN;
  *p++ = prop >> 8;
  *p++ = prop & 0xFF;
  *p++ = op;
  if (valueLen) memcpy(p, value, valueLen);
  p += valueLen;
  *p = xorBytes(out + 1, size - 2);
  return size;
}

// Origem e destino trocam de lugar, e o contador perde o bit de envio.
size_t acBuildAck(uint8_t* out, size_t cap, const AcFrame& f) {
  return acBuild(out, cap, f.counter & ~AC_REQUEST_FLAG, f.src, f.dst, f.prop, f.op + 1, nullptr, 0);
}

static bool fromNode(const AcFrame& f, const char* name) { return !memcmp(f.src, name, ADDR_LEN); }

bool acApply(AcState& s, const AcFrame& f) {
  if (f.op != AC_OP_NOTIFY || !(f.counter & AC_REQUEST_FLAG) || !f.valueLen) return false;
  const bool ca = fromNode(f, "CA1"), ae = fromNode(f, "AE1");
  const int16_t byte0 = f.value[0];
  const int16_t temp = f.valueLen == TEMP_VALUE_LEN ? f.value[2] : -1;

  if (ca) {
    switch (f.prop) {
      case AC_PROP_STATE: s.power = byte0 != 0; return true;
      case AC_PROP_MODE: s.mode = byte0; return true;
      case AC_PROP_FAN: s.fan = byte0; return true;
      case AC_PROP_SWING: s.swing = byte0; return true;
      case AC_PROP_VANE: s.vane = byte0; return true;
      case AC_PROP_SLEEP: s.sleep = byte0; return true;
      case AC_PROP_MUTE: s.mute = byte0 ? 0 : 1; return true;  // o ar guarda se o som está ligado
      case AC_PROP_DISPLAY: s.display = byte0; return true;
      case AC_PROP_INDIRECT: s.indirect = byte0; return true;
      case AC_PROP_AUTO_SENSE: s.autoSense = byte0; return true;
      case AC_PROP_TIMER:
        if (f.valueLen != TEMP_VALUE_LEN) return false;
        s.timerSeconds = (int32_t)((uint32_t)f.value[0] << 24 | (uint32_t)f.value[1] << 16 |
                                   (uint32_t)f.value[2] << 8 | f.value[3]);
        return true;
      case AC_PROP_SETPOINT: s.setpoint = temp; return true;
      case AC_PROP_TEMP_CA30: s.tempCa30 = temp; return true;
      case AC_PROP_TEMP_CA33: s.tempCa33 = temp; return true;
    }
  } else if (ae) {
    switch (f.prop) {
      case AC_PROP_TEMP_CA30: s.tempAe30 = temp; return true;
      case AC_PROP_TEMP_AE35: s.tempAe35 = temp; return true;
      case AC_PROP_TEMP_AE36: s.tempAe36 = temp; return true;
    }
  }
  return false;
}

// Quadro ruim: o próximo pode já ter começado dentro dele.
void AcReader::resync() {
  size_t i = 1;
  while (i < n_ && buf_[i] != AC_START) i++;
  memmove(buf_, buf_ + i, n_ - i);
  n_ -= i;
}

bool AcReader::push(uint8_t b, AcFrame& f) {
  if (!n_ && b != AC_START) return false;
  buf_[n_++] = b;
  for (;;) {
    if (n_ < 2) return false;
    const size_t size = (size_t)buf_[1] + 3;
    if (size < MIN_FRAME || size > AC_MAX_FRAME) {
      resync();
      if (!n_) return false;
      continue;
    }
    if (n_ < size) return false;
    if (acParse(buf_, size, f)) {
      n_ = 0;
      return true;
    }
    resync();
    if (!n_) return false;
  }
}
