// Teste no PC do protocolo do ar: todos os valores esperados vêm das capturas de 31/10/2026.
// Compile com: g++ -std=c++17 -I../src test_protocol.cpp ../src/ac_protocol.cpp -o test_protocol
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ac_protocol.h"

static int failures = 0;

static std::vector<uint8_t> fromHex(const std::string& h) {
  std::vector<uint8_t> v;
  for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back(std::stoi(h.substr(i, 2), nullptr, 16));
  return v;
}

static void check(bool ok, const std::string& what) {
  if (ok) return;
  printf("FALHOU: %s\n", what.c_str());
  failures++;
}

struct Command {
  const char* hex;
  uint16_t counter, prop;
  std::vector<uint8_t> value;
};

// Comandos do app, na ordem da captura
static void testBuildsCapturedCommands() {
  const Command cmds[] = {
    {"C60DD0F2AD4341315250310403040180", 0xD0F2, AC_PROP_POWER, {1}},
    {"C60DD0F4AD4341315250311000040191", 0xD0F4, AC_PROP_MODE, {1}},
    {"C610D0F6AD43413152503104320400001600BF", 0xD0F6, AC_PROP_SETPOINT, {0, 0, 0x16, 0}},
    {"C610D0F8AD43413152503104320400001700B0", 0xD0F8, AC_PROP_SETPOINT, {0, 0, 0x17, 0}},
    {"C60DD0FAAD434131525031100204019D", 0xD0FA, AC_PROP_FAN, {1}},
    {"C60DD0FBAD434131525031100204019C", 0xD0FB, AC_PROP_FAN, {1}},
    {"C60DD0FDAD434131525031100204049F", 0xD0FD, AC_PROP_FAN, {4}},
    {"C60DD100AD4341315250311000040366", 0xD100, AC_PROP_MODE, {3}},
    {"C60DD102AD4341315250311005040163", 0xD102, AC_PROP_SWING, {1}},
    {"C60DD103AD4341315250311007040061", 0xD103, AC_PROP_VANE, {0}},
    {"C60DD105AD4341315250311005040065", 0xD105, AC_PROP_SWING, {0}},
    {"C60DD106AD4341315250311007040165", 0xD106, AC_PROP_VANE, {1}},
    {"C60DD108AD434131525031040304007A", 0xD108, AC_PROP_POWER, {0}},
  };
  for (const Command& c : cmds) {
    uint8_t out[AC_MAX_FRAME];
    size_t n = acBuild(out, sizeof(out), c.counter, "CA1", "RP1", c.prop, AC_OP_WRITE,
                       c.value.data(), c.value.size());
    auto want = fromHex(c.hex);
    check(n == want.size() && !memcmp(out, want.data(), n), std::string("comando ") + c.hex);
  }
}

// Funções da terceira captura, uma por valor
static void testBuildsThirdCapture() {
  const Command cmds[] = {
    {"C60D8020AD4341315250311000040612", 0x8020, AC_PROP_MODE, {6}},
    {"C60D8057AD4341315250311000040261", 0x8057, AC_PROP_MODE, {2}},
    {"C60D8022AD4341315250311002040216", 0x8022, AC_PROP_FAN, {2}},
    {"C60D8024AD434131525031100204081A", 0x8024, AC_PROP_FAN, {8}},
    {"C60D802DAD434131525031100704021C", 0x802D, AC_PROP_VANE, {2}},
    {"C60D8037AD4341315250311007040602", 0x8037, AC_PROP_VANE, {6}},
    {"C60D8047AD434131525031042804014E", 0x8047, AC_PROP_SLEEP, {1}},
    {"C60D804BAD4341315250310428040043", 0x804B, AC_PROP_SLEEP, {0}},
    {"C6108049AD43413152503104130400000E1079", 0x8049, AC_PROP_TIMER, {0, 0, 0x0E, 0x10}},
    {"C60D804FAD434131525031100104017B", 0x804F, AC_PROP_INDIRECT, {1}},
    {"C60D8051AD4341315250310438040148", 0x8051, AC_PROP_AUTO_SENSE, {1}},
    {"C60D8054AD434131525031042904015C", 0x8054, AC_PROP_DISPLAY, {1}},
    {"C60D8055AD434131525031042904005C", 0x8055, AC_PROP_DISPLAY, {0}},
    {"C610805AAD43413152503104320400001E004B", 0x805A, AC_PROP_SETPOINT, {0, 0, 0x1E, 0}},
  };
  for (const Command& c : cmds) {
    uint8_t out[AC_MAX_FRAME];
    size_t n = acBuild(out, sizeof(out), c.counter, "CA1", "RP1", c.prop, AC_OP_WRITE,
                       c.value.data(), c.value.size());
    auto want = fromHex(c.hex);
    check(n == want.size() && !memcmp(out, want.data(), n), std::string("comando ") + c.hex);
  }
}

