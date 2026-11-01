#pragma once

#include "common_config.h"

#define WS_PORT        81
#define WS_FLUSH_MS    40    // junta os quadros e envia a cada 40 ms
#define WS_BATCH_BYTES 6000  // tamanho máximo de cada envio
// Um navegador que sumiu sem fechar a conexão trava o laço principal: cada envio espera por ele.
#define WS_PING_MS     5000
#define WS_PONG_MS     2000
#define WS_PING_MISSES 2

// ---------- Pinos de escuta ----------
// 34 e 35 só funcionam como entrada: o firmware não tem como forçar a linha do ar.
#define PIN_CH_A       34    // fio verde
#define PIN_CH_B       35    // fio branco
#define CH_COUNT       2

// ---------- Gerador de teste (bancada) ----------
// Ligue 25 -> 34 e 26 -> 35 com o ar DESCONECTADO. Fora do teste os pinos ficam em alta impedância.
#define PIN_TEST_A     25
#define PIN_TEST_B     26

// ---------- Controle do ar (substitui o módulo original) ----------
// Só ligue este fio com o módulo original desconectado.
#define PIN_AC_TX      27    // GPIO27 -> 1 kOhm -> fio branco
#define AC_RX_LINE     0     // linha A (fio verde): transmissão do ar
#define AC_FORMAT      1     // 8E1 em UART_FORMATS
#define AC_GAP_MS      3     // o módulo confirma em ~3 ms; 3 ms de silêncio já fecham o quadro

// ---------- Captura ----------
#define EDGE_RING_SIZE   2048   // bordas entre a interrupção e a tarefa (potência de 2)
#define BURST_MAX_EDGES  2048   // bordas por rajada
#define SAMPLE_MAX_EDGES 4000   // bordas guardadas para a análise automática
#define SCOPE_MAX_PULSES 600    // pulsos enviados para a tela de sinal
#define FRAME_MAX_BYTES  512    // bytes por quadro
#define FRAME_RING_BYTES (24 * 1024)  // quadros aguardando um navegador conectado
#define DEFAULT_GAP_MS   10     // silêncio que separa dois quadros
#define ANALYZE_MS       1000   // intervalo da análise automática
#define OVERFLOW_HOLD_MS 100    // pausa após ruído demais na linha
#define PULSE_BUCKETS    12     // histograma: <5 us, <10 us, <20 us... dobrando a cada faixa

// Critérios para travar baud e formato sozinho
#define LOCK_MIN_BYTES   16
#define LOCK_MIN_SCORE   0.98f

#define LED_PIN        2     // LED azul da DevKit; -1 para desativar
#define LED_HOLD_MS    40
