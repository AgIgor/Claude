#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define TBL_N 16    // mapas 16x16
#define CURVE_N 10  // curvas de 10 pontos

struct Table3D {
  float x[TBL_N];          // eixo RPM
  float y[TBL_N];          // eixo MAP (kPa)
  float z[TBL_N][TBL_N];   // z[linha MAP][coluna RPM]
};

struct Curve {
  float x[CURVE_N];
  float y[CURVE_N];
};

// Todos os parametros escalares. X(nome, valor_padrao)
// Tipos: F = float, B = uint8_t (0/1 ou opcao)
#define SETTINGS_FIELDS(F, B)                                                 \
  /* Motor / combustivel */                                                  \
  F(displacementCc, 1600)  /* cilindrada total */                            \
  F(injFlowCcMin, 190)     /* vazao do bico a 3 bar */                       \
  F(stoichAfr, 13.2)       /* gasolina E27 ~13.2, E100 ~9.0, pura 14.7 */    \
  F(fuelDensity, 0.75)     /* g/cm3 */                                       \
  /* Roda fonica / fase */                                                   \
  B(teeth, 60)                                                               \
  B(missingTeeth, 2)                                                         \
  B(ckpRisingEdge, 0)                                                        \
  F(tdcOffset, 90)         /* graus do 1o dente apos a falha ate o PMS cil.1 (0-719) */ \
  B(camEnabled, 1)                                                           \
  B(camRisingEdge, 0)                                                        \
  B(camCountA, 1)          /* pulsos de fase na volta que antecede a volta 0 */ \
  B(camCountB, 0)          /* pulsos de fase na outra volta (diferente de A) */ \
  /* Injecao */                                                              \
  B(forceSemiSeq, 0)       /* 1 = sempre semissequencial */                  \
  F(injEndAngle, 355)      /* fim da injecao, graus antes do PMS de compressao */ \
  F(crankRpm, 400)         /* abaixo disso = partida */                      \
  F(afterStartPct, 30)     /* enriquecimento pos-partida inicial (%) */      \
  F(afterStartSec, 10)                                                       \
  F(aeMapdotThresh, 80)    /* kPa/s */                                       \
  F(aeTpsdotThresh, 150)   /* %/s */                                         \
  F(aePct, 30)                                                               \
  F(aeDurationMs, 250)                                                       \
  B(dfcoEnabled, 1)        /* corte em desaceleracao */                      \
  F(dfcoRpm, 1800)                                                           \
  F(dfcoMap, 25)                                                             \
  F(maxDutyPct, 85)                                                          \
  /* Ignicao */                                                              \
  F(crankAdvance, 10)                                                        \
  F(crankDwellMs, 4.0)                                                       \
  F(maxDwellMs, 6.0)       /* seguranca: bobina nunca carrega mais que isso */ \
  B(fixedAdvanceOn, 0)     /* trava o avanco (calibrar com pistola) */       \
  F(fixedAdvance, 10)                                                        \
  F(outputLatencyUs, 0)    /* compensacao do atraso do optoacoplador */      \
  F(revLimit, 6500)                                                          \
  F(revLimitHyst, 200)                                                       \
  /* Malha fechada (sonda banda estreita) */                                 \
  B(closedLoop, 1)                                                           \
  F(clMinClt, 70)                                                            \
  F(clMinRpm, 900)                                                           \
  F(clMaxRpm, 4000)                                                          \
  F(clMaxMap, 80)                                                            \
  F(clDelaySec, 30)        /* espera apos a partida (sonda aquecer) */       \
  F(clStepPct, 0.5)        /* passo do integrador a cada 50 ms */            \
  F(clLimitPct, 15)                                                          \
  F(o2Threshold, 0.45)     /* V: acima = rico */                             \
  /* Sensores */                                                             \
  F(mapV1, 0.4) F(mapKpa1, 20)                                               \
  F(mapV2, 4.65) F(mapKpa2, 105)                                             \
  F(mapFilter, 0.3)        /* 0..1, maior = mais rapido */                   \
  F(ntcPullup, 2490)       /* resistor de pull-up para 3,3 V */              \
  F(cltR25, 2500) F(cltBeta, 3450)                                           \
  F(iatR25, 2500) F(iatBeta, 3450)                                           \
  F(divMap, 1.5)           /* divisor da entrada 0-5 V (Vreal/Vadc) */       \
  F(divTps, 1.5)                                                             \
  F(divVbat, 5.7)                                                            \
  B(tpsEnabled, 0)                                                           \
  F(tpsVmin, 0.5) F(tpsVmax, 4.5)                                            \
  /* Ventoinhas / bomba */                                                   \
  F(fan1On, 95) F(fan1Off, 90)                                               \
  F(fan2On, 100) F(fan2Off, 95)                                              \
  F(pumpPrimeSec, 2)                                                         \
  /* Marcha lenta */                                                         \
  B(idleType, 0)           /* 0 nenhum, 1 valvula PWM, 2 motor de passo */   \
  F(idlePwmFreq, 200)                                                        \
  F(idleStepperMax, 200)   /* passos de totalmente fechado a aberto */       \
  F(idleStepMs, 3)         /* intervalo entre passos */                      \
  B(idleStepperDirInvert, 0)                                                 \
  F(idleCrankPos, 50)      /* % com motor parado / em partida */             \
  F(idleKp, 0.02)          /* % por rpm de erro */                           \
  F(idleKi, 0.01)          /* % por rpm por segundo */                       \
  F(idleIntLimit, 20)      /* limite do integrador (%) */                    \
  F(idleTpsMax, 2)         /* com TPS: abaixo disso = marcha lenta */        \
  F(idleActivationRpm, 400)/* sem TPS: lenta se rpm < alvo + isso */         \
  F(idleDashpotPct, 8)     /* abertura extra fora da lenta */                \
  F(idleDashpotDecay, 4)   /* %/s ao voltar para a lenta */                  \
  F(idleAcAdder, 10)                                                         \
  F(idleAcRpmUp, 100)                                                        \
  F(idleFanAdder, 3)                                                         \
  F(idleAdvGain, 0.04)     /* graus por rpm de erro */                       \
  F(idleAdvLimit, 6)                                                         \
  /* Ar-condicionado */                                                      \
  B(acEnabled, 0)                                                            \
  B(acInputActiveHigh, 0)                                                    \
  F(acDelayMs, 500)        /* sobe a lenta antes de acoplar */               \
  F(acMinRpm, 550)                                                           \
  F(acMaxRpm, 6000)                                                          \
  F(acMaxClt, 112)                                                           \
  F(acWotTps, 85)          /* TPS acima disso desliga o compressor */        \
  F(acWotCutSec, 5)                                                          \
  B(acFan, 1)              /* ventoinha junto com o compressor: 0, 1 ou 2 */ \
  /* Detonacao (TPIC8101) */                                                 \
  B(knockEnabled, 0)                                                         \
  F(knockFreqKhz, 7.0)     /* ~ 900 / (pi x raio do cilindro em mm) */       \
  B(knockGainIdx, 14)      /* 0..63 (0 = ganho 2,0  63 = 0,111) */           \
  B(knockIntegIdx, 20)     /* 0..31 (constante de integracao 40..600 us) */  \
  F(knockWinStart, 10)     /* graus apos o PMS */                            \
  F(knockWinLen, 50)       /* graus */                                       \
  F(knockMinRpm, 1500)                                                       \
  F(knockMinMap, 45)                                                         \
  F(knockRetardStep, 2)                                                      \
  F(knockMaxRetard, 8)                                                       \
  F(knockRecoverDegSec, 1)                                                   \
  /* Saidas */                                                               \
  B(injInvert, 0)                                                            \
  B(ignInvert, 0)

