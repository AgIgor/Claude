#pragma once
#include "common_config.h"

// Recepção: fio verde -> 1 kOhm + zener 3V3 -> GPIO3 (RX da UART0). Com 5 kOhm, não recebe.
// Transmissão: GPIO1 (TX da UART0) -> gate do MOSFET N; dreno por 220 Ohm a 1 kOhm no fio branco,
// fonte no GND. As mensagens de boot da ROM também saem por aí e chegam ao ar; ele as descarta pelo
// checksum.
// O GPIO1 e o GPIO2 precisam estar altos ao ligar: sem pull-down no gate.
#define AC_TX_PIN       1
#define AC_RX_PIN       3
#define AC_TX_INVERTED  true  // o MOSFET inverte: GPIO1 alto puxa a linha para baixo
// Em paralelo com o módulo original, ele cuida do heartbeat e das confirmações, inclusive da
// verificação do ar ao energizar. Sem o módulo, use false: o ar pode não aceitar comandos depois
// de uma queda de energia.
#define AC_PARALLEL     true
#define AC_PIN_TEST_MS      60000  // /api/txtest: duração, sem transmitir para o ar nesse tempo
#define AC_PIN_TEST_STEP_MS 3000
#define AC_FRAME_GAP_MS 20   // silêncio maior que isto descarta um quadro incompleto
#define AC_RX_BUFFER    512
#define EEPROM_BYTES    512
#define SCHEDULE_EEPROM_OFFSET 128  // o Wi-Fi fica antes disto
#define HTTP_PAGE_TIMEOUT_MS 20000  // retransmissões somam segundos; a biblioteca desiste em 1 s
