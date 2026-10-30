#include "obd.h"

const PidDef PIDS[] = {
  {0x0C, "rpm",  2, true,  [](const uint8_t* d) { return (d[0] * 256 + d[1]) / 4.0f; }},
  {0x0D, "spd",  1, true,  [](const uint8_t* d) { return (float)d[0]; }},
  {0x11, "tps",  1, true,  [](const uint8_t* d) { return d[0] * 100.0f / 255.0f; }},
  {0x0B, "map",  1, true,  [](const uint8_t* d) { return (float)d[0]; }},
  {0x0E, "adv",  1, true,  [](const uint8_t* d) { return d[0] / 2.0f - 64.0f; }},
  {0x04, "load", 1, true,  [](const uint8_t* d) { return d[0] * 100.0f / 255.0f; }},
  {0x05, "ect",  1, false, [](const uint8_t* d) { return d[0] - 40.0f; }},
  {0x0F, "iat",  1, false, [](const uint8_t* d) { return d[0] - 40.0f; }},
  {0x06, "stft", 1, false, [](const uint8_t* d) { return (d[0] - 128) * 100.0f / 128.0f; }},
  {0x07, "ltft", 1, false, [](const uint8_t* d) { return (d[0] - 128) * 100.0f / 128.0f; }},
  {0x14, "o2",   2, false, [](const uint8_t* d) { return d[0] / 200.0f; }},
  {0x42, "vecu", 2, false, [](const uint8_t* d) { return (d[0] * 256 + d[1]) / 1000.0f; }},
  {0x01, "_mon", 4, false, [](const uint8_t* d) { return (float)d[0]; }},  // MIL + nº de DTCs
};
const size_t NPIDS = sizeof(PIDS) / sizeof(PIDS[0]);

String dtcToString(uint8_t a, uint8_t b) {
  static const char sys[] = {'P', 'C', 'B', 'U'};
  char s[6];
  snprintf(s, sizeof(s), "%c%X%X%02X", sys[a >> 6], (a >> 4) & 0x3, a & 0xF, b);
  return String(s);
}
