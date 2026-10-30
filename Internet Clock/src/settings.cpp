#include "settings.h"
#include <EEPROM.h>

Settings S;
Layout   L;

// A orientação fica numa região separada para não apagar as configurações
static const int LAYOUT_ADDR = 1024;
static_assert(sizeof(Settings) <= LAYOUT_ADDR, "Settings grande demais");
static const uint32_t LAYOUT_MAGIC = 0x4D545836;  // muda quando a struct Layout muda

static const uint32_t MAGIC = 0x434C4B06;  // "CLK" v6

static uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

static uint32_t calcCrc() {
  return crc32((const uint8_t*)&S, offsetof(Settings, crc));
}

void Cfg::defaults() {
  memset(&S, 0, sizeof(S));
  S.magic = MAGIC;

  strlcpy(S.hostname, "relogio", sizeof(S.hostname));
#ifdef DEFAULT_WIFI_SSID
  strlcpy(S.wifiSsid, DEFAULT_WIFI_SSID, sizeof(S.wifiSsid));
#endif
#ifdef DEFAULT_WIFI_PASS
  strlcpy(S.wifiPass, DEFAULT_WIFI_PASS, sizeof(S.wifiPass));
#endif

  strlcpy(S.tz, "<-03>3", sizeof(S.tz));
  strlcpy(S.ntp, "pool.ntp.org", sizeof(S.ntp));
  S.h12 = false;
  S.leadingZero = true;
  S.blinkColon = true;

  S.tClock = 20;
  S.tTemp = 5;
  S.tHumi = 5;
  S.transition = TRANS_FADE;
  S.tempDecimal = false;
  S.fahrenheit = false;

  S.colorMode = COLOR_RAINBOW;
  S.cClock = 0x00A0FF;
  S.cTemp = 0xFF6000;
  S.cHumi = 0x00FF80;
  S.rainbowSpeed = 3;

  S.autoBright = true;
  S.bMin = 2;
  S.bMax = 150;
  S.bManual = 80;
  S.luxLow = 2;
  S.luxHigh = 400;

  S.nightMode = NIGHT_LUX;
  S.nightLux = 1;
  S.nightStart = 23;
  S.nightEnd = 6;
  S.nightColor = 0xFF0000;
  S.nightBright = 2;
  S.nightClockOnly = false;

  S.tempOffset10 = 0;
  S.humiOffset10 = 0;
  S.sensorInterval = 10;

  S.mqttEnabled = false;
  S.mqttPort = 1883;
  strlcpy(S.mqttTopic, "relogio", sizeof(S.mqttTopic));
  S.mqttInterval = 60;
}

static uint32_t layoutCrc() {
  return crc32((const uint8_t*)&L, offsetof(Layout, crc));
}

void Cfg::defaultLayout() {
  memset(&L, 0, sizeof(L));
  L.magic = LAYOUT_MAGIC;  // embaixo à direita, linhas em zigue-zague, GPIO0
  L.zigzag = true;
}

void Cfg::saveLayout() {
  L.crc = layoutCrc();
  EEPROM.put(LAYOUT_ADDR, L);
  EEPROM.commit();
}

void Cfg::load() {
  EEPROM.begin(LAYOUT_ADDR + sizeof(Layout));
  EEPROM.get(LAYOUT_ADDR, L);
  if (L.magic != LAYOUT_MAGIC || L.crc != layoutCrc()) {
    defaultLayout();
    saveLayout();
  }
  EEPROM.get(0, S);
  if (S.magic != MAGIC || S.crc != calcCrc()) {
    defaults();
    save();
  }
  // garante strings terminadas
  S.hostname[sizeof(S.hostname) - 1] = 0;
  S.tz[sizeof(S.tz) - 1] = 0;
}

void Cfg::save() {
  S.crc = calcCrc();
  EEPROM.put(0, S);
  EEPROM.commit();
}

// ---------- JSON ----------

static void colorToHex(uint32_t c, char* out) {
  snprintf(out, 8, "#%06X", (unsigned)(c & 0xFFFFFF));
}

