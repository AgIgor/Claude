// Botao/entrada digital com debounce, bordas e tempo pressionado.
#pragma once
#include <Arduino.h>

class Button {
 public:
  // GPIOs 34..39 sao somente entrada e NAO tem pull-up interno: use pullup=false
  // e um resistor de 10k externo para 3V3.
  explicit Button(int8_t pin, bool pullup = true, bool activeLow = true)
      : pin_(pin), pullup_(pullup), activeLow_(activeLow) {}

  void begin() {
    if (pin_ < 0) return;
    pinMode(pin_, pullup_ ? INPUT_PULLUP : INPUT);
    stable_ = raw();
  }

  void update() {
    pressedEdge_ = releasedEdge_ = false;
    if (pin_ < 0) return;
    bool r = raw();
    uint32_t now = millis();
    if (r != last_) { last_ = r; changedAt_ = now; }
    if (r != stable_ && now - changedAt_ >= kDebounceMs) {
      stable_ = r;
      if (stable_) { pressedAt_ = now; pressedEdge_ = true; }
      else { releasedEdge_ = true; heldOnRelease_ = now - pressedAt_; }
    }
  }

  bool isPressed() const { return stable_; }
  bool pressed() const { return pressedEdge_; }
  bool released() const { return releasedEdge_; }
  uint32_t heldMs() const { return stable_ ? millis() - pressedAt_ : 0; }
  uint32_t heldOnReleaseMs() const { return heldOnRelease_; }

 private:
  static constexpr uint32_t kDebounceMs = 30;
  bool raw() const { return (digitalRead(pin_) == LOW) == activeLow_; }

  int8_t pin_;
  bool pullup_, activeLow_;
  bool stable_ = false, last_ = false;
  bool pressedEdge_ = false, releasedEdge_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0, heldOnRelease_ = 0;
};
