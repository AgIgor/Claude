// Teste no PC do decodificador: gera sinais UART sintéticos, com jitter e pausas entre bytes.
// Compile com: g++ -std=c++17 -I../src test_decode.cpp ../src/uart_decode.cpp -o test_decode
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include "uart_decode.h"

static const uint32_t TICK_HZ = 120000000;
static int failures = 0;

static void check(bool ok, const char* what, uint32_t baud, const char* fmt) {
  if (ok) return;
  printf("FALHOU: %s (%u %s)\n", what, baud, fmt);
  failures++;
}

struct Signal {
  std::vector<uint32_t> edges;
  std::mt19937 rng{1234};
  double t = 0;
  bool level;
  bool idle;
  double jitterTicks;

  Signal(bool idleLevel, double jitter) : level(idleLevel), idle(idleLevel), jitterTicks(jitter) {}

  void set(bool lvl, double dur) {
    if (lvl != level) {
      std::uniform_real_distribution<double> d(0, jitterTicks);
      edges.push_back(((uint32_t)(t + d(rng)) << 1) | (lvl ? 1 : 0));
      level = lvl;
    }
    t += dur;
  }

  void byte(uint8_t v, const UartFormat& f, double bit) {
    const bool on = idle, off = !idle;  // on = bit 1
    set(off, bit);
    int ones = 0;
    for (int b = 0; b < f.dataBits; b++) {
      bool one = v >> b & 1;
      ones += one;
      set(one ? on : off, bit);
    }
    if (f.parity != 'N') {
      bool p = f.parity == 'E' ? ones & 1 : !(ones & 1);
      set(p ? on : off, bit);
    }
    set(on, bit * f.stopBits);
  }
};

static std::vector<uint8_t> payload(size_t n, std::mt19937& rng) {
  std::vector<uint8_t> v(n);
  for (auto& b : v) b = rng() & 0xFF;
  return v;
}

// 8N2, 8E2 e 8O2 são indistinguíveis da versão de 1 stop bit quando há pausa entre os bytes,
// e ler com 1 stop bit funciona igual.
static bool sameForReading(const UartFormat& a, const UartFormat& b) {
  return a.parity == b.parity;
}

static void testRoundTrip() {
  const uint32_t bauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
  for (uint32_t baud : bauds) {
    for (int f = 0; f < UART_FORMAT_COUNT; f++) {
      for (int idle = 0; idle <= 1; idle++) {
        const UartFormat& fmt = UART_FORMATS[f];
        const double bit = (double)TICK_HZ / baud;
        // Jitter de 2 us, o atraso esperado da interrupção
        Signal s(idle, 240);
        std::mt19937 rng(baud + f);
        auto data = payload(64, rng);
        for (auto b : data) s.byte(b, fmt, bit);

        DecodeStats st;
        uint8_t out[128];
        size_t n = uartDecode(s.edges.data(), s.edges.size(), idle, baud, fmt, TICK_HZ, out,
                              sizeof(out), st);
        check(n == data.size() && !memcmp(out, data.data(), n), "ida e volta", baud, fmt.name);
        check(st.framing + st.parity + st.glitch == 0, "erros de decodificação", baud, fmt.name);
      }
    }
  }
}

static void testAutoDetect() {
  const uint32_t bauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
  for (uint32_t baud : bauds) {
    for (int f = 0; f < UART_FORMAT_COUNT; f++) {
      for (int gaps = 0; gaps <= 1; gaps++) {
        const UartFormat& fmt = UART_FORMATS[f];
        const double bit = (double)TICK_HZ / baud;
        Signal s(true, 240);
        std::mt19937 rng(baud * 7 + f);
        for (int frame = 0; frame < 6; frame++) {
          for (auto b : payload(20, rng)) {
            s.byte(b, fmt, bit);
            if (gaps) s.set(true, bit * 3);
          }
          s.set(true, bit * 40);
        }

        std::vector<uint32_t> scratch(s.edges.size());
        UartAnalysis a;
        uartAnalyze(s.edges.data(), s.edges.size(), true, TICK_HZ, scratch.data(), a);
        check(a.count > 0, "sem candidato", baud, fmt.name);
        if (!a.count) continue;
        const UartCandidate& c = a.top[0];
        if (c.baud != baud || !sameForReading(UART_FORMATS[c.fmt], fmt)) {
          printf("  obtido %u %s nota %.3f (est %u)\n", c.baud, UART_FORMATS[c.fmt].name, c.score,
                 a.estBaud);
        }
        check(c.baud == baud, "baud detectado", baud, fmt.name);
        check(sameForReading(UART_FORMATS[c.fmt], fmt), "formato detectado", baud, fmt.name);
        check(c.score >= 0.98f, "nota alta", baud, fmt.name);
      }
    }
  }
}

