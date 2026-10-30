/*
  Gateway WiFi para Speeduino

  Faz polling dos "output channels" da ECU pela serial secundária usando o
  comando legado 'r' (o mesmo bloco de 130 bytes que o TunerStudio lê) e
  repassa cada quadro, sem decodificar, para os navegadores conectados via
  WebSocket. A decodificação e a exibição ficam na página (data/index.html).

  Protocolo (comms_legacy.cpp / comms_secondary.cpp do Speeduino):
    pedido:   'r', canId, 0x30, offsetLo, offsetHi, lenLo, lenHi
    resposta: 'r', 0x30, <len bytes>
  O eco "r 0x30" só é enviado com o protocolo da serial secundária em
  "Generic (Fixed List)", "Generic (ini File)" ou "Real Dash".
*/
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

static const uint16_t OCH_BLOCK_SIZE = 130;   // ochBlockSize do speeduino.ini
static const uint8_t  FRAME_TYPE_OCH = 0x01;  // primeiro byte do quadro binário no WebSocket

static HardwareSerial& ecuSerial = Serial2;
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

enum class PollState : uint8_t { Idle, WaitHeader, WaitCmd, WaitData, WaitSignature };

static PollState pollState = PollState::Idle;
static uint8_t   frame[1 + OCH_BLOCK_SIZE];   // [tipo][dados]
static uint16_t  framePos = 0;
static bool      haveFrame = false;
static uint32_t  requestSentAt = 0;
static uint32_t  lastPollAt = 0;
static uint32_t  lastByteAt = 0;

static bool      ecuOnline = false;
static uint8_t   consecutiveTimeouts = 0;
static uint32_t  totalTimeouts = 0;
static uint32_t  frameCount = 0;
static float     frameRate = 0;
static String    ecuSignature;
static bool      signatureKnown = false;

static void broadcastFrame()
{
  if (ws.count() == 0) { return; }
  // Se algum cliente está lento, descarta este quadro em vez de enfileirar.
  if (!ws.availableForWriteAll()) { return; }
  ws.binaryAll(frame, sizeof(frame));
}

