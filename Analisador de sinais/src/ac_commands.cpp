// Comandos do ar e estado em JSON: iguais nas duas placas. Só o transporte muda (ac_link).
#include "ac_link.h"
#include "ac_protocol.h"
#include "common_config.h"

static bool enqueueByte(uint16_t prop, uint8_t value) { return AcLink::enqueue(prop, &value, 1); }

namespace AcLink {

// Como o app: desliga o modo dormir antes de desligar o ar.
bool power(bool on) {
  if (!on && state().sleep == 1 && !enqueueByte(AC_PROP_SLEEP, 0)) return false;
  return enqueueByte(AC_PROP_POWER, on ? 1 : 0);
}

bool mode(int value) { return enqueueByte(AC_PROP_MODE, value); }

bool fan(int value) { return enqueueByte(AC_PROP_FAN, value); }

bool setpoint(int celsius) {
  const uint8_t value[] = {0, 0, (uint8_t)celsius, 0};
  return enqueue(AC_PROP_SETPOINT, value, sizeof(value));
}

// O app sempre envia o par, nesta ordem.
bool direction(bool oscillate, int position) {
  return enqueueByte(AC_PROP_SWING, oscillate ? 1 : 0) && enqueueByte(AC_PROP_VANE, position);
}

bool sleep(bool on) { return enqueueByte(AC_PROP_SLEEP, on ? 1 : 0); }

bool display(bool on) { return enqueueByte(AC_PROP_DISPLAY, on ? 1 : 0); }

bool mute(bool on) { return enqueueByte(AC_PROP_MUTE, on ? 0 : 1); }  // 04.25 liga o som

bool indirect() { return enqueueByte(AC_PROP_INDIRECT, 1); }

bool autoSense() { return enqueueByte(AC_PROP_AUTO_SENSE, 1); }

bool timerHours(int hours) {
  const uint32_t s = hours * 3600;
  const uint8_t value[] = {(uint8_t)(s >> 24), (uint8_t)(s >> 16), (uint8_t)(s >> 8), (uint8_t)s};
  return enqueue(AC_PROP_TIMER, value, sizeof(value));
}

String json() {
  const AcState& s = state();
  const LinkStats st = stats();
  char b[420];
  snprintf(b, sizeof(b),
           "{\"fw\":\"" FW_VERSION "\",\"on\":%s,\"power\":%d,\"mode\":%d,\"fan\":%d,\"set\":%d,"
           "\"swing\":%d,\"vane\":%d,\"sleep\":%d,\"disp\":%d,\"mute\":%d,\"ind\":%d,\"sense\":%d,\"timer\":%ld,"
           "\"ca30\":%d,\"ca33\":%d,\"ae30\":%d,\"ae35\":%d,\"ae36\":%d,\"hb\":%u,\"hbok\":%u,"
           "\"acks\":%u,\"ok\":%u,\"fail\":%u,\"rx\":%d,\"rxb\":%u,\"rxerr\":%u}",
           enabled() ? "true" : "false", s.power, s.mode, s.fan, s.setpoint, s.swing, s.vane,
           s.sleep, s.display, s.mute, s.indirect, s.autoSense, (long)s.timerSeconds, s.tempCa30,
           s.tempCa33, s.tempAe30, s.tempAe35, s.tempAe36, (unsigned)st.heartbeats,
           (unsigned)st.heartbeatsAcked, (unsigned)st.acksSent, (unsigned)st.commandsOk,
           (unsigned)st.commandsFailed,
           st.lastRxMs ? (int)((millis() - st.lastRxMs) / 1000) : -1, (unsigned)st.rxBytes,
           (unsigned)st.rxErrors);
  return String(b);
}

}  // namespace AcLink