static void testNonStandardBaud() {
  const uint32_t baud = 31250;
  const UartFormat& fmt = UART_FORMATS[0];
  Signal s(true, 120);
  std::mt19937 rng(9);
  for (auto b : payload(120, rng)) s.byte(b, fmt, (double)TICK_HZ / baud);
  std::vector<uint32_t> scratch(s.edges.size());
  UartAnalysis a;
  uartAnalyze(s.edges.data(), s.edges.size(), true, TICK_HZ, scratch.data(), a);
  check(a.count > 0 && a.top[0].baud > baud * 0.97 && a.top[0].baud < baud * 1.03,
        "baud fora do padrão", baud, fmt.name);
}

static void testNoiseIsNotLocked() {
  std::mt19937 rng(5);
  std::uniform_int_distribution<uint32_t> d(1200, 200000);
  std::vector<uint32_t> edges;
  uint32_t t = 0;
  bool lvl = false;
  for (int i = 0; i < 3000; i++) {
    t += d(rng);
    edges.push_back(t << 1 | lvl);
    lvl = !lvl;
  }
  std::vector<uint32_t> scratch(edges.size());
  UartAnalysis a;
  uartAnalyze(edges.data(), edges.size(), true, TICK_HZ, scratch.data(), a);
  check(a.count == 0 || a.top[0].score < 0.98f, "ruído não pode travar", 0, "ruido");
}

// Reproduz a captura real de 31/10/2026: pulsos baixos soltos, com largura decrescente.
static void testSparsePulsesAreNotBytes() {
  std::mt19937 rng(7);
  std::geometric_distribution<int> width(0.35);
  std::uniform_int_distribution<uint32_t> spacing(TICK_HZ / 5000, TICK_HZ / 200);
  const double bit = (double)TICK_HZ / 115200;
  std::vector<uint32_t> edges;
  uint32_t t = 0;
  for (int i = 0; i < 1500; i++) {
    t += spacing(rng);
    edges.push_back(t << 1);
    t += (uint32_t)(bit * (1 + width(rng)));
    edges.push_back(t << 1 | 1);
  }
  std::vector<uint32_t> scratch(edges.size());
  UartAnalysis a;
  uartAnalyze(edges.data(), edges.size(), true, TICK_HZ, scratch.data(), a);
  check(a.count == 0, "pulsos soltos não podem virar candidato", 115200, "8N1");
}

// Quadros de ar têm muitos 00: isso não pode ser confundido com ruído.
static void testZeroHeavyFramesAreAccepted() {
  const UartFormat& fmt = UART_FORMATS[1];
  const uint8_t frame[] = {0xAA, 0x0A, 0x40, 0x03, 0x00, 0x18, 0x00, 0x00, 0x55, 0x00, 0x00, 0x7B};
  Signal s(true, 240);
  for (int r = 0; r < 8; r++) {
    for (uint8_t b : frame) s.byte(b, fmt, (double)TICK_HZ / 9600);
    s.set(true, TICK_HZ / 50);
  }
  std::vector<uint32_t> scratch(s.edges.size());
  UartAnalysis a;
  uartAnalyze(s.edges.data(), s.edges.size(), true, TICK_HZ, scratch.data(), a);
  check(a.count > 0 && a.top[0].baud == 9600 && a.top[0].fmt == 1, "quadro com muitos zeros", 9600,
        fmt.name);
}

int main() {
  testRoundTrip();
  testAutoDetect();
  testNonStandardBaud();
  testNoiseIsNotLocked();
  testSparsePulsesAreNotBytes();
  testZeroHeavyFramesAreAccepted();
  printf(failures ? "%d falha(s)\n" : "Todos os testes passaram\n", failures);
  return failures ? 1 : 0;
}