// MAC e IP ficticios no lugar dos capturados; o checksum foi recalculado
static void testBuildsCapturedHeartbeat() {
  const auto want = fromHex("C61DCF83AD4341314E495500200601020102130102000000000104C0A8000ACC");
  const auto value = fromHex("010201021301" "02000000000104C0A8000A");
  uint8_t out[AC_MAX_FRAME];
  size_t n = acBuild(out, sizeof(out), 0xCF83, "CA1", "NIU", AC_PROP_HEARTBEAT, AC_OP_NOTIFY,
                     value.data(), value.size());
  check(n == want.size() && !memcmp(out, want.data(), n), "heartbeat");
}

// Para cada aviso do ar, a confirmação tem que sair igual à do módulo original
static void testBuildsCapturedAcks() {
  const char* pairs[][2] = {
    {"C60DD1C9AD414C4C43413110020604DA", "C60C51C9AD434131414C4C1002075E"},
    {"C610D1CCAD414C4C41453104360600001400F4", "C60C51CCAD414531414C4C0436077D"},
    {"C60FD1F5AD414C4C414531044406014700F2", "C60C51F5AD414531414C4C04440736"},
    {"C610D1E0AD414C4C43413104300600001700DB", "C60C51E0AD434131414C4C04300751"},
    {"C60DD1F1AD414C4C43413104010600F1", "C60C51F1AD434131414C4C04010771"},
  };
  for (auto& p : pairs) {
    auto in = fromHex(p[0]), want = fromHex(p[1]);
    AcFrame f;
    check(acParse(in.data(), in.size(), f), std::string("leitura ") + p[0]);
    uint8_t out[AC_MAX_FRAME];
    size_t n = acBuildAck(out, sizeof(out), f);
    check(n == want.size() && !memcmp(out, want.data(), n), std::string("confirmação ") + p[0]);
  }
}

static void testRejectsDamagedFrames() {
  auto raw = fromHex("C60DD1C9AD414C4C43413110020604DA");
  AcFrame f;
  raw[9] ^= 1;
  check(!acParse(raw.data(), raw.size(), f), "checksum errado");
  raw[9] ^= 1;
  check(!acParse(raw.data(), raw.size() - 1, f), "quadro cortado");
  raw[0] = 0xAA;
  check(!acParse(raw.data(), raw.size(), f), "início errado");
}

static void testSplitsMergedFrames() {
  auto raw = fromHex("C60C50F2AD52503143413104030501C60DD22DAD414C4C434131040106022C");
  size_t first = acFrameSize(raw.data(), raw.size());
  check(first == 15, "tamanho do primeiro quadro colado");
  AcFrame a, b;
  check(acParse(raw.data(), first, a) && a.op == AC_OP_WRITE_ACK, "confirmação colada");
  check(acParse(raw.data() + first, raw.size() - first, b) && b.op == AC_OP_NOTIFY, "aviso colado");
  check(a.counter == 0x50F2 && b.counter == 0xD22D, "contadores colados");
}

static void feed(AcState& s, const char* hex) {
  auto raw = fromHex(hex);
  AcFrame f;
  check(acParse(raw.data(), raw.size(), f), std::string("aviso ") + hex);
  acApply(s, f);
}

