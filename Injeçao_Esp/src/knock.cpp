// Driver do TPIC8101 (processador de sinal do sensor de detonacao).
// Mesmo barramento SPI do MCP3208, CS proprio. Cristal de 8 MHz no TPIC.
#include "knock.h"
#include <SPI.h>
#include "pins.h"

// Frequencias centrais do filtro passa-banda (kHz), indice 0..63 (datasheet TPIC8101)
static const float BAND_KHZ[64] = {
    1.22, 1.26, 1.31, 1.35, 1.40, 1.45, 1.51, 1.57, 1.63, 1.71, 1.78, 1.87, 1.96,
    2.07, 2.18, 2.31, 2.46, 2.54, 2.62, 2.71, 2.81, 2.92, 3.03, 3.15, 3.28, 3.43,
    3.59, 3.76, 3.95, 4.16, 4.39, 4.66, 4.95, 5.12, 5.29, 5.48, 5.68, 5.90, 6.12,
    6.37, 6.64, 6.94, 7.27, 7.63, 8.02, 8.46, 8.95, 9.50, 10.12, 10.46, 10.83, 11.22,
    11.65, 12.10, 12.60, 13.14, 13.72, 14.36, 15.07, 15.84, 16.71, 17.67, 18.76, 19.98};

#define CMD_PRESCALER_8MHZ 0x46  // 0x40 | (0b0011 << 1), SO ativo
#define CMD_CHANNEL(c) (0xE0 | ((c) & 1))
#define CMD_BAND(i) (0x00 | ((i) & 0x3F))
#define CMD_GAIN(i) (0x80 | ((i) & 0x3F))
#define CMD_INTEG(i) (0xC0 | ((i) & 0x1F))
#define CMD_ADVANCED 0x71  // resultado digital de 10 bits pela SPI

static uint8_t xfer(uint8_t cmd) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
  digitalWrite(PIN_KNOCK_CS, LOW);
  uint8_t r = SPI.transfer(cmd);
  digitalWrite(PIN_KNOCK_CS, HIGH);
  SPI.endTransaction();
  return r;
}

static uint8_t bandIdx = 0, gainIdx = 0;

void knockInit(float freqKhz, uint8_t gain, uint8_t integ) {
  pinMode(PIN_KNOCK_CS, OUTPUT);
  digitalWrite(PIN_KNOCK_CS, HIGH);
  bandIdx = 0;
  for (int i = 1; i < 64; i++)
    if (fabsf(BAND_KHZ[i] - freqKhz) < fabsf(BAND_KHZ[bandIdx] - freqKhz)) bandIdx = i;
  gainIdx = gain;
  xfer(CMD_PRESCALER_8MHZ);
  xfer(CMD_CHANNEL(0));
  xfer(CMD_BAND(bandIdx));
  xfer(CMD_GAIN(gain));
  xfer(CMD_INTEG(integ));
  xfer(CMD_ADVANCED);
}

// Le o valor integrado da ultima janela (0..1023). Chamar depois que INT/HOLD
// desceu. No modo avancado o resultado sai nas duas transferencias seguintes
// (8 bits baixos, depois os 2 altos); os comandos enviados so reconfirmam ajustes.
uint16_t knockRead() {
  uint8_t lo = xfer(CMD_CHANNEL(0));
  uint8_t hi = xfer(CMD_GAIN(gainIdx));
  return lo | ((hi & 0x03) << 8);
}
