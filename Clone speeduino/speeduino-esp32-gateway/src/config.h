#pragma once

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
// O ESP32 sempre cria a própria rede (modo AP), para funcionar dentro do carro.
#define AP_SSID       "Speeduino-Dash"
#define AP_PASSWORD   "speeduino"      // mínimo de 8 caracteres
#define AP_CHANNEL    6

// Opcional: também conecta numa rede existente (casa/oficina).
// Deixe STA_SSID vazio para desativar.
#define STA_SSID      ""
#define STA_PASSWORD  ""

#define HOSTNAME      "speeduino"      // http://speeduino.local

// ---------------------------------------------------------------------------
// Serial com a ECU (serial secundária do Speeduino)
//   Mega 2560: Serial3 -> TX3 = pino 14, RX3 = pino 15
//   STM32/Teensy: Serial2
// ---------------------------------------------------------------------------
#define ECU_BAUD      115200           // fixo no firmware (init.cpp)
#define ECU_RX_PIN    16               // ligar no TX da ECU (com divisor 5V -> 3,3V no Mega)
#define ECU_TX_PIN    17               // ligar no RX da ECU

// $tsCanId usado no comando 'r' (0 = ECU principal)
#define ECU_CAN_ID    0

// Intervalo entre pedidos de dados. 130 bytes a 115200 baud levam ~11 ms,
// então 40 ms (25 Hz) deixa folga para a ECU.
#define POLL_INTERVAL_MS     40
#define RESPONSE_TIMEOUT_MS  150
#define OFFLINE_AFTER_TIMEOUTS 3
