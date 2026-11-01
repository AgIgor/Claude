#pragma once
// Captura passiva: a interrupção grava cada borda das duas linhas e uma tarefa separa as rajadas,
// analisa baud/formato e decodifica os quadros.
#include <Arduino.h>
#include <freertos/ringbuf.h>
#include "config.h"
#include "uart_decode.h"

struct FrameHeader {
  uint64_t tUs;
  uint16_t len;
  uint16_t errors;
  uint8_t ch;
};

struct CaptureConfig {
  uint32_t baud;   // 0 = ainda sem baud definido
  int8_t fmt;
  uint16_t gapMs;
  bool autoLock;   // trava sozinho quando a análise tiver confiança
};

struct ChannelStats {
  uint32_t edges;
  uint32_t bursts;
  uint32_t frames;
  uint32_t bytes;
  uint32_t framing;
  uint32_t parity;
  uint32_t overflows;
  uint32_t droppedFrames;
  bool level;
  uint32_t sampleEdges;
  uint32_t hist[PULSE_BUCKETS];
  UartAnalysis analysis;
};

namespace Capture {
void begin(const CaptureConfig& cfg);
CaptureConfig config();
void setManual(uint32_t baud, int fmt);
void setAuto();                    // volta a detectar e descarta as amostras
void setGap(uint16_t gapMs);
void setFrameHook(void (*hook)(int ch, const uint8_t* data, size_t n));
void resetAnalysis();              // descarta as amostras, mantendo o baud travado
bool takeConfigChanged();          // true uma vez após a trava automática, para salvar na flash
void stats(int ch, ChannelStats& out);
String scopeJson();
uint64_t nowUs();
RingbufHandle_t frames();          // cada item: FrameHeader seguido dos bytes
}
