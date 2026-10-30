// Estado compartilhado entre a tarefa do BCM (20 ms) e o servidor web.
//
// Regra de concorrência: só a tarefa do BCM altera a lógica, a Config e as
// entradas virtuais. O servidor web envia comandos por fila (postCommand) e lê
// cópias do estado (getSnapshot / getConfig), protegidas por mutex.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bcm_logic.h"

namespace app {

struct IoSettings {
    bool lux_from_ldr = false;    // false: luminosidade vem do slider da página
    bool physical_inputs = true;  // lê portas, ignição e botões dos GPIOs
};

// Entradas "virtuais" controladas pela página web.
struct WebInputs {
    bcm::Ignition ign = bcm::Ignition::OFF;
    bcm::LightSwitch sw = bcm::LightSwitch::SW_AUTO;
    double lux = 1000.0;
    bool doors[4] = {false, false, false, false};
    double batt = 12.6;
};

// Leitura dos GPIOs (já com debounce).
struct PhysInputs {
    bcm::Ignition ign = bcm::Ignition::OFF;
    bool doors[4] = {false, false, false, false};
    double lux = 0.0;
};

struct Snapshot {
    double t;
    bcm::Mode mode;
    double mode_timer;
    bool locked;
    bool dark;
    double relock_timer;
    double interior_timer;
    bool saver;
    bcm::Outputs out;
    bool ind_seen;  // pisca acendeu desde o último snapshot (não perde piscadas curtas)
    bcm::Inputs eff;
    WebInputs web;
    PhysInputs phys;
    int speed;
    bool paused;
    IoSettings io;
};

enum class CmdType : uint8_t {
    LOCK, UNLOCK, FLASH, RESET,
    SET_IGN, SET_SWITCH, SET_LUX, SET_DOOR, SET_BATT,
    SET_CONFIG, CONFIG_DEFAULTS,
    SET_SPEED, SET_PAUSED,
    SET_LUX_SRC, SET_PHYS_INPUTS,
};

struct Command {
    CmdType type;
    int8_t index = 0;
    double value = 0;
    char key[40] = {0};
};

struct LogEntry {
    double t;
    char msg[96];
};

bool postCommand(const Command& c);
void getSnapshot(Snapshot& s);
void getConfig(bcm::Config& c);
// Incrementa a cada mudança de Config/IoSettings (a web reenvia a configuração).
uint32_t configVersion();
// Log em buffer circular: `cursor` é o próximo número de sequência a ler.
size_t readLogs(uint32_t& cursor, LogEntry* out, size_t max);
uint32_t logHead();

}  // namespace app