void Cfg::toJson(JsonObject o) {
  char hex[8];
  o["hostname"] = S.hostname;
  o["adminSet"] = S.adminPass[0] != 0;
  o["ssid"] = S.wifiSsid;

  o["tz"] = S.tz;
  o["ntp"] = S.ntp;
  o["h12"] = S.h12;
  o["lz"] = S.leadingZero;
  o["blink"] = S.blinkColon;

  o["tClock"] = S.tClock;
  o["tTemp"] = S.tTemp;
  o["tHumi"] = S.tHumi;
  o["trans"] = S.transition;
  o["tDec"] = S.tempDecimal;
  o["tF"] = S.fahrenheit;

  o["cMode"] = S.colorMode;
  colorToHex(S.cClock, hex); o["cClock"] = hex;
  colorToHex(S.cTemp, hex);  o["cTemp"] = hex;
  colorToHex(S.cHumi, hex);  o["cHumi"] = hex;
  o["rbSpd"] = S.rainbowSpeed;

  o["bAuto"] = S.autoBright;
  o["bMin"] = S.bMin;
  o["bMax"] = S.bMax;
  o["bMan"] = S.bManual;
  o["luxLo"] = S.luxLow;
  o["luxHi"] = S.luxHigh;

  o["nMode"] = S.nightMode;
  o["nLux"] = S.nightLux;
  o["nStart"] = S.nightStart;
  o["nEnd"] = S.nightEnd;
  colorToHex(S.nightColor, hex); o["nColor"] = hex;
  o["nBright"] = S.nightBright;
  o["nClock"] = S.nightClockOnly;

  o["tOff"] = S.tempOffset10 / 10.0;
  o["hOff"] = S.humiOffset10 / 10.0;
  o["sInt"] = S.sensorInterval;

  o["mqEn"] = S.mqttEnabled;
  o["mqHost"] = S.mqttHost;
  o["mqPort"] = S.mqttPort;
  o["mqUser"] = S.mqttUser;
  o["mqPassSet"] = S.mqttPass[0] != 0;
  o["mqTopic"] = S.mqttTopic;
  o["mqInt"] = S.mqttInterval;
}

template <typename T>
static bool setNum(JsonObjectConst o, const char* key, T& dst, long lo, long hi) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return false;
  long n = constrain(v.as<long>(), lo, hi);
  if ((T)n == dst) return false;
  dst = (T)n;
  return true;
}

static bool setTenths(JsonObjectConst o, const char* key, int16_t& dst, int lo, int hi) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return false;
  float f = v.as<float>() * 10.0f;
  long n = constrain(lroundf(f), (long)lo, (long)hi);
  if (n == dst) return false;
  dst = (int16_t)n;
  return true;
}

static bool setBool(JsonObjectConst o, const char* key, bool& dst) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return false;
  bool b = v.as<bool>();
  if (b == dst) return false;
  dst = b;
  return true;
}

static bool setStr(JsonObjectConst o, const char* key, char* dst, size_t size) {
  JsonVariantConst v = o[key];
  if (!v.is<const char*>()) return false;
  const char* s = v.as<const char*>();
  if (strncmp(s, dst, size) == 0) return false;
  strlcpy(dst, s, size);
  return true;
}

static bool setColor(JsonObjectConst o, const char* key, uint32_t& dst) {
  JsonVariantConst v = o[key];
  if (!v.is<const char*>()) return false;
  const char* s = v.as<const char*>();
  if (*s == '#') s++;
  uint32_t c = strtoul(s, nullptr, 16) & 0xFFFFFF;
  if (c == dst) return false;
  dst = c;
  return true;
}

