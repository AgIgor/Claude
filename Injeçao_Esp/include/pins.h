#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Mapa de pinos - ESP32 WROOM-32 DevKit
// Evitados: 6-11 (flash), 0/2/12/15 (strapping), 1/3 (USB serial).
// Saidas de bico/bobina ficam todas abaixo do GPIO32 para serem acionadas
// com uma unica escrita de registrador dentro da interrupcao.
// ---------------------------------------------------------------------------

// Entradas digitais (34/35 sao somente entrada e NAO tem pull-up interno:
// o pull-up precisa estar na placa)
#define PIN_CKP 34  // sensor de rotacao (roda fonica 60-2)
#define PIN_CMP 35  // sensor de fase (comando)

// Saidas (via optoacoplador)
static const uint8_t PIN_INJ[4] = {25, 26, 27, 13};  // bicos cil. 1..4
static const uint8_t PIN_IGN[4] = {16, 17, 21, 22};  // bobinas cil. 1..4
#define PIN_FAN1 32
#define PIN_FAN2 33
#define PIN_PUMP 14  // rele da bomba de combustivel
#define PIN_AC 12    // rele do compressor. Strapping: PRECISA estar em nivel baixo no boot
                     // (o LED do opto ate o GND + pull-down de 10k garantem isso)

// Marcha lenta: valvula PWM (2 fios) ou motor de passo via driver (STEP/DIR)
#define PIN_IDLE 4   // PWM da valvula ou STEP do driver
#define PIN_IDLE_DIR 2  // DIR do driver (motor de passo). Sem motor de passo vira LED de status.
#define PIN_LED 2       // LED azul do DevKit: aceso = sincronizado

// Entradas extras (36/39 sao somente entrada, sem pull-up interno)
#define PIN_AC_REQ 36   // pedido do ar-condicionado (botao + pressostatos)
#define PIN_SPARE_IN 39 // livre

// Barramento SPI (VSPI): MCP3208 + TPIC8101 (detonacao)
#define PIN_SPI_SCK 18
#define PIN_SPI_MISO 19
#define PIN_SPI_MOSI 23
#define PIN_ADC_CS 5
#define PIN_KNOCK_CS 0    // strapping: precisa de pull-up (ja existe no DevKit)
#define PIN_KNOCK_HOLD 15 // INT/HOLD do TPIC8101: alto = integrando (janela aberta)

// Canais do MCP3208
enum AdcChannel : uint8_t {
  ADC_MAP = 0,
  ADC_CLT = 1,  // NTC agua
  ADC_IAT = 2,  // NTC ar
  ADC_O2 = 3,   // sonda lambda (banda estreita 0-1 V)
  ADC_TPS = 4,  // borboleta (opcional)
  ADC_VBAT = 5, // tensao da bateria (divisor)
};

#define ADC_VREF 3.3f  // MCP3208 alimentado e referenciado em 3,3 V
