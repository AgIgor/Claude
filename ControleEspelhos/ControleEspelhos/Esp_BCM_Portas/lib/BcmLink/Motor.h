// Motor DC reversivel acionado por ponte H de 2 entradas (BTS7960, DRV8871,
// L298N, modulo de 2 reles...). Dois modos:
//   - hold : so gira enquanto receber refresh (vidro, espelho). Para sozinho se
//            o refresh parar de chegar ou se atingir o tempo maximo.
//   - timed: pulso de duracao fixa (trava, rebatimento).
// Opcional: deteccao de travamento por corrente (pino ADC ligado ao IS do
// BTS7960, ACS712, shunt + amplificador...).
#pragma once
#include <Arduino.h>

class Motor {
 public:
  enum End : uint8_t { END_NONE, END_DONE, END_REFRESH_TIMEOUT, END_MAX_TIME, END_STALL };

  Motor(int8_t pinA, int8_t pinB, int8_t sensePin = -1, int stallThreshold = 0)
      : a_(pinA), b_(pinB), sense_(sensePin), stallThreshold_(stallThreshold) {}

  virtual void begin() {
    pinMode(a_, OUTPUT);
    pinMode(b_, OUTPUT);
    drive(0);
    if (sense_ >= 0) pinMode(sense_, INPUT);
  }

  // dir: +1 ou -1. Retorna false se esse sentido esta bloqueado: o motor ja
  // parou por fim de curso/tempo maximo e o botao continua pressionado.
  bool runHold(int8_t dir, uint32_t maxMs, uint32_t refreshTimeoutMs) {
    uint32_t now = millis();
    if (dir == blockedDir_) return false;
    blockedDir_ = 0;
    if (!running_ || dir != dir_ || !hold_) start(dir, now);
    hold_ = true;
    maxMs_ = maxMs;
    refreshTimeout_ = refreshTimeoutMs;
    lastRefresh_ = now;
    return true;
  }

  void runTimed(int8_t dir, uint32_t durationMs) {
    blockedDir_ = 0;
    start(dir, millis());
    hold_ = false;
    maxMs_ = durationMs;
  }

  // Parada comandada: libera tambem o bloqueio de sentido.
  void stop() {
    blockedDir_ = 0;
    halt();
  }

  // Chamar no loop(). Retorna != END_NONE quando o motor parou sozinho.
  End update() {
    if (!running_) return END_NONE;
    uint32_t now = millis();
    if (hold_ && now - lastRefresh_ > refreshTimeout_) { halt(); return END_REFRESH_TIMEOUT; }
    if (now - startMs_ >= maxMs_) {
      if (!hold_) { halt(); return END_DONE; }
      blockedDir_ = dir_;
      halt();
      return END_MAX_TIME;
    }
    if (sense_ >= 0 && now - startMs_ > kInrushBlankMs && now - lastSample_ >= 10) {
      lastSample_ = now;
      stallCount_ = analogRead(sense_) > stallThreshold_ ? stallCount_ + 1 : 0;
      if (stallCount_ >= 3) {
        if (hold_) blockedDir_ = dir_;
        halt();
        return END_STALL;
      }
    }
    return END_NONE;
  }

  bool running() const { return running_; }
  int8_t dir() const { return running_ ? dir_ : 0; }

 protected:
  // Liga as saidas para o sentido pedido (0 = parado). Subclasses mudam a ligacao.
  virtual void drive(int8_t dir) {
    digitalWrite(a_, dir > 0 ? HIGH : LOW);
    digitalWrite(b_, dir < 0 ? HIGH : LOW);
  }

 private:
  static constexpr uint32_t kInrushBlankMs = 250;  // ignora pico de partida

  void halt() {
    running_ = false;
    drive(0);
  }

  void start(int8_t dir, uint32_t now) {
    dir_ = dir;
    running_ = true;
    startMs_ = now;
    stallCount_ = 0;
    drive(dir);
  }

  int8_t a_, b_, sense_;
  int stallThreshold_;
  bool running_ = false, hold_ = false;
  int8_t dir_ = 0, blockedDir_ = 0;
  uint32_t startMs_ = 0, lastRefresh_ = 0, lastSample_ = 0;
  uint32_t maxMs_ = 0, refreshTimeout_ = 0;
  uint8_t stallCount_ = 0;
};
