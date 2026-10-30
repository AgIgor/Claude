#pragma once

// ---------- Wi-Fi (o ESP32 cria a própria rede) ----------
#define WIFI_SSID      "FiestaDash"
#define WIFI_PASS      "fiesta2004"     // mínimo 8 caracteres
// Dashboard: http://192.168.4.1

// ---------- K-line (UART2) ----------
// TX: GPIO17 -> Q1 -> Q2 (coletor na K-line), circuito prj.perquin.com: duas inversões = não invertido
// RX: K-line -> 10k -> GPIO16, com 10k + zener 3V3 para o GND (obrigatório no ESP32)
#define KLINE_RX_PIN   16
#define KLINE_TX_PIN   17
#define KLINE_TX_SIG   U2TXD_OUT_IDX   // sinal TX da UART2 na matriz de GPIO
#define KLINE_TX_INVERT 0   // 0 = 2 transistores/MOSFETs em cascata ou L9637D/MC33290
                            // 1 = um MOSFET só (GPIO alto puxa a K-line para baixo)
#define KLINE_BAUD     10400

// Temporizações ISO 9141-2 / ISO 14230 (ms)
#define KL_P1_GAP_MS   22    // silêncio que encerra uma resposta da ECU
#define KL_P2_MAX_MS   150   // espera pelo 1º byte da resposta
#define KL_P3_MIN_MS   55    // intervalo mínimo entre requisições
#define KL_P4_MS       5     // intervalo entre bytes enviados
// Valores da biblioteca OBD9141 (painelKline), que funciona em carros reais:
#define KL_IDLE_BEFORE_INIT_MS 3000  // linha parada antes de qualquer init
#define KL_POST_INIT_MS        50    // pausa depois da init antes do 1º pedido

// Endereços padrão OBD
#define KL_DEFAULT_TARGET  0x33   // 0x33 = endereçamento funcional OBD
#define KL_TESTER_ADDR     0xF1

// ---------- Botão BOOT e LED ----------
// Toque curto no BOOT: força nova conexão com a ECU (desliga o modo demonstração).
// Segurar 3 s: teste do circuito, K-line alterna 1 s em 0 V / 1 s em 12 V por 20 s.
#define BUTTON_PIN     0    // botão BOOT da placa (ativo em nível baixo)
#define BUTTON_LONG_MS 3000
#define LED_PIN        2    // LED azul da DevKit; -1 para desativar

// ---------- Tensão da bateria (opcional) ----------
// Divisor 100k (do +12V do pino 16 OBD) / 22k (para GND) -> GPIO34.
// Use -1 para desativar.
#define VBAT_PIN       -1//34
#define VBAT_RATIO     ((100.0f + 22.0f) / 22.0f)
