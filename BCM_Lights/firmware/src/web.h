// Wi-Fi em modo AP + servidor HTTP/WebSocket com a interface de controle.
#pragma once

namespace web {

constexpr const char* AP_SSID = "BCM-Lights";
constexpr const char* AP_PASSWORD = "bcm12345";  // mínimo 8 caracteres (WPA2)
constexpr const char* HOSTNAME = "bcm";          // http://bcm.local

void begin();
void loop();  // chamar continuamente no loop() do Arduino

}  // namespace web