#define SF_DECL_F(n, d) float n;
#define SF_DECL_B(n, d) uint8_t n;

struct Settings {
  uint32_t magic;
  uint32_t size;
  SETTINGS_FIELDS(SF_DECL_F, SF_DECL_B)
  char wifiSsid[32];
  char wifiPass[64];
  Curve warmup;      // CLT -> % enriquecimento
  Curve crankEnrich; // CLT -> % na partida
  Curve deadTime;    // VBAT -> ms tempo morto do bico
  Curve dwell;       // VBAT -> ms carga da bobina
  Curve cltAdvance;  // CLT -> graus somados ao avanco
  Curve idleTarget;  // CLT -> rpm alvo da marcha lenta
  Curve idleBase;    // CLT -> % posicao base da valvula
  Curve knockThreshold;  // RPM -> nivel (0-1023) acima do qual e detonacao
  Table3D ve;        // eficiencia volumetrica (%)
  Table3D ign;       // avanco (graus APMS)
  Table3D lambda;    // lambda alvo
};

extern Settings cfg;
extern SemaphoreHandle_t cfgMutex;  // protege cfg entre a tarefa do motor e a web
extern volatile bool cfgDirty;      // RAM diferente da flash

void settingsInit();
void settingsDefaults(Settings &s);
bool settingsSave();
void settingsToJson(const Settings &s, JsonObject o);
void settingsFromJson(Settings &s, JsonObjectConst o);  // aplica so as chaves presentes

float curveLookup(const Curve &c, float x);
float tableLookup(const Table3D &t, float rpm, float map);
