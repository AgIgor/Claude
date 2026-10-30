// Mapa de pinos e tempos do MODULO DE PORTA (ESP32 DevKit / WROOM-32).
// O mesmo firmware serve para as 4 portas: a posicao vem dos jumpers.
#pragma once
#include <stdint.h>

constexpr int8_t PIN_PAIR = 0;  // botao BOOT da placa
constexpr int8_t PIN_LED  = 2;

// --- Codificacao da posicao (jumper ao GND = 1) ---
//   CODE1 CODE0   posicao
//   aberto aberto  DE  (dianteira esquerda)
//   aberto GND     DD  (dianteira direita)
//   GND    aberto  TE  (traseira esquerda)
//   GND    GND     TD  (traseira direita)
constexpr int8_t PIN_CODE0 = 32;
constexpr int8_t PIN_CODE1 = 33;

// --- Pontes H: {A, B}. A=HIGH -> sentido "+", B=HIGH -> sentido "-" ---
constexpr int8_t PIN_WINDOW[2] = {25, 26};  // + sobe   / - desce
constexpr int8_t PIN_LOCK[2]   = {27, 14};  // + trava  / - destrava
// Somente portas dianteiras:
constexpr int8_t PIN_FOLD[2]   = {21, 22};  // + rebate / - abre
constexpr int8_t PIN_LAMP      = 23;        // seta (MOSFET/rele), ativo em HIGH

// --- Ajuste do espelho ---
// true : espelho de carro de 3 fios (comum + vertical + horizontal), 3 reles SPDT.
//        Em cada rele: NF = GND, NA = +12 V, comum = fio do espelho.
// false: cada motor com ponte H propria (PIN_MIR_V / PIN_MIR_H).
constexpr bool   MIRROR_COMMON_WIRE      = true;
constexpr int8_t PIN_MIR_3WIRE[3]        = {16, 17, 18};  // {comum, vertical, horizontal}
constexpr bool   MIRROR_RELAY_ACTIVE_LOW = true;          // modulos de rele comuns: LOW aciona
constexpr int8_t PIN_MIR_V[2]  = {16, 17};  // + cima   / - baixo     (so com MIRROR_COMMON_WIRE = false)
constexpr int8_t PIN_MIR_H[2]  = {18, 19};  // + direita/ - esquerda  (so com MIRROR_COMMON_WIRE = false)
// Inverta se o espelho andar ao contrario. Indice: 0 = DE, 1 = DD.
constexpr bool MIRROR_INVERT_V[2] = {false, false};
constexpr bool MIRROR_INVERT_H[2] = {false, true};  // espelhos sao montados espelhados

// --- Desembacador do espelho (rele) ---
constexpr int8_t PIN_HEATER        = 13;
constexpr bool   HEATER_ACTIVE_LOW = true;

// --- Sensores de corrente opcionais (-1 = desativado) ---
// Ex.: pino IS do BTS7960 com divisor, ou ACS712. Ajuste o limiar (0..4095) na bancada.
constexpr int8_t PIN_WINDOW_SENSE = -1;     // sugestao: 36 (VP)
constexpr int    WINDOW_STALL_ADC = 2500;
constexpr int8_t PIN_FOLD_SENSE   = -1;     // sugestao: 39 (VN)
constexpr int    FOLD_STALL_ADC   = 2500;

// --- Tempos ---
constexpr uint32_t WINDOW_MAX_MS  = 8000;   // tempo maximo continuo do motor do vidro
constexpr uint32_t MIRROR_MAX_MS  = 10000;
constexpr uint32_t LOCK_PULSE_MS  = 300;    // pulso do atuador de trava
constexpr uint32_t FOLD_MS        = 3000;   // tempo de rebatimento (ou ate travar por corrente)
constexpr uint32_t PAIR_WINDOW_MS = 60000;
constexpr uint32_t PAIR_HOLD_MS   = 3000;   // segurar PAIR 3 s  -> pareamento
constexpr uint32_t RESET_HOLD_MS  = 10000;  // segurar PAIR 10 s -> esquece a central
constexpr uint32_t LINK_LOST_MS   = 3500;   // sem mensagens da central -> LED pisca lento
