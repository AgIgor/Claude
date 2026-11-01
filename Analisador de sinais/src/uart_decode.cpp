#include "uart_decode.h"
#include <algorithm>

const UartFormat UART_FORMATS[] = {
  {"8N1", 8, 'N', 1}, {"8E1", 8, 'E', 1}, {"8O1", 8, 'O', 1},
  {"8N2", 8, 'N', 2}, {"8E2", 8, 'E', 2}, {"8O2", 8, 'O', 2},
};
const int UART_FORMAT_COUNT = sizeof(UART_FORMATS) / sizeof(UART_FORMATS[0]);

static const uint32_t STD_BAUDS[] = {
  300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600, 76800, 115200,
};
static const int MIN_GOOD_BYTES = 8;
static const size_t MIN_PULSES = 20;
static const uint32_t MIN_DISTINCT_BYTES = 6;
static const uint32_t MAX_ONE_PULSE_PERCENT = 85;

float DecodeStats::score() const {
  uint32_t total = good + framing + parity + glitch;
  return total ? (float)good / total : 0;
}

static inline uint32_t edgeTime(uint32_t e) { return e >> 1; }
static inline bool edgeLevel(uint32_t e) { return e & 1; }

// Nível da linha no instante t. j só avança: as amostras de um byte vêm em ordem.
static inline bool levelAt(const uint32_t* edges, size_t n, bool idle, uint32_t t, size_t& j) {
  while (j < n && edgeTime(edges[j]) <= t) j++;
  return j ? edgeLevel(edges[j - 1]) : idle;
}

// Pulso isolado lido como byte: FF, FE, FC, F8, F0, E0, C0, 80 ou 00.
static inline bool isOnePulse(uint8_t v) {
  const uint8_t inv = ~v;
  return (inv & (uint8_t)(inv + 1)) == 0;
}

size_t uartDecode(const uint32_t* edges, size_t n, bool idle, uint32_t baud, const UartFormat& f,
                  uint32_t tickHz, uint8_t* out, size_t outMax, DecodeStats& st) {
  st = DecodeStats{};
  if (!baud) return 0;
  const double bit = (double)tickHz / baud;
  size_t written = 0, i = 0;
  uint32_t seen[8] = {};

  while (i < n) {
    if (edgeLevel(edges[i]) == idle) { i++; continue; }  // procura o início do start bit
    const uint32_t t0 = edgeTime(edges[i]);
    size_t j = i;
    auto sampleOne = [&](int slot) {  // true = nível de repouso, ou seja, bit 1
      uint32_t t = t0 + (uint32_t)(bit * (slot + 0.5));
      return levelAt(edges, n, idle, t, j) == idle;
    };

    if (sampleOne(0)) {  // voltou ao repouso antes do meio do bit: era ruído
      st.glitch++;
      i++;
      continue;
    }

    int slot = 1;
    uint32_t value = 0;
    int ones = 0;
    for (int b = 0; b < f.dataBits; b++, slot++) {
      if (sampleOne(slot)) { value |= 1u << b; ones++; }
    }

    bool parityOk = true;
    if (f.parity != 'N') {
      if (sampleOne(slot++)) ones++;
      parityOk = (ones & 1) == (f.parity == 'O' ? 1 : 0);
    }

    bool frameOk = true;
    for (int s = 0; s < f.stopBits; s++) {
      if (!sampleOne(slot++)) frameOk = false;
    }

    if (!frameOk) st.framing++;
    else if (!parityOk) st.parity++;
    else {
      st.good++;
      if (isOnePulse(value)) st.onePulse++;
      seen[value >> 5] |= 1u << (value & 31);
    }
    if (out && written < outMax) out[written++] = (uint8_t)value;
    i = j > i ? j : i + 1;  // o próximo start bit vem depois da última amostra
  }
  for (uint32_t w : seen) st.distinct += __builtin_popcount(w);
  return written;
}

// Menor largura que se repete: é a melhor estimativa do tempo de 1 bit.
static float bitTicks(uint32_t* widths, size_t count) {
  std::sort(widths, widths + count);
  const size_t minRepeat = std::max<size_t>(3, count / 100);
  for (size_t i = 0; i + minRepeat < count; i++) {
    uint32_t limit = widths[i] + widths[i] / 4;
    if (widths[i + minRepeat] > limit) continue;
    uint64_t sum = 0;
    size_t k = i;
    for (; k < count && widths[k] <= limit; k++) sum += widths[k];
    return (float)sum / (k - i);
  }
  return 0;
}

static void tryCandidate(const uint32_t* edges, size_t n, bool idle, uint32_t tickHz, uint32_t baud,
                         UartAnalysis& out) {
  for (int f = 0; f < UART_FORMAT_COUNT; f++) {
    DecodeStats st;
    uartDecode(edges, n, idle, baud, UART_FORMATS[f], tickHz, nullptr, 0, st);
    if (st.good < MIN_GOOD_BYTES) continue;
    // Pulsos soltos passam em qualquer checagem de UART, então a nota sozinha não basta.
    if (st.distinct < MIN_DISTINCT_BYTES) continue;
    if (st.onePulse * 100 > st.good * MAX_ONE_PULSE_PERCENT) continue;
    UartCandidate c{baud, (int8_t)f, st.score(), st.good, st.framing + st.parity + st.glitch};

    // Empate técnico fica com quem entrou primeiro: baud padrão e formato mais simples.
    int pos = out.count;
    while (pos > 0 && c.score > out.top[pos - 1].score + 0.005f) pos--;
    const int cap = sizeof(out.top) / sizeof(out.top[0]);
    if (pos >= cap) continue;
    for (int k = std::min(out.count, cap - 1); k > pos; k--) out.top[k] = out.top[k - 1];
    out.top[pos] = c;
    if (out.count < cap) out.count++;
  }
}

void uartAnalyze(const uint32_t* edges, size_t n, bool idle, uint32_t tickHz, uint32_t* scratch,
                 UartAnalysis& out) {
  out = UartAnalysis{};
  if (n < 2) return;
  size_t pulses = 0;
  for (size_t i = 1; i < n; i++) {
    uint32_t w = edgeTime(edges[i]) - edgeTime(edges[i - 1]);
    if (edgeLevel(edges[i]) != edgeLevel(edges[i - 1])) scratch[pulses++] = w;
  }
  out.pulses = pulses;
  if (pulses < MIN_PULSES) return;

  const float bit = bitTicks(scratch, pulses);
  if (bit <= 0) return;
  out.minPulseUs = bit * 1e6f / tickHz;
  out.estBaud = (uint32_t)(tickHz / bit + 0.5f);

  // Só testa o que combina com o pulso medido; um baud muito mais baixo também "acha" bytes.
  for (uint32_t b : STD_BAUDS) {
    const float ratio = (float)b / out.estBaud;
    if (ratio >= 0.9f && ratio <= 1.1f) tryCandidate(edges, n, idle, tickHz, b, out);
  }
  // Baud fora do padrão só entra se ficar claramente melhor que os padrões.
  tryCandidate(edges, n, idle, tickHz, out.estBaud, out);
}