static String statusJson()
{
  String s;
  s.reserve(256);
  s += F("{\"t\":\"status\",\"ecu\":");
  s += ecuOnline ? F("true") : F("false");
  s += F(",\"sig\":\"");
  s += ecuSignature;
  s += F("\",\"hz\":");
  s += String(frameRate, 1);
  s += F(",\"timeouts\":");
  s += totalTimeouts;
  s += F(",\"frames\":");
  s += frameCount;
  s += F(",\"clients\":");
  s += ws.count();
  s += F(",\"uptime\":");
  s += millis() / 1000;
  s += F(",\"rssi\":");
  s += (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  s += F(",\"apIp\":\"");
  s += WiFi.softAPIP().toString();
  s += F("\",\"staIp\":\"");
  s += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String();
  s += F("\",\"heap\":");
  s += ESP.getFreeHeap();
  s += '}';
  return s;
}

static void onFrameComplete()
{
  frameCount++;
  consecutiveTimeouts = 0;
  ecuOnline = true;
  haveFrame = true;
  broadcastFrame();
}

static void onResponseTimeout()
{
  totalTimeouts++;
  if (consecutiveTimeouts < 255) { consecutiveTimeouts++; }
  if (consecutiveTimeouts >= OFFLINE_AFTER_TIMEOUTS)
  {
    ecuOnline = false;
    signatureKnown = false;
  }
}

static void sendRequest(uint32_t now)
{
  while (ecuSerial.available() > 0) { ecuSerial.read(); } // descarta lixo de respostas antigas

  if (ecuOnline && !signatureKnown)
  {
    ecuSignature = "";
    ecuSerial.write('Q');
    pollState = PollState::WaitSignature;
  }
  else
  {
    const uint8_t req[7] = {
      'r', ECU_CAN_ID, 0x30,
      0x00, 0x00,                                                       // offset
      (uint8_t)(OCH_BLOCK_SIZE & 0xFF), (uint8_t)(OCH_BLOCK_SIZE >> 8) // tamanho
    };
    ecuSerial.write(req, sizeof(req));
    framePos = 0;
    pollState = PollState::WaitHeader;
  }
  requestSentAt = now;
  lastPollAt = now;
  lastByteAt = now;
}

static void pollEcu()
{
  const uint32_t now = millis();

  if (pollState == PollState::Idle)
  {
    if (now - lastPollAt >= POLL_INTERVAL_MS) { sendRequest(now); }
    return;
  }

  while ((ecuSerial.available() > 0) && (pollState != PollState::Idle))
  {
    const uint8_t b = ecuSerial.read();
    lastByteAt = now;

    switch (pollState)
    {
      case PollState::WaitHeader:
        if (b == 'r') { pollState = PollState::WaitCmd; }
        break;

      case PollState::WaitCmd:
        if (b == 0x30) { pollState = PollState::WaitData; }
        else if (b != 'r') { pollState = PollState::WaitHeader; }
        break;

      case PollState::WaitData:
        frame[1 + framePos] = b;
        framePos++;
        if (framePos >= OCH_BLOCK_SIZE)
        {
          onFrameComplete();
          pollState = PollState::Idle;
        }
        break;

      case PollState::WaitSignature:
        if ((b >= 0x20) && (b < 0x7F) && (ecuSignature.length() < 64)) { ecuSignature += (char)b; }
        break;

      default:
        break;
    }
  }

  if (pollState == PollState::WaitSignature)
  {
    // A assinatura não tem terminador: considera completa após 30 ms de silêncio.
    if ((ecuSignature.length() > 0) && (now - lastByteAt > 30))
    {
      signatureKnown = true;
      pollState = PollState::Idle;
    }
    else if (now - requestSentAt > RESPONSE_TIMEOUT_MS)
    {
      signatureKnown = true; // não insiste; segue com os dados
      pollState = PollState::Idle;
    }
    return;
  }

  if ((pollState != PollState::Idle) && (now - requestSentAt > RESPONSE_TIMEOUT_MS))
  {
    onResponseTimeout();
    pollState = PollState::Idle;
  }
}

static void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
  (void)srv; (void)arg; (void)data; (void)len;
  if (type == WS_EVT_CONNECT)
  {
    client->text(statusJson());
    if (haveFrame && ecuOnline) { client->binary(frame, sizeof(frame)); }
  }
}

static void setupWifi()
{
  const bool useSta = strlen(STA_SSID) > 0;
  WiFi.mode(useSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.setHostname(HOSTNAME);
  WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  if (useSta) { WiFi.begin(STA_SSID, STA_PASSWORD); }
  WiFi.setSleep(false); // menor latência no WebSocket

  if (MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); }

  Serial.printf("AP \"%s\" em http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

static void setupServer()
{
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json", statusJson());
  });

  if (LittleFS.exists("/index.html") || LittleFS.exists("/index.html.gz"))
  {
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("max-age=300");
  }
  else
  {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(200, "text/html",
        "<h2>Speeduino Gateway</h2><p>A p&aacute;gina n&atilde;o foi gravada no LittleFS.</p>"
        "<p>Rode <code>pio run -t uploadfs</code> e reinicie o ESP32.</p>"
        "<p><a href='/api/status'>/api/status</a></p>");
    });
  }

  server.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });
  server.begin();
}

void setup()
{
  Serial.begin(115200);
  ecuSerial.setRxBufferSize(512);
  ecuSerial.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);
  frame[0] = FRAME_TYPE_OCH;

  if (!LittleFS.begin(true)) { Serial.println("Falha ao montar LittleFS"); }

  setupWifi();
  setupServer();
}

void loop()
{
  pollEcu();

  static uint32_t lastStatusAt = 0;
  static uint32_t framesAtLastStatus = 0;
  const uint32_t now = millis();
  if (now - lastStatusAt >= 1000)
  {
    frameRate = (float)(frameCount - framesAtLastStatus) * 1000.0f / (float)(now - lastStatusAt);
    framesAtLastStatus = frameCount;
    lastStatusAt = now;
    ws.cleanupClients();
    if (ws.count() > 0) { ws.textAll(statusJson()); }
  }
}
