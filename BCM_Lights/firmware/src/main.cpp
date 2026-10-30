// BCM de iluminação: tarefa de controle (20 ms) + servidor web em Wi-Fi próprio.
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "app.h"
#include "bcm_logic.h"
#include "hw_io.h"
#include "web.h"

namespace app {
namespace {

constexpr uint32_t STEP_MS = 20;
constexpr double STEP_S = STEP_MS / 1000.0;
constexpr uint32_t SAVE_DELAY_MS = 2000;  // grava na flash 2 s após a última mudança

// Muda quando a estrutura da Config muda: evita carregar um blob antigo incompatível.
uint32_t configMagic() {
    return 0xBC100000u ^ (sizeof(bcm::Config) << 8) ^ (sizeof(IoSettings) << 4) ^ bcm::kConfigFieldCount;
}
// ---- estado do BCM (alterado somente na tarefa do BCM)
bcm::Config cfg;
bcm::BCM logic(&cfg);
WebInputs web_in;
IoSettings io;
int speed = 1;
bool paused = false;

// ---- compartilhado (protegido por mutex)
SemaphoreHandle_t mtx;
QueueHandle_t cmd_queue;
Snapshot snap;
bool ind_seen = false;
uint32_t config_version = 1;
uint32_t config_dirty_ms = 0;  // 0 = nada para gravar

constexpr size_t LOG_SIZE = 64;
LogEntry log_ring[LOG_SIZE];
uint32_t log_head = 0;  // número de sequência do próximo registro

Preferences prefs;

void onLog(double t, const char* msg, void*) {
    Serial.printf("[%8.2f] %s\n", t, msg);
    xSemaphoreTake(mtx, portMAX_DELAY);
    LogEntry& e = log_ring[log_head % LOG_SIZE];
    e.t = t;
    strlcpy(e.msg, msg, sizeof(e.msg));
    ++log_head;
    xSemaphoreGive(mtx);
}

void markConfigChanged() {
    xSemaphoreTake(mtx, portMAX_DELAY);
    ++config_version;
    config_dirty_ms = millis() | 1;
    xSemaphoreGive(mtx);
}

void loadSettings() {
    prefs.begin("bcm", false);
    if (prefs.getUInt("magic", 0) == configMagic() &&
        prefs.getBytesLength("cfg") == sizeof(cfg) && prefs.getBytesLength("io") == sizeof(io)) {
        prefs.getBytes("cfg", &cfg, sizeof(cfg));
        prefs.getBytes("io", &io, sizeof(io));
        Serial.println("Configuração carregada da flash");
    } else {
        Serial.println("Usando configuração padrão");
    }
}

void saveSettings() {
    bcm::Config c;
    IoSettings i;
    getConfig(c);
    xSemaphoreTake(mtx, portMAX_DELAY);
    i = io;
    xSemaphoreGive(mtx);
    prefs.putBytes("cfg", &c, sizeof(c));
    prefs.putBytes("io", &i, sizeof(i));
    prefs.putUInt("magic", configMagic());
    Serial.println("Configuração gravada na flash");
}

void applyCommand(const Command& c) {
    switch (c.type) {
        case CmdType::LOCK: logic.pressLock(); break;
        case CmdType::UNLOCK: logic.pressUnlock(); break;
        case CmdType::FLASH: logic.flashHighBeam(); break;
        case CmdType::RESET:
            logic.reset();
            onLog(0.0, "BCM reiniciado", nullptr);
            break;
        case CmdType::SET_IGN:
            web_in.ign = static_cast<bcm::Ignition>(constrain(static_cast<int>(c.value), 0, 2));
            break;
        case CmdType::SET_SWITCH:
            web_in.sw = static_cast<bcm::LightSwitch>(constrain(static_cast<int>(c.value), 0, 3));
            break;
        case CmdType::SET_LUX: web_in.lux = constrain(c.value, 0.0, 100000.0); break;
        case CmdType::SET_DOOR:
            if (c.index >= 0 && c.index < 4) web_in.doors[c.index] = c.value != 0;
            break;
        case CmdType::SET_BATT: web_in.batt = constrain(c.value, 0.0, 20.0); break;
        case CmdType::SET_CONFIG:
            if (const bcm::ConfigField* f = bcm::findConfigField(c.key)) {
                xSemaphoreTake(mtx, portMAX_DELAY);  // getConfig() lê sob o mesmo mutex
                bcm::setConfigValue(cfg, *f, c.value);
                xSemaphoreGive(mtx);
                markConfigChanged();
            }
            break;
        case CmdType::CONFIG_DEFAULTS:
            xSemaphoreTake(mtx, portMAX_DELAY);
            cfg = bcm::Config();
            io = IoSettings();
            xSemaphoreGive(mtx);
            markConfigChanged();
            break;
        case CmdType::SET_SPEED: speed = constrain(static_cast<int>(c.value), 1, 50); break;
        case CmdType::SET_PAUSED: paused = c.value != 0; break;
        case CmdType::SET_LUX_SRC:
            io.lux_from_ldr = c.value != 0;
            markConfigChanged();
            break;
        case CmdType::SET_PHYS_INPUTS:
            io.physical_inputs = c.value != 0;
            markConfigChanged();
            break;
    }
}

// Junta entradas da página com as dos GPIOs: portas em OU, ignição na maior posição.
bcm::Inputs mergeInputs(const PhysInputs& phys) {
    bcm::Inputs in;
    const bool use_phys = io.physical_inputs;
    in.ignition = web_in.ign;
    if (use_phys && static_cast<int>(phys.ign) > static_cast<int>(in.ignition)) in.ignition = phys.ign;
    in.light_switch = web_in.sw;
    in.ambient_lux = io.lux_from_ldr ? phys.lux : web_in.lux;
    for (int i = 0; i < 4; ++i) in.doors[i] = web_in.doors[i] || (use_phys && phys.doors[i]);
    in.battery_v = web_in.batt;
    return in;
}

void bcmTask(void*) {
    PhysInputs phys;
    hw::Buttons buttons;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STEP_MS));

