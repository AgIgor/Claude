// Modos salvos e programação por horário. O relógio vem da internet: sem ele, nenhuma regra dispara.
#include "ac_schedule.h"
#include <time.h>
#include "ac_link.h"
#include "common_config.h"
#ifdef ESP8266
#include <EEPROM.h>
#include "config_esp01.h"
#else
#include <Preferences.h>
#endif

static const uint32_t STORE_MAGIC = 0xAC5C4001;
static const time_t CLOCK_VALID_AFTER = 1600000000;
static const uint32_t TICK_MS = 1000;

struct Store {
  uint32_t magic;
  AcPreset presets[AC_PRESET_COUNT];
  AcRule rules[AC_RULE_COUNT];
};

static Store store;
static uint32_t lastTick = 0;
static int32_t lastMinute = -1;
static char lastRun[48] = "";

static uint32_t storeMagic() { return STORE_MAGIC ^ sizeof(Store); }

static void loadDefaults() {
  memset(&store, 0, sizeof(store));
  store.magic = storeMagic();
  AcPreset& p = store.presets[0];
  acCopyName(p.name, "Dormir");
  p.mode = 1;
  p.temp = 22;
  p.fan = 1;
  p.vane = 1;
  p.display = 1;
  for (AcRule& r : store.rules) r.action = AC_RULE_OFF;
}

#ifdef ESP8266
static void loadStore() { EEPROM.get(SCHEDULE_EEPROM_OFFSET, store); }

static void saveStore() {
  EEPROM.put(SCHEDULE_EEPROM_OFFSET, store);
  EEPROM.commit();
}
#else
static Preferences prefs;

static void loadStore() {
  prefs.begin("acschedule", false);
  if (prefs.getBytes("data", &store, sizeof(store)) != sizeof(store)) store.magic = 0;
}

static void saveStore() { prefs.putBytes("data", &store, sizeof(store)); }
#endif

// Silencioso vem primeiro para o ar não apitar a cada comando. Ventilar não usa temperatura.
static bool applyPreset(const AcPreset& p) {
  bool ok = AcLink::mute(p.mute) && AcLink::power(true) && AcLink::mode(p.mode) && AcLink::fan(p.fan);
  if (p.mode != 3) ok = ok && AcLink::setpoint(p.temp);
  return ok && AcLink::direction(p.swing, p.vane) && AcLink::sleep(p.sleep) && AcLink::display(p.display);
}

// Devolve false só se a fila estiver cheia: uma regra de modo apagado não tem o que fazer.
static bool runRule(const AcRule& r, const tm& t) {
  const char* label = "Desligar";
  if (r.action == AC_RULE_OFF) {
    if (!AcLink::power(false)) return false;
  } else {
    const AcPreset& p = store.presets[r.action];
    if (!p.name[0]) return true;
    if (!applyPreset(p)) return false;
    label = p.name;
  }
  snprintf(lastRun, sizeof(lastRun), "%02d:%02d %s", t.tm_hour, t.tm_min, label);
  return true;
}

static bool saveRule(AcSchedule::ArgFn arg) {
  const int i = arg("i").toInt();
  if (i < 0 || i >= AC_RULE_COUNT) return false;
  const AcRule r = {(uint8_t)arg("days").toInt(), (uint8_t)arg("on").toInt(),
                    (uint8_t)arg("h").toInt(), (uint8_t)arg("m").toInt(),
                    (int8_t)arg("act").toInt()};
  if (!acRuleValid(r)) return false;
  store.rules[i] = r;
  return true;
}

static bool savePreset(AcSchedule::ArgFn arg) {
  const int i = arg("i").toInt();
  if (i < 0 || i >= AC_PRESET_COUNT) return false;
  AcPreset p;
  memset(&p, 0, sizeof(p));
  const String name = arg("n");
  if (name.length()) {
    acCopyName(p.name, name.c_str());
    p.mode = arg("mode").toInt();
    p.temp = arg("temp").toInt();
    p.fan = arg("fan").toInt();
    p.swing = arg("swing").toInt();
    p.vane = arg("vane").toInt();
    p.sleep = arg("sleep").toInt();
    p.display = arg("disp").toInt();
    p.mute = arg("mute").toInt();
    if (!acPresetValid(p, AC_TEMP_MIN, AC_TEMP_MAX)) return false;
  }
  store.presets[i] = p;
  return true;
}

static void appendEscaped(String& o, const char* s) {
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') o += '\\';
    if ((uint8_t)*s >= 0x20) o += *s;
  }
}

namespace AcSchedule {

void begin() {
  loadStore();
  if (store.magic != storeMagic()) loadDefaults();
#ifdef ESP8266
  configTime(AC_TIMEZONE, AC_NTP_SERVER);
#else
  configTzTime(AC_TIMEZONE, AC_NTP_SERVER);
#endif
}

void loop() {
  const uint32_t ms = millis();
  if (ms - lastTick < TICK_MS) return;
  lastTick = ms;
  const time_t now = time(nullptr);
  if (now < CLOCK_VALID_AFTER) return;
  tm t;
  localtime_r(&now, &t);
  const int32_t minute = (int32_t)(now / 60);
  if (minute == lastMinute) return;
  const int due = acRuleDue(store.rules, AC_RULE_COUNT, t.tm_wday, t.tm_hour, t.tm_min);
  if (due < 0 || runRule(store.rules[due], t)) lastMinute = minute;
}

bool apply(int preset) {
  if (preset < 0 || preset >= AC_PRESET_COUNT || !store.presets[preset].name[0]) return false;
  return applyPreset(store.presets[preset]);
}

bool update(ArgFn arg) {
  const String kind = arg("kind");
  const bool ok = kind == "preset" ? savePreset(arg) : kind == "rule" && saveRule(arg);
  if (ok) saveStore();
  return ok;
}

String json() {
  String o;
  o.reserve(1600);
  const time_t now = time(nullptr);
  o = "{\"sync\":";
  if (now >= CLOCK_VALID_AFTER) {
    tm t;
    localtime_r(&now, &t);
    char clock[32];
    snprintf(clock, sizeof(clock), "true,\"day\":%d,\"h\":%d,\"m\":%d", t.tm_wday, t.tm_hour, t.tm_min);
    o += clock;
  } else {
    o += "false";
  }
  o += ",\"last\":\"";
  appendEscaped(o, lastRun);
  o += "\",\"presets\":[";
  for (int i = 0; i < AC_PRESET_COUNT; i++) {
    const AcPreset& p = store.presets[i];
    char b[88];
    if (i) o += ',';
    o += "{\"n\":\"";
    appendEscaped(o, p.name);
    snprintf(b, sizeof(b),
             "\",\"mode\":%u,\"temp\":%u,\"fan\":%u,\"swing\":%u,\"vane\":%u,\"sleep\":%u,\"disp\":%u,\"mute\":%u}",
             p.mode, p.temp, p.fan, p.swing, p.vane, p.sleep, p.display, p.mute);
    o += b;
  }
  o += "],\"rules\":[";
  for (int i = 0; i < AC_RULE_COUNT; i++) {
    const AcRule& r = store.rules[i];
    char b[56];
    snprintf(b, sizeof(b), "%s{\"days\":%u,\"on\":%u,\"h\":%u,\"m\":%u,\"act\":%d}", i ? "," : "",
             r.days, r.enabled, r.hour, r.minute, r.action);
    o += b;
  }
  o += "]}";
  return o;
}

}  // namespace AcSchedule
