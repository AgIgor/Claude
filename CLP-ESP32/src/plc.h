#pragma once
#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Runtime do CLP: o programa Ladder (JSON) é compilado para um bytecode de
// pilha booleana e executado a cada ciclo de scan.
//
// Endereços:
//   I0..  entradas digitais      Q0..  saídas digitais     M0..  bits internos
//   S0 1º scan, S1 pisca 1 Hz, S2 sempre ON
//   T0..  bit Q do temporizador  T0.ET tempo decorrido (ms)
//   C0..  bit Q do contador      C0.CV valor atual
//   AI0.. entrada analógica (mV) D0..  registrador inteiro
// ---------------------------------------------------------------------------

enum class Area : uint8_t { I, Q, M, S, T, C, AI, D, TET, CCV, K };

struct Ref {
  Area area = Area::K;
  uint16_t idx = 0;
  int32_t k = 0;  // valor da constante quando area == K
};

enum Op : uint8_t {
  OP_PUSH0, OP_PUSH1, OP_AND, OP_OR, OP_POP,
  OP_LD, OP_LDN, OP_RE, OP_FE, OP_CMP,
  OP_OUT, OP_SET, OP_RST, OP_TON, OP_TOF, OP_TP, OP_CTU, OP_CTD, OP_MOV,
};

enum CmpOp : uint8_t { CMP_GT, CMP_GE, CMP_LT, CMP_LE, CMP_EQ, CMP_NE };

struct Instr {
  uint8_t op;
  uint8_t cmp;    // OP_CMP
  uint16_t slot;  // memória de borda (OP_RE/FE/CTU/CTD)
  Ref a;          // operando principal / destino
  Ref b;          // segundo operando (CMP, MOV) ou preset em b.k
};

struct PlcTimer { int32_t pt, et; bool q, in; };
struct PlcCounter { int32_t pv, cv; bool q; };

struct PlcImage {
  uint32_t i, q, s;
  uint32_t m[(PLC_NUM_M + 31) / 32];
  int32_t ai[PLC_NUM_AI];
  int32_t d[PLC_NUM_D];
  PlcTimer t[PLC_NUM_T];
  PlcCounter c[PLC_NUM_C];
};

struct PlcSnapshot {
  PlcImage img;
  uint32_t qPhys;
  uint32_t forceIEn, forceIVal, forceQEn, forceQVal;
  bool running;
  uint32_t scanUs, scanMaxUs, scanCount;
  uint16_t instrCount, rungCount;
};

class Plc {
 public:
  void begin();
  void scan();  // chamado pela tarefa de scan

  // Compila e, se válido, troca o programa em execução. err recebe a mensagem.
  bool loadProgram(const char* json, size_t len, String& err);

  void setRunning(bool run);
  void setForce(char area, uint8_t idx, int8_t mode);  // mode: -1 libera, 0 força 0, 1 força 1
  bool setD(uint8_t idx, int32_t value);
  void snapshot(PlcSnapshot& out);
  void resetScanMax();

 private:
  void exec(uint32_t dtMs);
  void resetVolatile();
  bool readBit(const Ref& r) const;
  int32_t readInt(const Ref& r) const;
  void writeBit(const Ref& r, bool v);

  SemaphoreHandle_t mutex_ = nullptr;
  PlcImage img_{};
  Instr* code_ = nullptr;
  uint16_t codeLen_ = 0;
  uint16_t rungCount_ = 0;
  bool edges_[PLC_MAX_EDGES] = {};
  bool running_ = false;
  bool firstScan_ = true;
  uint32_t lastScanMs_ = 0;
  bool aiInit_ = false;
  int32_t aiFilt_[PLC_NUM_AI] = {};  // valor filtrado << PLC_AI_FILTER_SHIFT
  uint32_t qPhys_ = 0;
  uint32_t fIEn_ = 0, fIVal_ = 0, fQEn_ = 0, fQVal_ = 0;
  uint32_t scanUs_ = 0, scanMaxUs_ = 0, scanCount_ = 0;
};

extern Plc g_plc;

// Exposto para testes/diagnóstico: compila sem carregar.
bool plcCompile(const char* json, size_t len, Instr* out, uint16_t& outLen,
                uint16_t& rungCount, String& err);
