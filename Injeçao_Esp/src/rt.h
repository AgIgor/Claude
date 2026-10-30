#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Nucleo de tempo real: decodificador da roda fonica + agendador de eventos.
// Tambem abre/fecha a janela de leitura do sensor de detonacao (INT/HOLD).
// Roda inteiro em interrupcoes no nucleo 1, so com matematica inteira
// (o ESP32 nao salva o contexto da FPU dentro de ISR).
//
// Angulos em decimos de grau, na coordenada do ciclo:
//   0 = primeiro dente apos a falha, na volta de fase 0
//   ciclo = 7200 (sequencial, 720 graus) ou 3600 (centelha perdida)
// ---------------------------------------------------------------------------

struct RtParams {
  volatile int32_t ignCharge10[4];  // inicio da carga da bobina
  volatile int32_t ignFire10[4];    // centelha
  volatile int32_t injStart10[4];   // abertura do bico
  volatile uint32_t ignMask[4];     // pinos de cada canal (0 = canal desligado)
  volatile uint32_t injMask[4];
  volatile int32_t knockStart10[4]; // abertura da janela de detonacao (por canal de ignicao)
  volatile uint32_t knockWinUs;
  volatile uint8_t knockEnabled;
  volatile uint32_t injPwUs;        // largura do pulso (ja com tempo morto)
  volatile uint32_t maxDwellUs;
  volatile uint32_t latencyUs;
  volatile uint8_t sequential;      // 1 = ciclo de 720 graus (exige fase)
  volatile uint8_t ignEnabled;
  volatile uint8_t injEnabled;
  volatile uint8_t ignInvert;
  volatile uint8_t injInvert;
};
extern RtParams rt;

struct DecoderSnapshot {
  bool crankSync;
  bool camSync;
  uint32_t revTimeUs;      // tempo da ultima volta completa
  uint32_t toothPeriodUs;
  uint64_t lastToothUs;    // 0 = nenhum dente desde o reset
  uint64_t nowUs;
  uint32_t syncLoss;
  uint32_t revs;
};

void rtInit();  // chamar a partir de uma tarefa fixada no nucleo 1
void rtApplyTrigger(uint8_t teeth, uint8_t missing, bool ckpRising, bool camEnabled, bool camRising,
                    uint8_t camCountA, uint8_t camCountB);
// Janelas de detonacao fechadas desde o inicio; ch recebe o canal da ultima
uint32_t rtKnockEvents(uint8_t &ch);
void rtSnapshot(DecoderSnapshot &s);
void rtResetSync();   // motor parou: perde sincronismo e desliga tudo
void rtKillOutputs();
bool rtTestPulse(bool ign, uint8_t ch, uint32_t durUs);
