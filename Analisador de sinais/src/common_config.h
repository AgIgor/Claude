#pragma once
// Parâmetros iguais nas duas placas.

#define FW_VERSION     "3.10"

// ---------- Wi-Fi: roteador, com rede própria de reserva ----------
#define WIFI_SSID      "ACRemote"
#define WIFI_PASS      "analisador"     // mínimo 8 caracteres
// Na rede própria: http://192.168.4.1
#define HOSTNAME       "acremote"       // http://acremote.local no roteador
#define OTA_PASS       "analisador"     // senha para gravar firmware pela rede
#define WIFI_RETRY_MS  60000            // tentar sem parar derruba a rede própria
#define WIFI_AP_OPEN_MS  30000          // sem roteador por este tempo, abre a rede própria
#define WIFI_AP_CLOSE_MS 30000          // com roteador por este tempo, fecha a rede própria

// ---------- Relógio da programação ----------
#define AC_TIMEZONE    "<-03>3"         // Brasília, sem horário de verão
#define AC_NTP_SERVER  "pool.ntp.org"

// ---------- Protocolo do ar ----------
#define AC_BAUD        9600
#define AC_HEARTBEAT_MS   10000
#define AC_ACK_TIMEOUT_MS 300
#define AC_COMMAND_TRIES  3
#define AC_COMMAND_GAP_MS 200
#define AC_QUEUE_LEN      12    // um modo salvo envia até 9 comandos
#define AC_TEMP_MIN       16    // mínimo suposto; o máximo de 30 foi visto no app
#define AC_TEMP_MAX       30
#define AC_TIMER_MAX_H    12    // suposto; só 1 h foi capturada
// Copiado do módulo original (byte a byte, inclusive MAC e IP dele): a função dos primeiros bytes
// é desconhecida, então nada aqui foi alterado.
#define AC_HEARTBEAT_VALUE {0x01, 0x02, 0x01, 0x02, 0x13, 0x01, \
                            0x44, 0x3E, 0x07, 0x56, 0x48, 0x02, \
                            0x04, 0xC0, 0xA8, 0x0F, 0x4A}
