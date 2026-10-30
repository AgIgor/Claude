// Espelho de 3 fios: os motores vertical e horizontal dividem um fio comum.
// Cada fio vai num rele SPDT (ou meia-ponte): desligado = GND, ligado = +12 V.
// O motor parado recebe o mesmo potencial do comum para nao girar junto.
// Herda de Motor o modo "segurar", o tempo maximo e o bloqueio de sentido.
#pragma once
#include "Motor.h"

class MirrorCommon : public Motor {
 public:
  enum Axis : uint8_t { AXIS_V, AXIS_H };

  MirrorCommon(int8_t pinCom, int8_t pinV, int8_t pinH, bool activeLow)
      : Motor(-1, -1), com_(pinCom), v_(pinV), h_(pinH), activeLow_(activeLow) {}

  void begin() override {
    for (int8_t p : {com_, v_, h_}) {
      write(p, false);  // nivel de "desligado" antes de virar saida (rele ativo em LOW)
      pinMode(p, OUTPUT);
      write(p, false);
    }
  }

  // Para o eixo atual antes de acionar o outro.
  bool runAxis(Axis axis, int8_t dir, uint32_t maxMs, uint32_t refreshTimeoutMs) {
    if (axis != axis_) {
      stop();
      axis_ = axis;
    }
    return runHold(dir, maxMs, refreshTimeoutMs);
  }

 protected:
  void drive(int8_t dir) override {
    bool on = dir != 0, pos = dir > 0;
    int8_t motor = axis_ == AXIS_V ? v_ : h_, other = axis_ == AXIS_V ? h_ : v_;
    write(motor, on && pos);   // motor em +12 V e comum em GND, ou o inverso
    write(com_, on && !pos);
    write(other, on && !pos);
  }

 private:
  void write(int8_t pin, bool on) { digitalWrite(pin, on != activeLow_ ? HIGH : LOW); }

  int8_t com_, v_, h_;
  bool activeLow_;
  Axis axis_ = AXIS_V;
};
