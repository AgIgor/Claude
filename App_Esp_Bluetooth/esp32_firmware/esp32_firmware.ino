// Firmware de teste: controla saidas e le entradas via Bluetooth Classic (SPP).
// Requer ESP32 "classico" (ESP32-WROOM/DevKit). ESP32-S3/C3 NAO tem Bluetooth Classic.
//
// Protocolo (texto, uma linha por mensagem, terminada em '\n'):
//   App -> ESP : "O<n><v>"  liga/desliga saida n (1..4), v = 0/1   ex.: "O21"
//                "?"        pede o status imediatamente
//   ESP -> App : "S,<saidas>,<entradas>,<analogico>"   ex.: "S,1000,0110,2048"

#include <BluetoothSerial.h>
#include <esp_gap_bt_api.h>

BluetoothSerial SerialBT;

const char *BT_NAME = "ESP32_IO";

const int OUT_PINS[] = {2, 4, 16, 17};    // GPIO2 = LED onboard na maioria das placas
const int IN_PINS[]  = {13, 14, 27, 26};  // com pull-up interno: solto = 1, ligado ao GND = 0
const int ANALOG_PIN = 34;                // 0..4095
const int N_OUT = sizeof(OUT_PINS) / sizeof(OUT_PINS[0]);
const int N_IN  = sizeof(IN_PINS) / sizeof(IN_PINS[0]);

bool outState[N_OUT] = {false};
String rxLine;
String lastStatus;
unsigned long lastSend = 0;

String buildStatus() {
  String s = "S,";
  for (int i = 0; i < N_OUT; i++) s += outState[i] ? '1' : '0';
  s += ',';
  for (int i = 0; i < N_IN; i++) s += digitalRead(IN_PINS[i]) ? '1' : '0';
  s += ',';
  s += analogRead(ANALOG_PIN) / 16 * 16;  // arredonda para nao mandar ruido o tempo todo
  return s;
}

void sendStatus() {
  lastStatus = buildStatus();
  SerialBT.println(lastStatus);
  lastSend = millis();
}

void handleCommand(const String &cmd) {
  Serial.print("RX: ");
  Serial.println(cmd);
  if (cmd == "?") {
    sendStatus();
  } else if (cmd.length() == 3 && cmd[0] == 'O') {
    int n = cmd[1] - '1';
    if (n >= 0 && n < N_OUT && (cmd[2] == '0' || cmd[2] == '1')) {
      outState[n] = cmd[2] == '1';
      digitalWrite(OUT_PINS[n], outState[n] ? HIGH : LOW);
      sendStatus();
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < N_OUT; i++) {
    pinMode(OUT_PINS[i], OUTPUT);
    digitalWrite(OUT_PINS[i], LOW);
  }
  for (int i = 0; i < N_IN; i++) pinMode(IN_PINS[i], INPUT_PULLUP);

  if (!SerialBT.begin(BT_NAME)) {
    Serial.println("ERRO: falha ao iniciar o Bluetooth!");
    return;
  }

  // Alguns celulares Android escondem aparelhos de classe "nao categorizada".
  // Define uma classe de dispositivo (Computador) e forca o modo visivel.
  esp_bt_cod_t cod = {};
  cod.major = ESP_BT_COD_MAJOR_DEV_COMPUTER;
  cod.minor = 0;
  cod.service = ESP_BT_COD_SRVC_NONE;
  esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD);
  esp_err_t err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

  Serial.printf("Bluetooth iniciado como \"%s\"  MAC: %s  visivel: %s\n",
                BT_NAME, SerialBT.getBtAddressString().c_str(), err == ESP_OK ? "sim" : "ERRO");
  Serial.println("Procure em Configuracoes > Bluetooth > Parear novo dispositivo.");
}

void loop() {
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\n' || c == '\r') {
      rxLine.trim();
      if (rxLine.length()) handleCommand(rxLine);
      rxLine = "";
    } else if (rxLine.length() < 32) {
      rxLine += c;
    }
  }

  // Envia status quando algo muda (checa a cada 100 ms) ou a cada 1 s como "heartbeat"
  static unsigned long lastCheck = 0;
  if (SerialBT.hasClient() && millis() - lastCheck >= 100) {
    lastCheck = millis();
    if (buildStatus() != lastStatus || millis() - lastSend >= 1000) sendStatus();
  }
}
