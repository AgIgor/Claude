#include "BcmLink.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <mbedtls/md.h>

namespace bcm {

const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static QueueHandle_t rxQueue = nullptr;

// Roda na task do Wi-Fi: so copia para a fila, o processamento e feito no loop().
static void handleRx(const uint8_t* mac, const uint8_t* data, int len) {
  if (!rxQueue || len != (int)sizeof(Packet)) return;
  const Packet* p = reinterpret_cast<const Packet*>(data);
  if (p->magic != kMagic || p->version != kVersion) return;
  RxFrame f;
  memcpy(f.mac, mac, 6);
  memcpy(&f.pkt, data, sizeof(Packet));
  xQueueSend(rxQueue, &f, 0);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  handleRx(info->src_addr, data, len);
}
#else
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  handleRx(mac, data, len);
}
#endif

bool Link::begin(uint8_t channel) {
  if (!rxQueue) rxQueue = xQueueCreate(24, sizeof(RxFrame));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);  // sem economia de energia: menor latencia
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  return true;
}

void Link::end() {
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
}

bool Link::addPeer(const uint8_t mac[6]) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;  // canal atual
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;  // autenticacao feita via HMAC na camada de aplicacao
  return esp_now_add_peer(&peer) == ESP_OK;
}

void Link::removePeer(const uint8_t mac[6]) {
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
}

static void computeTag(const Packet& pkt, const uint8_t* key, uint8_t out[kTagLen]) {
  uint8_t full[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, key, kKeyLen, reinterpret_cast<const uint8_t*>(&pkt),
                  offsetof(Packet, tag), full);
  memcpy(out, full, kTagLen);
}

void Link::sign(Packet& pkt, const uint8_t* key) { computeTag(pkt, key, pkt.tag); }

bool Link::verify(const Packet& pkt, const uint8_t* key) {
  uint8_t expected[kTagLen];
  computeTag(pkt, key, expected);
  uint8_t diff = 0;  // comparacao em tempo constante
  for (int i = 0; i < kTagLen; i++) diff |= expected[i] ^ pkt.tag[i];
  return diff == 0;
}

bool Link::send(const uint8_t mac[6], Packet& pkt, const uint8_t* key) {
  pkt.magic = kMagic;
  pkt.version = kVersion;
  if (key) sign(pkt, key);
  else memset(pkt.tag, 0, kTagLen);
  return esp_now_send(mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(Packet)) == ESP_OK;
}

bool Link::receive(RxFrame& out) {
  return rxQueue && xQueueReceive(rxQueue, &out, 0) == pdTRUE;
}

bool macEqual(const uint8_t a[6], const uint8_t b[6]) { return memcmp(a, b, 6) == 0; }

String macToString(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
  return String(buf);
}

}  // namespace bcm