        Command c;
        while (xQueueReceive(cmd_queue, &c, 0) == pdTRUE) applyCommand(c);

        hw::readInputs(STEP_MS, phys, buttons);
        if (io.physical_inputs) {
            if (buttons.lock) logic.pressLock();
            if (buttons.unlock) logic.pressUnlock();
            if (buttons.flash) logic.flashHighBeam();
        }
        const bcm::Inputs in = mergeInputs(phys);

        if (!paused) {
            for (int i = 0; i < speed; ++i) {
                logic.step(STEP_S, in);
                if (logic.out.indicators) ind_seen = true;
            }
        }
        hw::writeOutputs(logic.out);

        xSemaphoreTake(mtx, portMAX_DELAY);
        snap.t = logic.t;
        snap.mode = logic.mode;
        snap.mode_timer = logic.mode_timer;
        snap.locked = logic.locked;
        snap.dark = logic.dark;
        snap.relock_timer = logic.relock_timer;
        snap.interior_timer = logic.interior_timer;
        snap.saver = logic.saver_tripped || logic.interior_saver_tripped;
        snap.out = logic.out;
        snap.ind_seen = snap.ind_seen || ind_seen;
        snap.eff = in;
        snap.web = web_in;
        snap.phys = phys;
        snap.speed = speed;
        snap.paused = paused;
        snap.io = io;
        xSemaphoreGive(mtx);
        ind_seen = false;
    }
}

}  // namespace

// ---- API usada pelo servidor web
bool postCommand(const Command& c) { return xQueueSend(cmd_queue, &c, 0) == pdTRUE; }

void getSnapshot(Snapshot& s) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    s = snap;
    snap.ind_seen = false;
    xSemaphoreGive(mtx);
}

void getConfig(bcm::Config& c) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    c = cfg;
    xSemaphoreGive(mtx);
}

uint32_t configVersion() {
    xSemaphoreTake(mtx, portMAX_DELAY);
    const uint32_t v = config_version;
    xSemaphoreGive(mtx);
    return v;
}

size_t readLogs(uint32_t& cursor, LogEntry* out, size_t max) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (log_head - cursor > LOG_SIZE) cursor = log_head - LOG_SIZE;  // perdeu registros antigos
    size_t n = 0;
    while (cursor != log_head && n < max) out[n++] = log_ring[cursor++ % LOG_SIZE];
    xSemaphoreGive(mtx);
    return n;
}

uint32_t logHead() {
    xSemaphoreTake(mtx, portMAX_DELAY);
    const uint32_t h = log_head;
    xSemaphoreGive(mtx);
    return h;
}

// Chamado no loop(): grava a configuração com atraso para não gastar a flash.
static void saveIfDirty() {
    xSemaphoreTake(mtx, portMAX_DELAY);
    const uint32_t dirty = config_dirty_ms;
    const bool due = dirty && millis() - dirty >= SAVE_DELAY_MS;
    if (due) config_dirty_ms = 0;
    xSemaphoreGive(mtx);
    if (due) saveSettings();
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== BCM Lights ===");
    mtx = xSemaphoreCreateMutex();
    cmd_queue = xQueueCreate(32, sizeof(Command));

    loadSettings();
    hw::begin();
    logic.setLogger(onLog, nullptr);
    web::begin();

    xTaskCreatePinnedToCore(bcmTask, "bcm", 6144, nullptr, 3, nullptr, 1);
}

void loop() {
    web::loop();
    saveIfDirty();
    delay(10);
}

}  // namespace app

void setup() { app::setup(); }
void loop() { app::loop(); }
