#pragma once

// ---------- Wi-Fi (o ESP32 cria a própria rede) ----------
#define WIFI_SSID      "FiestaDash"
#define WIFI_PASS      "fiesta2004"     // mínimo 8 caracteres
// Dashboard: http://192.168.4.1

// Transmissão contínua (WebSocket): a página recebe cada quadro CAN e cada valor no instante
// em que chegam, sem precisar perguntar.
#define WS_PORT        81
#define WS_FLUSH_MS    40    // junta as linhas e envia a cada 40 ms (até 25 pacotes/s)

// ---------- MCP2515 (SPI / VSPI) ----------
#define CAN_SCK_PIN    18
#define CAN_MISO_PIN   19   // SO do MCP2515: nunca ligue 5 V direto aqui (veja README)
#define CAN_MOSI_PIN   23
#define CAN_CS_PIN     5
// 1 MHz funciona com conversor de nível BSS138. Com o MCP2515 em 3,3 V pode subir para 8 MHz.
#define CAN_SPI_HZ     1000000

// Cristal do módulo (escrito no componente metálico: 8.000 ou 16.000). Também ajustável na web.
#define CAN_XTAL_MHZ   8

// Temporizações (ms)
#define CAN_LISTEN_MS  400   // tempo ouvindo cada velocidade na detecção automática
#define CAN_P2_MS      100   // espera pela resposta da ECU
#define CAN_MULTI_MS   60    // espera por respostas adicionais (outras ECUs / várias DTCs)
#define CAN_GAP_MS     5     // intervalo mínimo entre pedidos

// ID de requisição: 0x7DF = funcional (todas as ECUs), 0x7E0 = ECU do motor
#define CAN_DEFAULT_REQ 0x7DF

// Sinais do modo escuta usados quando nada foi salvo pela web (rede ECU <-> painel,
// identificados nas gravações de 25/10/2026). Formato: chave,ID,byte,tamanho,ordem,mult,soma
//   rpm: 201 bytes 0-1 / 4
//   spd: 201 bytes 4-5, (valor - 10000) / 100; parado = 27 10
//   tps: 201 byte 6, escala a confirmar (pedal no fundo deve dar ~100%)
//   ect: 420 byte 0 - 40
//   map: 420 byte 6 em kPa (motor parado = ~100, pressão atmosférica)
#define DEFAULT_SIGNALS \
  "rpm,201,0,2,B,0.25,0\n" \
  "spd,201,4,2,B,0.01,-100\n" \
  "tps,201,6,1,B,0.5,0\n" \
  "ect,420,0,1,B,1,-40\n" \
  "map,420,6,1,B,1,0\n"

// ---------- Botão BOOT e LED ----------
// Toque curto no BOOT: força nova conexão com a ECU (desliga o modo demonstração).
// Segurar 3 s: autoteste do MCP2515 em loopback (não transmite no carro).
#define BUTTON_PIN     0    // botão BOOT da placa (ativo em nível baixo)
#define BUTTON_LONG_MS 3000
#define LED_PIN        2    // LED azul da DevKit; -1 para desativar

// ---------- Tensão da bateria (opcional) ----------
// Divisor 100k (do +12V do pino 16 OBD) / 22k (para GND) -> GPIO34.
// Use -1 para desativar.
#define VBAT_PIN       -1//34
#define VBAT_RATIO     ((100.0f + 22.0f) / 22.0f)
