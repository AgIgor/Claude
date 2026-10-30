#pragma once
#include <Arduino.h>

// Dados ao vivo (escritos pela tarefa do motor, lidos pela web)
struct LiveData {
  float rpm, map, clt, iat, o2, tps, vbat;
  float mapDot;
  float ve, lambdaTarget, warmPct, aePct, afterStartPct, clCorrPct;
  float reqFuelMs, pwMs, dutyPct, advance, dwellMs;
  uint8_t sync, camSync, sequential, cranking, running;
  uint8_t fan1, fan2, pump, revCut, dfco, clActive;
  uint8_t sensorFault;  // bits: 1 MAP, 2 CLT, 4 IAT, 8 TPS
  float idlePos, idleTarget, idleAdvTrim;
  uint8_t idleClosedLoop, acReq, acOn;
  uint16_t knockLevel[4];  // ultimo valor lido por canal de ignicao (0-1023)
  float knockRetard[4];
  uint32_t knockCount;
  uint32_t syncLoss, revs;
  uint32_t loopUs;
};
extern LiveData live;

// Posicao manual da marcha lenta pela web (%); negativo = automatico
extern volatile float idleOverride;

// Pedido de teste de saida (feito pela web, executado pela tarefa do motor)
enum TestType : uint8_t {
  TEST_INJ = 0, TEST_IGN = 1, TEST_FAN1 = 2, TEST_FAN2 = 3, TEST_PUMP = 4, TEST_AC = 5
};
struct TestRequest {
  volatile bool pending;
  uint8_t type;
  uint8_t ch;
  uint16_t count;
  float ms;
  uint16_t intervalMs;
};
extern TestRequest testReq;

void engineStart();
