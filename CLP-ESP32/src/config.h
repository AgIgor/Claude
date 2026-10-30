#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Mapa de pinos (ESP32 DevKit / WROOM-32) - protótipo com GPIO direto.
// Na placa própria, troque a implementação em io_hal.cpp (ex.: expansores
// I2C, ADC externo) sem mexer no resto do firmware.
// ---------------------------------------------------------------------------

#define PLC_NUM_DI 10
#define PLC_NUM_DO 6
#define PLC_NUM_AI 5

// Entradas digitais: todos com pull-up/pull-down interno disponível.
// Evitados: 0/1/3 (boot/serial), 6-11 (flash), 12/15 (strapping).
static const uint8_t PIN_DI[PLC_NUM_DI] = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22};

// Saídas digitais. GPIO2 = LED da placa (Q5), útil para testes.
static const uint8_t PIN_DO[PLC_NUM_DO] = {23, 25, 26, 27, 33, 2};

// Entradas analógicas: só ADC1 (o ADC2 não funciona com Wi-Fi ligado).
// 36 = VP, 39 = VN. Faixa útil ~0-3100 mV com atenuação de 11 dB.
static const uint8_t PIN_AI[PLC_NUM_AI] = {36, 39, 34, 35, 32};

// ---------------------------------------------------------------------------
// Memória do CLP
// ---------------------------------------------------------------------------
#define PLC_NUM_M 128   // bits internos M0..M127
#define PLC_NUM_D 32    // registradores inteiros D0..D31 (retentivos via web)
#define PLC_NUM_T 16    // temporizadores T0..T15
#define PLC_NUM_C 16    // contadores C0..C15
#define PLC_NUM_S 3     // bits de sistema: S0 1º scan, S1 pisca 1 Hz, S2 sempre ON

#define PLC_MAX_INSTR 2048   // instruções de bytecode por programa
#define PLC_MAX_EDGES 128    // contatos de borda (P/N) por programa
#define PLC_STACK_DEPTH 32   // profundidade máxima de aninhamento
#define PLC_MAX_PROGRAM_BYTES (32 * 1024)  // tamanho máximo do JSON do programa

#define PLC_SCAN_PERIOD_MS 10
#define PLC_AI_FILTER_SHIFT 3   // filtro exponencial: alfa = 1/8

#define PROGRAM_FILE "/program.json"

// Wi-Fi de fallback (modo AP) quando não há rede configurada ou ela falha.
#define AP_SSID "CLP-ESP32"
#define AP_PASS "clp12345"     // mínimo 8 caracteres
#define DEFAULT_HOSTNAME "clp"  // http://clp.local
