// Camada de enlace: ESP-NOW + fila de recepcao + autenticacao HMAC.
#pragma once
#include <Arduino.h>
#include "BcmProtocol.h"

namespace bcm {

struct RxFrame {
  uint8_t mac[6];
  Packet pkt;
};

class Link {
 public:
  bool begin(uint8_t channel = kChannel);
  bool addPeer(const uint8_t mac[6]);
  void removePeer(const uint8_t mac[6]);

  // Preenche magic/version e, se key != nullptr, assina o pacote.
  bool send(const uint8_t mac[6], Packet& pkt, const uint8_t* key);

  // Retira um quadro da fila (nao bloqueia). Use no loop().
  bool receive(RxFrame& out);

  static bool verify(const Packet& pkt, const uint8_t* key);
  static void sign(Packet& pkt, const uint8_t* key);
};

extern const uint8_t kBroadcast[6];

bool macEqual(const uint8_t a[6], const uint8_t b[6]);
String macToString(const uint8_t mac[6]);

}  // namespace bcm
