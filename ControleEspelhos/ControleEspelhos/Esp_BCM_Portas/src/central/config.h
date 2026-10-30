// Mapa de pinos e opcoes da CENTRAL (ESP32 DevKit / WROOM-32).
// Todas as entradas sao ativas em nivel BAIXO: o botao liga o pino ao GND.
#pragma once
#include <stdint.h>

// --- Portas instaladas neste carro, na ordem DE, DD, TE, TD ---
// Exemplos:  4 portas         -> {true,  true,  true, true}
//            so traseiras     -> {false, false, true, true}
// Os botoes de portas nao instaladas nao sao lidos (os pinos ficam livres) e a
// central recusa pareamento dessas posicoes. Sem nenhuma dianteira, os botoes de
// seta, pisca-alerta, espelho e rebatimento tambem sao desativados.
constexpr bool DOOR_INSTALLED[4] = {true, true, true, true};
constexpr bool FRONT_INSTALLED = DOOR_INSTALLED[0] || DOOR_INSTALLED[1];

// --- Pareamento / status ---
constexpr int8_t PIN_PAIR = 0;   // botao BOOT da placa
constexpr int8_t PIN_LED  = 2;   // LED azul da placa

// --- Vidros: {subir, descer} por porta, na ordem DE, DD, TE, TD ---
constexpr int8_t PIN_WIN[4][2] = {
    {13, 14},  // dianteira esquerda
    {16, 17},  // dianteira direita
    {18, 19},  // traseira esquerda
    {21, 22},  // traseira direita
};

// --- Travas ---
constexpr int8_t PIN_LOCK   = 23;
constexpr int8_t PIN_UNLOCK = 25;

// --- Setas ---
// TURN_L / TURN_R: chave de alavanca (nivel: enquanto fechada, pisca).
// HAZARD: botao de pulso, alterna o pisca-alerta.
constexpr int8_t PIN_TURN_L = 26;
constexpr int8_t PIN_TURN_R = 27;
constexpr int8_t PIN_HAZARD = 32;
constexpr int8_t PIN_TURN_INDICATOR = 5;  // saida: LED/buzzer do "painel" acompanha o pisca

// --- Espelhos: botao resistivo VW Tiguan 2015 ---
// Cada saida do botao e um divisor: 3V3 -> pull-up -> pino -> botao -> GND (+100 nF ao GND).
// Use so pinos do ADC1 (32..39): o ADC2 nao funciona com o Wi-Fi/ESP-NOW ligado.
// NUNCA ligue o pull-up em 5 V: os pinos do ESP32 sao 3,3 V.
constexpr int8_t PIN_MIR_JOY = 34;  // joystick: neutro, cima, baixo, esquerda, direita
constexpr int8_t PIN_MIR_SEL = 35;  // seletor: espelho esq., centro, espelho dir., rebater
constexpr int8_t PIN_MIR_CAL = 4;   // jumper ao GND ao ligar = calibrar o botao
constexpr int8_t PIN_HEAT    = 33;  // desembacador: o botao fecha ao GND nessa posicao

// --- Comportamento ---
constexpr bool     AUTO_FOLD_ON_LOCK = true;   // rebate ao travar, abre ao destravar (se o
                                               // seletor nao estiver em REBATER)
constexpr uint32_t HEAT_MAX_MS       = 600000; // desembacador desliga apos 10 min (0 = sem limite)
constexpr uint32_t BLINK_HALF_MS     = 350;    // ~85 piscadas/min
constexpr uint32_t PAIR_WINDOW_MS    = 60000;  // duracao do modo pareamento
constexpr uint32_t PAIR_HOLD_MS      = 3000;   // segurar PAIR 3 s  -> pareamento
constexpr uint32_t RESET_HOLD_MS     = 10000;  // segurar PAIR 10 s -> apaga todos os pareamentos
constexpr uint32_t PING_INTERVAL_MS  = 1000;
constexpr uint32_t OFFLINE_AFTER_MS  = 3500;
constexpr uint32_t RETRY_TIMEOUT_MS  = 80;
constexpr uint8_t  MAX_TRIES         = 4;
