#include "hw_io.h"

#include <Arduino.h>
#include <math.h>

#include "pins.h"

namespace hw {
namespace {

constexpr int PWM_FREQ_HZ = 5000;
constexpr int PWM_BITS = 12;
constexpr int PWM_MAX = (1 << PWM_BITS) - 1;
constexpr float PWM_GAMMA = 2.2f;  // brilho percebido ~ linear durante o fade
constexpr uint32_t DEBOUNCE_MS = 40;

enum PwmChannel { CH_LOW_BEAM, CH_PARK, CH_PUDDLE, CH_INTERIOR };
const int kPwmPins[] = {PIN_LOW_BEAM, PIN_PARK, PIN_PUDDLE, PIN_INTERIOR};

// Entrada digital com debounce por tempo de estabilidade.
struct Debounced {
    int pin;
    bool state;     // estável (true = ativo, pino em GND)
    bool raw;
    uint32_t stable_ms;

    // Retorna true na borda de ativação.
    bool update(uint32_t period_ms) {
        const bool now = digitalRead(pin) == LOW;
        if (now != raw) {
            raw = now;
            stable_ms = 0;
        } else if (stable_ms < DEBOUNCE_MS) {
            stable_ms += period_ms;
            if (stable_ms >= DEBOUNCE_MS && raw != state) {
                state = raw;
                return state;
            }
        }
        return false;
    }
};

Debounced doors[4] = {{PIN_DOORS[0]}, {PIN_DOORS[1]}, {PIN_DOORS[2]}, {PIN_DOORS[3]}};
Debounced ign_on{PIN_IGN_ON}, ign_acc{PIN_IGN_ACC};
Debounced btn_lock{PIN_BTN_LOCK}, btn_unlock{PIN_BTN_UNLOCK}, btn_flash{PIN_BTN_FLASH};

float ldr_mv_filtered = -1.0f;

double ldrToLux(float mv) {
    if (mv < 5.0f) return 0.1;
    if (mv > 3290.0f) return 100000.0;
    const float r_ldr = LDR_FIXED_OHMS * (3300.0f / mv - 1.0f);
    const double lux = 10.0 * pow(LDR_R10_OHMS / r_ldr, 1.0 / LDR_GAMMA);
    return lux < 0.1 ? 0.1 : (lux > 100000.0 ? 100000.0 : lux);
}

uint32_t duty(double level) {
    if (level <= 0.0) return 0;
    if (level >= 1.0) return PWM_MAX;
    return static_cast<uint32_t>(powf(static_cast<float>(level), PWM_GAMMA) * PWM_MAX + 0.5f);
}

}  // namespace

void begin() {
    for (int ch = 0; ch < 4; ++ch) {
        ledcSetup(ch, PWM_FREQ_HZ, PWM_BITS);
        ledcAttachPin(kPwmPins[ch], ch);
        ledcWrite(ch, 0);
    }
    pinMode(PIN_IND_LEFT, OUTPUT);
    pinMode(PIN_IND_RIGHT, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_IND_LEFT, LOW);
    digitalWrite(PIN_IND_RIGHT, LOW);

    for (Debounced* d : {&doors[0], &doors[1], &doors[2], &doors[3], &ign_on, &ign_acc,
                         &btn_lock, &btn_unlock, &btn_flash}) {
        pinMode(d->pin, INPUT_PULLUP);
    }
    delay(5);
    // Estado inicial = leitura atual, sem gerar borda (botão preso no boot não dispara evento).
    for (Debounced* d : {&doors[0], &doors[1], &doors[2], &doors[3], &ign_on, &ign_acc,
                         &btn_lock, &btn_unlock, &btn_flash}) {
        d->raw = d->state = digitalRead(d->pin) == LOW;
        d->stable_ms = DEBOUNCE_MS;
    }

    analogSetPinAttenuation(PIN_LDR, ADC_11db);
}

void readInputs(uint32_t period_ms, app::PhysInputs& phys, Buttons& buttons) {
    for (int i = 0; i < 4; ++i) {
        doors[i].update(period_ms);
        phys.doors[i] = doors[i].state;
    }
    ign_on.update(period_ms);
    ign_acc.update(period_ms);
    phys.ign = ign_on.state ? bcm::Ignition::ON : (ign_acc.state ? bcm::Ignition::ACC : bcm::Ignition::OFF);

    buttons.lock = btn_lock.update(period_ms);
    buttons.unlock = btn_unlock.update(period_ms);
    buttons.flash = btn_flash.update(period_ms);

    const float mv = static_cast<float>(analogReadMilliVolts(PIN_LDR));
    ldr_mv_filtered = ldr_mv_filtered < 0 ? mv : ldr_mv_filtered + 0.1f * (mv - ldr_mv_filtered);
    phys.lux = ldrToLux(ldr_mv_filtered);
}

void writeOutputs(const bcm::Outputs& out) {
    ledcWrite(CH_LOW_BEAM, duty(out.low_beam));
    ledcWrite(CH_PARK, duty(out.park_lamps));
    ledcWrite(CH_PUDDLE, duty(out.puddle));
    ledcWrite(CH_INTERIOR, duty(out.interior));
    digitalWrite(PIN_IND_LEFT, out.indicators ? HIGH : LOW);
    digitalWrite(PIN_IND_RIGHT, out.indicators ? HIGH : LOW);
}

void setStatusLed(bool on) { digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW); }

}  // namespace hw
