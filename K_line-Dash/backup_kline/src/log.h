#pragma once
#include <Arduino.h>

// Log em buffer circular, exibido na aba "Diagnóstico" e na serial.
void logLine(const char* fmt, ...);
String logDump();
String hexStr(const uint8_t* d, size_t n);