uint16_t Cfg::fromJson(JsonObjectConst o) {
  uint16_t chg = 0;
  auto mark = [&](bool changed, uint16_t flag) { if (changed) chg |= CHG_ANY | flag; };

  mark(setStr(o, "hostname", S.hostname, sizeof(S.hostname)), CHG_HOST);
  mark(setStr(o, "adminPass", S.adminPass, sizeof(S.adminPass)), CHG_AUTH);

  mark(setStr(o, "ssid", S.wifiSsid, sizeof(S.wifiSsid)), CHG_WIFI);
  mark(setStr(o, "pass", S.wifiPass, sizeof(S.wifiPass)), CHG_WIFI);

  mark(setStr(o, "tz", S.tz, sizeof(S.tz)), CHG_TIME);
  mark(setStr(o, "ntp", S.ntp, sizeof(S.ntp)), CHG_TIME);
  mark(setBool(o, "h12", S.h12), 0);
  mark(setBool(o, "lz", S.leadingZero), 0);
  mark(setBool(o, "blink", S.blinkColon), 0);

  mark(setNum(o, "tClock", S.tClock, 0, 3600), 0);
  mark(setNum(o, "tTemp", S.tTemp, 0, 3600), 0);
  mark(setNum(o, "tHumi", S.tHumi, 0, 3600), 0);
  mark(setNum(o, "trans", S.transition, 0, 2), 0);
  mark(setBool(o, "tDec", S.tempDecimal), 0);
  mark(setBool(o, "tF", S.fahrenheit), 0);

  mark(setNum(o, "cMode", S.colorMode, 0, 2), 0);
  mark(setColor(o, "cClock", S.cClock), 0);
  mark(setColor(o, "cTemp", S.cTemp), 0);
  mark(setColor(o, "cHumi", S.cHumi), 0);
  mark(setNum(o, "rbSpd", S.rainbowSpeed, 1, 10), 0);

  mark(setBool(o, "bAuto", S.autoBright), 0);
  mark(setNum(o, "bMin", S.bMin, 1, 255), 0);
  mark(setNum(o, "bMax", S.bMax, 1, 255), 0);
  mark(setNum(o, "bMan", S.bManual, 1, 255), 0);
  mark(setNum(o, "luxLo", S.luxLow, 0, 65000), 0);
  mark(setNum(o, "luxHi", S.luxHigh, 1, 65000), 0);

  mark(setNum(o, "nMode", S.nightMode, 0, 2), 0);
  mark(setNum(o, "nLux", S.nightLux, 0, 65000), 0);
  mark(setNum(o, "nStart", S.nightStart, 0, 23), 0);
  mark(setNum(o, "nEnd", S.nightEnd, 0, 23), 0);
  mark(setColor(o, "nColor", S.nightColor), 0);
  mark(setNum(o, "nBright", S.nightBright, 1, 255), 0);
  mark(setBool(o, "nClock", S.nightClockOnly), 0);

  mark(setTenths(o, "tOff", S.tempOffset10, -200, 200), 0);
  mark(setTenths(o, "hOff", S.humiOffset10, -300, 300), 0);
  mark(setNum(o, "sInt", S.sensorInterval, 2, 600), 0);

  mark(setBool(o, "mqEn", S.mqttEnabled), CHG_MQTT);
  mark(setStr(o, "mqHost", S.mqttHost, sizeof(S.mqttHost)), CHG_MQTT);
  mark(setNum(o, "mqPort", S.mqttPort, 1, 65535), CHG_MQTT);
  mark(setStr(o, "mqUser", S.mqttUser, sizeof(S.mqttUser)), CHG_MQTT);
  mark(setStr(o, "mqPass", S.mqttPass, sizeof(S.mqttPass)), CHG_MQTT);
  mark(setStr(o, "mqTopic", S.mqttTopic, sizeof(S.mqttTopic)), CHG_MQTT);
  mark(setNum(o, "mqInt", S.mqttInterval, 5, 3600), 0);

  if (S.bMin > S.bMax) S.bMin = S.bMax;
  if (S.luxLow >= S.luxHigh) S.luxHigh = S.luxLow + 1;
  if (!S.hostname[0]) strlcpy(S.hostname, "relogio", sizeof(S.hostname));

  return chg;
}
