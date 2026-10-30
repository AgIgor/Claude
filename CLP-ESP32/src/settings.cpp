#include "config.h"
#include "settings.h"
#include <Preferences.h>

Settings g_settings;

void settingsLoad() {
  Preferences p;
  p.begin("clp", true);
  g_settings.ssid = p.getString("ssid", "");
  g_settings.pass = p.getString("pass", "");
  g_settings.hostname = p.getString("host", DEFAULT_HOSTNAME);
  g_settings.diInvert = p.getUInt("diInv", 0);
  g_settings.diPullUp = p.getBool("diPU", false);
  p.end();
}

void settingsSave() {
  Preferences p;
  p.begin("clp", false);
  p.putString("ssid", g_settings.ssid);
  p.putString("pass", g_settings.pass);
  p.putString("host", g_settings.hostname);
  p.putUInt("diInv", g_settings.diInvert);
  p.putBool("diPU", g_settings.diPullUp);
  p.end();
}

void retainLoad(int32_t* d, size_t n) {
  Preferences p;
  p.begin("clp", true);
  if (p.getBytesLength("retD") == n * sizeof(int32_t)) {
    p.getBytes("retD", d, n * sizeof(int32_t));
  }
  p.end();
}

void retainSave(const int32_t* d, size_t n) {
  Preferences p;
  p.begin("clp", false);
  p.putBytes("retD", d, n * sizeof(int32_t));
  p.end();
}