static void testTracksState() {
  AcState s;
  acStateReset(s);
  check(s.power == -1 && s.setpoint == -1 && s.tempAe30 == -1 && s.timerSeconds == -1,
        "estado começa desconhecido");
  feed(s, "C60DD22DAD414C4C434131040106022C");        // ligou
  check(s.power == 1, "ligado");
  feed(s, "C60DD22EAD414C4C4341311000060139");        // resfriar
  check(s.mode == 1, "modo");
  feed(s, "C610D22FAD414C4C4341310432060000160014");  // setpoint 22
  check(s.setpoint == 22, "setpoint");
  feed(s, "C60DD23AAD414C4C434131100206042A");        // ventilador alto
  check(s.fan == 4, "ventilador");
  feed(s, "C60DD245AD414C4C4341311005060157");
  check(s.swing == 1, "oscilar");
  feed(s, "C610D1CCAD414C4C41453104360600001400F4");
  check(s.tempAe36 == 20, "temperatura do AE1");
  feed(s, "C60DD378AD414C4C4341310428060152");
  check(s.sleep == 1, "dormir");
  feed(s, "C610D37AAD414C4C43413104130600000E1069");
  check(s.timerSeconds == 3600, "timer de 1 h");
  feed(s, "C610D37DAD414C4C4341310413060000000070");
  check(s.timerSeconds == 0, "timer zerado");
  feed(s, "C60DD37FAD414C4C4341311001060168");
  check(s.indirect == 1, "ar indireto");
  feed(s, "C60DD381AD414C4C43413104380601BB");
  check(s.autoSense == 1, "auto sense");
  feed(s, "C60DD383AD414C4C43413104290601A8");
  check(s.display == 1, "display");
  feed(s, "C60DD1F1AD414C4C43413104010600F1");        // desligou
  check(s.power == 0, "desligado");
}

// Bytes da UART do ESP-01: quadros colados, lixo antes, quadro cortado e tamanho impossível.
static void testReadsByteStream() {
  const std::string ack = "C60C019BAD4E49554341310020077D";
  const std::string notify = "C60DD383AD414C4C43413104290601A8";
  const auto feed = [](AcReader& r, const std::string& hex, std::vector<AcFrame>& out) {
    for (uint8_t b : fromHex(hex)) {
      AcFrame f;
      if (r.push(b, f)) out.push_back(f);
    }
  };
  std::vector<AcFrame> got;
  AcReader r;
  feed(r, ack + notify, got);
  check(got.size() == 2, "quadros colados");
  check(got.size() == 2 && got[1].prop == AC_PROP_DISPLAY, "segundo quadro");

  got.clear();
  feed(r, "0011FF" + notify, got);
  check(got.size() == 1, "lixo antes do quadro");

  got.clear();
  feed(r, "C6FF" + notify, got);
  check(got.size() == 1, "tamanho impossível");

  got.clear();
  std::string damaged = notify;
  damaged[damaged.size() - 1] = '9';
  feed(r, damaged + notify, got);
  check(got.size() == 1, "checksum errado não perde o quadro seguinte");

  got.clear();
  feed(r, notify.substr(0, 12), got);
  r.reset();
  feed(r, notify, got);
  check(got.size() == 1, "quadro cortado descartado no silêncio");
}

// Silencioso, da captura de 01/11/2026: escrita e aviso.
static void testMute() {
  const struct { const char* hex; uint16_t counter; uint8_t value; } writes[] = {
    {"C60D803AAD434131525031042504013E", 0x803A, 1},
    {"C60D803BAD434131525031042504003E", 0x803B, 0},
  };
  for (const auto& w : writes) {
    uint8_t out[AC_MAX_FRAME];
    const size_t n = acBuild(out, sizeof(out), w.counter, "CA1", "RP1", AC_PROP_MUTE, AC_OP_WRITE,
                             &w.value, 1);
    check(std::vector<uint8_t>(out, out + n) == fromHex(w.hex), "escrita do silencioso");
  }
  AcState s;
  acStateReset(s);
  check(s.mute == -1, "silencioso começa desconhecido");
  const auto apply = [&s](const std::string& hex) {
    const std::vector<uint8_t> raw = fromHex(hex);
    AcFrame f;
    check(acParse(raw.data(), raw.size(), f), "aviso do silencioso válido");
    acApply(s, f);
  };
  // Testado no ar em 01/11/2026: com 00 ele não apita, com 01 apita.
  apply("C60D81EAAD414C4C434131042506019F");
  check(s.mute == 0, "valor 01 é com som");
  apply("C60D81EBAD414C4C434131042506009F");
  check(s.mute == 1, "valor 00 é silencioso");
}

int main() {
  testBuildsCapturedCommands();
  testBuildsThirdCapture();
  testBuildsCapturedHeartbeat();
  testBuildsCapturedAcks();
  testRejectsDamagedFrames();
  testSplitsMergedFrames();
  testTracksState();
  testReadsByteStream();
  testMute();
  printf(failures ? "%d falha(s)\n" : "Todos os testes passaram\n", failures);
  return failures ? 1 : 0;
}
