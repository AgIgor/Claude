// Mapa de pinos da bancada (ESP32 DevKit comum).
//
// Evitados de propósito: GPIO 0, 5, 12, 15 (strapping de boot), 6-11 (flash),
// ADC2 (não funciona com Wi-Fi ligado).
#pragma once

// ---- Saídas: LED + resistor (220-330 Ω) até o GND
constexpr int PIN_LOW_BEAM = 25;   // farol baixo   (LED branco)
constexpr int PIN_PARK = 26;       // lanternas     (LED vermelho)
constexpr int PIN_PUDDLE = 27;     // luz de chão   (LED azul/branco)
constexpr int PIN_INTERIOR = 14;   // luz interna   (LED amarelo)
constexpr int PIN_IND_LEFT = 32;   // pisca esq.    (LED âmbar)
constexpr int PIN_IND_RIGHT = 33;  // pisca dir.    (LED âmbar)
constexpr int PIN_STATUS_LED = 2;  // LED azul da própria placa

// ---- Entradas: chave/botão entre o pino e o GND (pull-up interno, ativo em nível baixo).
// Pinos sem nada ligado ficam em nível alto = inativos.
constexpr int PIN_DOORS[4] = {16, 17, 18, 19};  // motorista, passageiro, tras. esq., tras. dir.
constexpr int PIN_IGN_ON = 21;      // chave de ignição: posição ON
constexpr int PIN_IGN_ACC = 22;     // chave de ignição: posição ACC
constexpr int PIN_BTN_LOCK = 23;    // botão travar
constexpr int PIN_BTN_UNLOCK = 13;  // botão destravar
constexpr int PIN_BTN_FLASH = 4;    // lampejo do farol alto

// ---- Sensor de luz (ADC1): 3V3 -- LDR -- GPIO34 -- 10 kΩ -- GND
constexpr int PIN_LDR = 34;
constexpr float LDR_FIXED_OHMS = 10000.0f;
constexpr float LDR_R10_OHMS = 15000.0f;  // resistência do LDR a 10 lux (GL5528 ≈ 10-20 kΩ)
constexpr float LDR_GAMMA = 0.7f;         // inclinação da curva do LDR (datasheet)
