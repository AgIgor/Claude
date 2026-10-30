#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ---------- Hardware (mesma pinagem do v5) ----------
#define PIN_LEDS   0     // GPIO0 -> DIN da fita (bit-bang)
#define PIN_LEDS_DMA 3   // GPIO3/RX -> DIN da fita (DMA, imune a glitches)
#define PIN_SDA    1     // GPIO1 (TX) -> SDA  (Serial fica indisponível)
#define PIN_SCL    2     // GPIO2 -> SCL
#define MATRIX_W   17    // matriz 17 x 5
#define MATRIX_H   5
#define NUM_LEDS   (MATRIX_W * MATRIX_H)

// Modos
enum ColorMode  : uint8_t { COLOR_FIXED = 0, COLOR_RAINBOW = 1, COLOR_RAINBOW_DIGITS = 2 };
enum Transition : uint8_t { TRANS_NONE = 0, TRANS_FADE = 1, TRANS_WIPE = 2 };
enum NightMode  : uint8_t { NIGHT_OFF = 0, NIGHT_LUX = 1, NIGHT_SCHEDULE = 2 };

struct Settings {
  uint32_t magic;

  // Sistema
  char hostname[32];
  char adminPass[33];

  // Wi-Fi
  char wifiSsid[33];
  char wifiPass[65];

  // Hora
  char tz[64];          // string POSIX, ex: "<-03>3"
  char ntp[64];
  bool h12;
  bool leadingZero;
  bool blinkColon;

  // Telas (segundos, 0 = desativada)
  uint16_t tClock, tTemp, tHumi;
  uint8_t  transition;
  bool     tempDecimal;   // usa o LED do ":" como ponto decimal
  bool     fahrenheit;

  // Cor
  uint8_t  colorMode;
  uint32_t cClock, cTemp, cHumi;
  uint8_t  rainbowSpeed;  // 1..10

  // Brilho
  bool     autoBright;
  uint8_t  bMin, bMax, bManual;
  uint16_t luxLow, luxHigh;

  // Modo noturno
  uint8_t  nightMode;
  uint16_t nightLux;
  uint8_t  nightStart, nightEnd;   // horas
  uint32_t nightColor;
  uint8_t  nightBright;
  bool     nightClockOnly;

  // Sensores
  int16_t  tempOffset10;   // décimos de grau
  int16_t  humiOffset10;   // décimos de %
  uint16_t sensorInterval; // segundos

  // MQTT
  bool     mqttEnabled;
  char     mqttHost[64];
  uint16_t mqttPort;
  char     mqttUser[33];
  char     mqttPass[65];
  char     mqttTopic[48];
  uint16_t mqttInterval;

  uint32_t crc;
};

extern Settings S;

// Flags retornadas por Cfg::fromJson indicando o que mudou
enum : uint16_t {
  CHG_ANY  = 1 << 0,
  CHG_TIME = 1 << 1,
  CHG_WIFI = 1 << 2,
  CHG_MQTT = 1 << 3,
  CHG_AUTH = 1 << 4,
  CHG_HOST = 1 << 5,
};

// Orientação da fita na matriz. Padrão: olhando de frente, começa embaixo à
// direita, de baixo para cima, em zigue-zague.
struct Layout {
  uint32_t magic;
  bool     startLeft;   // primeiro LED à esquerda
  bool     startTop;    // primeiro LED em cima
  bool     zigzag;      // linhas alternam o sentido
  bool     columns;     // fita percorre colunas em vez de linhas
  bool     dma;         // dados no GPIO3/RX via DMA em vez do GPIO0 (exige reiniciar)
  uint32_t crc;
};

extern Layout L;

namespace Cfg {
  void     load();
  void     saveLayout();
  void     defaultLayout();
  void     save();
  void     defaults();
  void     toJson(JsonObject o);
  uint16_t fromJson(JsonObjectConst o);
}
