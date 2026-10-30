// Lógica do BCM de iluminação (Coming Home / Leaving Home).
//
// Porte direto de bcm/logic.py: C++11 puro, sem dependência de Arduino, para
// rodar igual no ESP32 e no PC (host_test). Usa double para ter os mesmos
// resultados da referência em Python (verificado por tools/check_parity.py).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bcm {

enum class Ignition : uint8_t { OFF = 0, ACC = 1, ON = 2 };
// Prefixo SW_ evita conflito com as macros LOW/HIGH do Arduino.
enum class LightSwitch : uint8_t { SW_OFF = 0, SW_PARK = 1, SW_LOW = 2, SW_AUTO = 3 };
enum class Mode : uint8_t { OFF = 0, LEAVING_HOME = 1, COMING_HOME = 2, NORMAL = 3 };

const char* modeName(Mode m);
const char* switchName(LightSwitch s);
bool parseSwitch(const char* name, LightSwitch* out);

struct Config {
    // Sensor de luz
    double dark_threshold_lux = 50.0;
    double light_threshold_lux = 150.0;
    double dark_debounce_s = 2.0;
    // Coming Home
    bool coming_home_auto = true;
    double coming_home_time_s = 60.0;
    double coming_home_flash_window_s = 10.0;
    bool coming_home_start_on_door_close = true;
    bool coming_home_restart_on_lock = true;
    // Leaving Home
    bool leaving_home_enabled = true;
    double leaving_home_time_s = 30.0;
    // Travas
    double auto_relock_time_s = 45.0;
    int lock_flashes = 1;
    int unlock_flashes = 2;
    double indicator_period_s = 0.6;
    // Luz interna
    double interior_delay_s = 15.0;
    double interior_unlock_time_s = 30.0;
    double interior_fade_in_s = 0.5;
    double interior_fade_s = 2.0;
    // Faróis / lanternas
    double exterior_fade_in_s = 0.3;
    double exterior_fade_out_s = 1.5;
    // Proteção de bateria
    double battery_saver_time_s = 600.0;
    double low_battery_v = 11.8;
};

// Tabela de campos da Config: usada para JSON, NVS e testes (acesso por nome).
struct ConfigField {
    enum Kind : uint8_t { NUM, BOOL, INT };
    const char* key;
    Kind kind;
    size_t offset;
    double min;
    double max;
};
extern const ConfigField kConfigFields[];
extern const size_t kConfigFieldCount;
const ConfigField* findConfigField(const char* key);
double getConfigValue(const Config& cfg, const ConfigField& f);
// Aplica com limite (clamp) ao intervalo do campo.
void setConfigValue(Config& cfg, const ConfigField& f, double value);

struct Inputs {
    Ignition ignition = Ignition::OFF;
    LightSwitch light_switch = LightSwitch::SW_AUTO;
    double ambient_lux = 1000.0;
    bool doors[4] = {false, false, false, false};
    double battery_v = 12.6;
};

// Níveis 0..1 (duty cycle do PWM).
struct Outputs {
    double low_beam = 0.0;
    double park_lamps = 0.0;
    double puddle = 0.0;
    double interior = 0.0;
    bool indicators = false;
};

class BCM {
public:
    typedef void (*LogFn)(double t, const char* msg, void* ctx);

    explicit BCM(const Config* cfg);
    void reset();
    void setLogger(LogFn fn, void* ctx) { log_fn_ = fn; log_ctx_ = ctx; }

    void pressLock() { pushEvent(EV_LOCK); }
    void pressUnlock() { pushEvent(EV_UNLOCK); }
    void flashHighBeam() { pushEvent(EV_FLASH); }

    const Outputs& step(double dt, const Inputs& in);

    // Estado observável (somente leitura para quem usa)
    double t;
    Mode mode;
    double mode_timer;
    bool locked;
    bool dark;
    double relock_timer;
    double interior_timer;
    bool saver_tripped;
    bool interior_saver_tripped;
    Outputs out;

private:
    enum Event : uint8_t { EV_LOCK, EV_UNLOCK, EV_FLASH };
    static const int kMaxEvents = 8;

    void pushEvent(Event e);
    void handleEvent(Event e, bool ign_on, bool door_open, bool battery_ok);
    void updateDarkness(double dt, double lux);
    void startComingHome(const char* reason);
    void startFlashes(int n);
    void setMode(Mode m, const char* reason);
    void emit(const char* fmt, ...);

    const Config* cfg_;
    LogFn log_fn_ = nullptr;
    void* log_ctx_ = nullptr;

    Event events_[kMaxEvents];
    int n_events_ = 0;

    double dark_counter_;
    bool initialized_;
    bool prev_ign_on_;
    bool prev_door_open_;
    int prev_switch_;  // -1 = nenhum ainda
    bool has_t_ign_off_;
    double t_ign_off_;
    double door_open_time_;
    double saver_timer_;
    double flash_elapsed_;
    double flash_end_;
};

}  // namespace bcm
