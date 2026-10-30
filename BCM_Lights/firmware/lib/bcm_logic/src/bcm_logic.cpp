#include "bcm_logic.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace bcm {

// ------------------------------------------------------------------ nomes
const char* modeName(Mode m) {
    switch (m) {
        case Mode::LEAVING_HOME: return "LEAVING_HOME";
        case Mode::COMING_HOME: return "COMING_HOME";
        case Mode::NORMAL: return "NORMAL";
        default: return "OFF";
    }
}

static const char* const kSwitchNames[] = {"OFF", "PARK", "LOW", "AUTO"};

const char* switchName(LightSwitch s) { return kSwitchNames[static_cast<int>(s) & 3]; }

bool parseSwitch(const char* name, LightSwitch* out) {
    for (int i = 0; i < 4; ++i) {
        if (strcmp(name, kSwitchNames[i]) == 0) {
            *out = static_cast<LightSwitch>(i);
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ config
#define NUM_F(name, lo, hi) {#name, ConfigField::NUM, offsetof(Config, name), lo, hi}
#define BOOL_F(name) {#name, ConfigField::BOOL, offsetof(Config, name), 0, 1}
#define INT_F(name, lo, hi) {#name, ConfigField::INT, offsetof(Config, name), lo, hi}

const ConfigField kConfigFields[] = {
    NUM_F(dark_threshold_lux, 1, 5000),
    NUM_F(light_threshold_lux, 1, 5000),
    NUM_F(dark_debounce_s, 0, 30),
    BOOL_F(coming_home_auto),
    NUM_F(coming_home_time_s, 0, 300),
    NUM_F(coming_home_flash_window_s, 0, 60),
    BOOL_F(coming_home_start_on_door_close),
    BOOL_F(coming_home_restart_on_lock),
    BOOL_F(leaving_home_enabled),
    NUM_F(leaving_home_time_s, 0, 300),
    NUM_F(auto_relock_time_s, 5, 300),
    INT_F(lock_flashes, 0, 5),
    INT_F(unlock_flashes, 0, 5),
    NUM_F(indicator_period_s, 0.2, 2),
    NUM_F(interior_delay_s, 0, 120),
    NUM_F(interior_unlock_time_s, 0, 120),
    NUM_F(interior_fade_in_s, 0, 10),
    NUM_F(interior_fade_s, 0, 10),
    NUM_F(exterior_fade_in_s, 0, 10),
    NUM_F(exterior_fade_out_s, 0, 10),
    NUM_F(battery_saver_time_s, 10, 3600),
    NUM_F(low_battery_v, 10, 13),
};
const size_t kConfigFieldCount = sizeof(kConfigFields) / sizeof(kConfigFields[0]);

#undef NUM_F
#undef BOOL_F
#undef INT_F

const ConfigField* findConfigField(const char* key) {
    for (size_t i = 0; i < kConfigFieldCount; ++i) {
        if (strcmp(kConfigFields[i].key, key) == 0) return &kConfigFields[i];
    }
    return nullptr;
}

double getConfigValue(const Config& cfg, const ConfigField& f) {
    const char* p = reinterpret_cast<const char*>(&cfg) + f.offset;
    switch (f.kind) {
        case ConfigField::BOOL: return *reinterpret_cast<const bool*>(p) ? 1.0 : 0.0;
        case ConfigField::INT: return *reinterpret_cast<const int*>(p);
        default: return *reinterpret_cast<const double*>(p);
    }
}

void setConfigValue(Config& cfg, const ConfigField& f, double value) {
    if (value < f.min) value = f.min;
    if (value > f.max) value = f.max;
    char* p = reinterpret_cast<char*>(&cfg) + f.offset;
    switch (f.kind) {
        case ConfigField::BOOL: *reinterpret_cast<bool*>(p) = value != 0.0; break;
        case ConfigField::INT: *reinterpret_cast<int*>(p) = static_cast<int>(lround(value)); break;
        default: *reinterpret_cast<double*>(p) = value; break;
    }
}

// ------------------------------------------------------------------ BCM
static double ramp(double current, double target, double dt, double rise_s, double fall_s) {
    if (target > current) {
        double v = current + (rise_s > 0 ? dt / rise_s : 1.0);
        return v < target ? v : target;
    }
    if (target < current) {
        double v = current - (fall_s > 0 ? dt / fall_s : 1.0);
        return v > target ? v : target;
    }
    return current;
}

BCM::BCM(const Config* cfg) : cfg_(cfg) { reset(); }

void BCM::reset() {
    t = 0.0;
    mode = Mode::OFF;
    mode_timer = 0.0;
    locked = true;
    dark = false;
    dark_counter_ = 0.0;
    initialized_ = false;

    prev_ign_on_ = false;
    prev_door_open_ = false;
    prev_switch_ = -1;
    has_t_ign_off_ = false;
    t_ign_off_ = 0.0;

    relock_timer = 0.0;
    interior_timer = 0.0;
    door_open_time_ = 0.0;
    interior_saver_tripped = false;
    saver_timer_ = 0.0;
    saver_tripped = false;

    flash_elapsed_ = 0.0;
    flash_end_ = 0.0;

    out = Outputs();
    n_events_ = 0;
}

void BCM::pushEvent(Event e) {
    if (n_events_ < kMaxEvents) events_[n_events_++] = e;
}

const Outputs& BCM::step(double dt, const Inputs& in) {
    const Config& cfg = *cfg_;
    t += dt;
    const bool ign_on = in.ignition == Ignition::ON;
    const bool door_open = in.doors[0] || in.doors[1] || in.doors[2] || in.doors[3];
    const bool battery_ok = in.battery_v >= cfg.low_battery_v;

    updateDarkness(dt, in.ambient_lux);

    const bool ign_on_edge = ign_on && !prev_ign_on_;
    const bool ign_off_edge = prev_ign_on_ && !ign_on;
    const bool door_close_edge = prev_door_open_ && !door_open;
    const int sw = static_cast<int>(in.light_switch);
    const bool switch_changed = sw != prev_switch_;

    // --- ignição
    if (ign_on_edge) {
        setMode(Mode::NORMAL, "Ignição ligada");
        relock_timer = 0.0;
        interior_timer = 0.0;
    }
    if (ign_off_edge) {
        has_t_ign_off_ = true;
        t_ign_off_ = t;
        if (cfg.coming_home_auto && in.light_switch == LightSwitch::SW_AUTO && dark && battery_ok) {
            startComingHome("ignição desligada no escuro (AUTO)");
        } else {
            setMode(Mode::OFF, "Ignição desligada");
        }
    }

    // --- eventos da chave / alavanca
    for (int i = 0; i < n_events_; ++i) handleEvent(events_[i], ign_on, door_open, battery_ok);
    n_events_ = 0;

    // --- temporizadores de cortesia
    if (mode == Mode::COMING_HOME || mode == Mode::LEAVING_HOME) {
        const bool coming = mode == Mode::COMING_HOME;
        const double full = coming ? cfg.coming_home_time_s : cfg.leaving_home_time_s;
        const bool hold = door_open && (coming ? cfg.coming_home_start_on_door_close : true);
        if (hold) {
            mode_timer = full;
        } else {
            mode_timer -= dt;
            if (mode_timer <= 0) {
                mode_timer = 0.0;
                setMode(Mode::OFF, coming ? "Coming Home encerrado (tempo esgotado)"
                                          : "Leaving Home encerrado (tempo esgotado)");
            }
        }
    }

    // --- re-travamento automático
    if (relock_timer > 0) {
        if (ign_on || door_open) {
            relock_timer = 0.0;
            if (door_open) emit("Re-travamento automático cancelado (porta aberta)");
        } else {
            relock_timer -= dt;
            if (relock_timer <= 0) {
                relock_timer = 0.0;
                locked = true;
                interior_timer = 0.0;
                startFlashes(cfg.lock_flashes);
                emit("Re-travamento automático (nenhuma porta foi aberta)");
                if (mode == Mode::LEAVING_HOME) {
                    setMode(Mode::OFF, "Leaving Home cancelado pelo re-travamento");
                }
            }
        }
    }

    // --- luz interna
    if (door_close_edge && !ign_on) {
        if (cfg.interior_delay_s > interior_timer) interior_timer = cfg.interior_delay_s;
    }
    if (interior_timer > 0) {
        interior_timer -= dt;
        if (interior_timer < 0.0) interior_timer = 0.0;
    }
    if (door_open && !ign_on) {
        door_open_time_ += dt;
        if (door_open_time_ >= cfg.battery_saver_time_s && !interior_saver_tripped) {
            interior_saver_tripped = true;
            emit("Proteção de bateria: luz interna desligada (porta aberta)");
        }
    } else {
        door_open_time_ = 0.0;
        interior_saver_tripped = false;
    }
    const bool door_light = door_open && !interior_saver_tripped;
    const double interior_target = (door_light || (!ign_on && interior_timer > 0)) ? 1.0 : 0.0;

    // --- faróis e lanternas
    double low = 0.0, park = 0.0, puddle = 0.0;
    if (ign_on) {
        saver_timer_ = 0.0;
        saver_tripped = false;
        if (in.light_switch == LightSwitch::SW_PARK) {
            park = 1.0;
        } else if (in.light_switch == LightSwitch::SW_LOW ||
                   (in.light_switch == LightSwitch::SW_AUTO && dark)) {
            park = low = 1.0;
        }
    } else {
        if (switch_changed) {
            saver_timer_ = 0.0;
            saver_tripped = false;
        }
        if ((in.light_switch == LightSwitch::SW_PARK || in.light_switch == LightSwitch::SW_LOW) &&
            !saver_tripped) {
            saver_timer_ += dt;
            if (saver_timer_ >= cfg.battery_saver_time_s) {
                saver_tripped = true;
                emit("Proteção de bateria: luzes externas desligadas");
            } else {
                park = 1.0;
                low = in.light_switch == LightSwitch::SW_LOW ? 1.0 : 0.0;
            }
        }
    }

    if (mode == Mode::COMING_HOME || mode == Mode::LEAVING_HOME) low = park = puddle = 1.0;
    if (!ign_on && door_open && dark) puddle = 1.0;

    // --- rampas (fade via PWM)
    out.low_beam = ramp(out.low_beam, low, dt, cfg.exterior_fade_in_s, cfg.exterior_fade_out_s);
    out.park_lamps = ramp(out.park_lamps, park, dt, cfg.exterior_fade_in_s, cfg.exterior_fade_out_s);
    out.puddle = ramp(out.puddle, puddle, dt, cfg.exterior_fade_in_s, cfg.exterior_fade_out_s);
    out.interior = ramp(out.interior, interior_target, dt, cfg.interior_fade_in_s, cfg.interior_fade_s);

    // --- piscas de confirmação
    if (flash_elapsed_ < flash_end_) {
        const double period = cfg.indicator_period_s;
        out.indicators = fmod(flash_elapsed_, period) < period / 2;
        flash_elapsed_ += dt;
    } else {
        out.indicators = false;
    }

    prev_ign_on_ = ign_on;
    prev_door_open_ = door_open;
    prev_switch_ = sw;
    return out;
}

void BCM::handleEvent(Event e, bool ign_on, bool door_open, bool battery_ok) {
    const Config& cfg = *cfg_;
    if (e == EV_UNLOCK) {
        locked = false;
        startFlashes(cfg.unlock_flashes);
        emit("Destravado");
        if (!ign_on) {
            relock_timer = cfg.auto_relock_time_s;
            interior_timer = cfg.interior_unlock_time_s;
            if (cfg.leaving_home_enabled && dark && battery_ok && mode != Mode::COMING_HOME) {
                mode_timer = cfg.leaving_home_time_s;
                setMode(Mode::LEAVING_HOME, "Leaving Home ativado (destravado no escuro)");
            }
        }
    } else if (e == EV_LOCK) {
        if (door_open) {
            emit("Travamento recusado: porta aberta");
            return;
        }
        locked = true;
        relock_timer = 0.0;
        interior_timer = 0.0;
        startFlashes(cfg.lock_flashes);
        emit("Travado");
        if (mode == Mode::LEAVING_HOME) {
            setMode(Mode::OFF, "Leaving Home cancelado pelo travamento");
        } else if (mode == Mode::COMING_HOME && cfg.coming_home_restart_on_lock) {
            mode_timer = cfg.coming_home_time_s;
            emit("Coming Home reiniciado pelo travamento");
        }
    } else if (e == EV_FLASH) {
        if (ign_on) {
            emit("Lampejo do farol alto");
        } else if (has_t_ign_off_ && battery_ok && t - t_ign_off_ <= cfg.coming_home_flash_window_s) {
            startComingHome("lampejo do farol alto");
        } else {
            emit("Lampejo ignorado (fora da janela após desligar a ignição)");
        }
    }
}

void BCM::updateDarkness(double dt, double lux) {
    const Config& cfg = *cfg_;
    if (!initialized_) {
        dark = lux < cfg.dark_threshold_lux;
        initialized_ = true;
        return;
    }
    const bool crossing = dark ? lux > cfg.light_threshold_lux : lux < cfg.dark_threshold_lux;
    if (crossing) {
        dark_counter_ += dt;
        if (dark_counter_ >= cfg.dark_debounce_s) {
            dark = !dark;
            dark_counter_ = 0.0;
            emit(dark ? "Sensor de luz: ESCURO" : "Sensor de luz: CLARO");
        }
    } else {
        dark_counter_ = 0.0;
    }
}

void BCM::startComingHome(const char* reason) {
    mode_timer = cfg_->coming_home_time_s;
    if (mode == Mode::COMING_HOME) {
        emit("Coming Home reiniciado (%s)", reason);
    } else {
        mode = Mode::COMING_HOME;
        emit("Coming Home ativado (%s)", reason);
    }
}

void BCM::startFlashes(int n) {
    flash_elapsed_ = 0.0;
    flash_end_ = n * cfg_->indicator_period_s;
}

void BCM::setMode(Mode m, const char* reason) {
    if (m != mode) {
        mode = m;
        emit("%s", reason);
    }
}

void BCM::emit(const char* fmt, ...) {
    if (!log_fn_) return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_fn_(t, buf, log_ctx_);
}

}  // namespace bcm
