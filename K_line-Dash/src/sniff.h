#pragma once
#include <Arduino.h>
#include <mcp2515.h>

// Modo escuta: tabela dos IDs que circulam na rede (ex. ECU <-> painel de instrumentos)
// e sinais mapeados (ID + bytes -> valor do painel web).
namespace Sniff {
  void begin(void (*valueSink)(const char* key, float v));
  void reset();                        // zera a tabela e o mín/máx
  void onFrame(const can_frame& f);    // chamado para cada quadro recebido
  void tick();                         // 1x por segundo: quadros/s por ID
  void addOverflows(uint32_t n);
  uint16_t idCount();
  float fps();
  String json();                       // tabela para /api/sniff

  // Sinais, um por linha: chave,ID,byte,tamanho,ordem,mult,soma[,máscara]
  //   ex.: rpm,201,0,2,B,0.25,0     ect,420,0,1,B,1,-40
  // ordem: B = big-endian (mais comum em Ford), L = little-endian; acrescente S para valor com sinal.
  bool setSignals(const String& text, String& err);
  String signals();
}
