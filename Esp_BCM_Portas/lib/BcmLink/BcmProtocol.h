// Protocolo de comunicacao entre o ESP32 central e os modulos de porta.
// Compartilhado pelos dois firmwares: qualquer mudanca aqui exige regravar todos.
#pragma once
#include <stdint.h>

namespace bcm {

// Canal Wi-Fi fixo usado pelo ESP-NOW (todos os modulos precisam usar o mesmo).
constexpr uint8_t  kChannel  = 1;
constexpr uint8_t  kMagic    = 0xBC;
constexpr uint8_t  kVersion  = 1;
constexpr uint8_t  kKeyLen   = 16;
constexpr uint8_t  kTagLen   = 8;

// Posicao da porta. Definida por jumpers no modulo da porta.
enum Role : uint8_t {
  ROLE_FL = 0,  // dianteira esquerda (motorista)
  ROLE_FR = 1,  // dianteira direita
  ROLE_RL = 2,  // traseira esquerda
  ROLE_RR = 3,  // traseira direita
  ROLE_COUNT = 4,
  ROLE_CENTRAL = 0xFF,
};

inline const char* roleName(uint8_t r) {
  switch (r) {
    case ROLE_FL: return "DE";
    case ROLE_FR: return "DD";
    case ROLE_RL: return "TE";
    case ROLE_RR: return "TD";
    case ROLE_CENTRAL: return "CENTRAL";
    default: return "??";
  }
}
inline bool isFront(uint8_t r) { return r == ROLE_FL || r == ROLE_FR; }

enum MsgType : uint8_t {
  MSG_PAIR_BEACON  = 1,  // central -> broadcast: "estou em modo pareamento"
  MSG_PAIR_REQUEST = 2,  // porta -> central: "quero parear, sou a porta X"
  MSG_PAIR_ACCEPT  = 3,  // central -> porta: id da rede + chave
  MSG_PAIR_DONE    = 4,  // porta -> central: confirmacao (ja autenticada)
  MSG_COMMAND      = 10, // central -> porta
  MSG_ACK          = 11, // porta -> central: recebeu e executou/iniciou
  MSG_RESULT       = 12, // porta -> central: acao temporizada terminou (ou parou sozinha)
};

enum Cmd : uint8_t {
  CMD_PING   = 0,
  CMD_WINDOW = 1,  // arg: MOVE_*  (segurar-para-mover, precisa de refresh)
  CMD_LOCK   = 2,  // arg: LOCK_*
  CMD_LAMP   = 3,  // arg: 0/1 seta (pisca) - so dianteiras
  CMD_MIRROR = 4,  // arg: MOVE_*  (segurar-para-mover) - so dianteiras
  CMD_FOLD   = 5,  // arg: FOLD_*  - so dianteiras
  CMD_SLEEP  = 6,  // central vai cortar a alimentacao: porta grava o contador e responde
};

enum Move : uint8_t { MOVE_STOP = 0, MOVE_UP = 1, MOVE_DOWN = 2, MOVE_LEFT = 3, MOVE_RIGHT = 4 };
enum LockArg : uint8_t { LOCK_UNLOCK = 0, LOCK_LOCK = 1 };
enum FoldArg : uint8_t { FOLD_UNFOLD = 0, FOLD_FOLD = 1 };

enum Status : uint8_t {
  ST_OK          = 0,  // recebido e executado (ou iniciado, para acoes temporizadas)
  ST_UNSUPPORTED = 1,  // funcao nao existe nesta porta (ex.: espelho na traseira)
  ST_BAD_ARG     = 2,
  ST_DONE        = 3,  // (RESULT) acao terminou normalmente
  ST_STALL       = 4,  // (RESULT) motor travou / fim de curso por corrente
  ST_TIMEOUT     = 5,  // (RESULT) tempo maximo de acionamento atingido
  ST_ALREADY     = 6,  // ja estava no estado pedido, nada a fazer
};

inline const char* statusName(uint8_t s) {
  switch (s) {
    case ST_OK: return "OK";
    case ST_UNSUPPORTED: return "NAO SUPORTADO";
    case ST_BAD_ARG: return "ARGUMENTO INVALIDO";
    case ST_DONE: return "CONCLUIDO";
    case ST_STALL: return "PAROU POR CORRENTE (fim de curso/obstaculo)";
    case ST_TIMEOUT: return "TEMPO MAXIMO ATINGIDO";
    case ST_ALREADY: return "JA ESTAVA NO ESTADO";
    default: return "?";
  }
}

inline const char* cmdName(uint8_t c) {
  switch (c) {
    case CMD_PING: return "PING";
    case CMD_WINDOW: return "VIDRO";
    case CMD_LOCK: return "TRAVA";
    case CMD_LAMP: return "SETA";
    case CMD_MIRROR: return "ESPELHO";
    case CMD_FOLD: return "REBATIMENTO";
    case CMD_SLEEP: return "DESLIGAR";
    default: return "?";
  }
}

// Bits de estado que a porta devolve em todo ACK/RESULT.
enum StateBits : uint8_t {
  STATE_LOCKED        = 1 << 0,
  STATE_FOLDED        = 1 << 1,
  STATE_WINDOW_MOVING = 1 << 2,
  STATE_LAMP_ON       = 1 << 3,
  STATE_MIRROR_MOVING = 1 << 4,
};

enum Flags : uint8_t {
  FLAG_NO_ACK = 1 << 0,  // quadros de refresh: a porta nao responde
};

// Tempos de seguranca compartilhados.
constexpr uint32_t kHoldRefreshMs = 100;  // central reenvia comandos "segurar" a cada 100 ms
constexpr uint32_t kHoldTimeoutMs = 350;  // porta para o motor se nao receber refresh
constexpr uint32_t kLampTimeoutMs = 1000; // porta apaga a seta se a central sumir

struct __attribute__((packed)) Packet {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  type;     // MsgType
  uint8_t  flags;    // Flags
  uint32_t netId;    // identifica o "carro" (gerado pela central)
  uint32_t counter;  // contador crescente anti-replay
  uint16_t reqId;    // correlaciona comando <-> ACK (igual nas retransmissoes)
  uint8_t  role;     // porta de origem/destino
  uint8_t  cmd;
  uint8_t  arg;
  uint8_t  status;
  uint8_t  state;    // StateBits
  uint8_t  reserved;
  uint8_t  payload[kKeyLen];  // chave da rede (apenas no PAIR_ACCEPT)
  uint8_t  tag[kTagLen];      // HMAC-SHA256 truncado (zero nas mensagens de pareamento)
};
static_assert(sizeof(Packet) <= 250, "ESP-NOW limita o payload a 250 bytes");

}  // namespace bcm
