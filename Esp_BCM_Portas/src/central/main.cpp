// ============================================================================
//  Esp_BCM - CENTRAL
//  Le os botoes do painel e envia comandos sem fio (ESP-NOW) para as portas.
//  Cada comando confiavel e reenviado ate a porta responder com ACK.
// ============================================================================
#include <Arduino.h>
#include <Preferences.h>
#if __has_include(<esp_random.h>)
#include <esp_random.h>
#else
#include <esp_system.h>
#endif

#include <BcmLink.h>
#include <Button.h>
#include "config.h"

using namespace bcm;

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
struct DoorSlot {
  bool paired = false;
  uint8_t mac[6] = {};
  uint32_t lastSeen = 0;
  bool online = false;
  uint8_t state = 0;
};

enum SendMode : uint8_t {
  FIRE_AND_FORGET,  // refresh de comandos "segurar": porta nao responde
  ACK_ONLY,         // ping: espera resposta, mas nao retransmite
  RELIABLE,         // retransmite ate receber ACK (ou MAX_TRIES)
};

struct Pending {
  bool used;
  bool retry;  // false = substituido por comando mais novo, so aguarda o ACK
  uint8_t door, cmd, arg;
  uint16_t reqId;
  uint8_t tries;
  uint32_t sentAt;
};

struct VirtualHold {  // "aperto de botao" simulado pelo terminal serial
  uint8_t dir = MOVE_STOP;
  uint32_t until = 0;
};

enum TurnMode : uint8_t { TURN_OFF, TURN_LEFT, TURN_RIGHT, TURN_HAZARD };

static Link radio;
static Preferences prefs;

static DoorSlot doors[ROLE_COUNT];
static uint32_t netId = 0;
static uint8_t netKey[kKeyLen];

static constexpr uint32_t kCounterBlock = 1000;
static uint32_t txCounter = 0, counterReserved = 0;
static uint16_t reqSeq = 0;
static Pending pend[16];

static bool pairing = false;
static uint32_t pairingUntil = 0, lastBeacon = 0, lastPing = 0;

static uint8_t winLast[ROLE_COUNT] = {};
static uint32_t winTx[ROLE_COUNT] = {};
static VirtualHold cliWin[ROLE_COUNT];

static uint8_t mirLast = MOVE_STOP, mirLastDoor = ROLE_FL, cliMirDoor = ROLE_FL;
static uint32_t mirTx = 0;
static VirtualHold cliMir;
static bool foldState = false;

static bool hazard = false;
static uint8_t cliTurn = TURN_OFF, turnMode = TURN_OFF;
static bool blinkOn = false;
static uint32_t blinkAt = 0;

// ---------------------------------------------------------------------------
// Entradas
// ---------------------------------------------------------------------------
// Pino -1 = entrada desativada (porta nao instalada): Button nunca reporta pressionado.
static constexpr int8_t pinIf(bool used, int8_t pin) { return used ? pin : -1; }
static constexpr int8_t winPin(uint8_t d, uint8_t i) { return pinIf(DOOR_INSTALLED[d], PIN_WIN[d][i]); }
static constexpr int8_t frontPin(int8_t pin) { return pinIf(FRONT_INSTALLED, pin); }

static Button btnPair(PIN_PAIR);
static Button btnWin[ROLE_COUNT * 2] = {
    Button(winPin(0, 0)), Button(winPin(0, 1)), Button(winPin(1, 0)), Button(winPin(1, 1)),
    Button(winPin(2, 0)), Button(winPin(2, 1)), Button(winPin(3, 0)), Button(winPin(3, 1)),
};
static Button btnLock(PIN_LOCK), btnUnlock(PIN_UNLOCK);
static Button btnTurnL(frontPin(PIN_TURN_L)), btnTurnR(frontPin(PIN_TURN_R));
static Button btnHazard(frontPin(PIN_HAZARD));
static Button btnMirSel(frontPin(PIN_MIR_SEL));
static Button btnMirUp(frontPin(PIN_MIR_UP), false), btnMirDown(frontPin(PIN_MIR_DOWN), false);
static Button btnMirLeft(frontPin(PIN_MIR_LEFT), false);
static Button btnMirRight(frontPin(PIN_MIR_RIGHT), false);
static Button btnFold(frontPin(PIN_FOLD));

static Button* const allButtons[] = {
    &btnPair, &btnWin[0], &btnWin[1], &btnWin[2], &btnWin[3], &btnWin[4], &btnWin[5],
    &btnWin[6], &btnWin[7], &btnLock, &btnUnlock, &btnTurnL, &btnTurnR, &btnHazard,
    &btnMirSel, &btnMirUp, &btnMirDown, &btnMirLeft, &btnMirRight, &btnFold,
};

// ---------------------------------------------------------------------------
// Persistencia (NVS)
// ---------------------------------------------------------------------------
static void macKey(uint8_t door, char* out) { snprintf(out, 8, "mac%u", door); }

static void newNetwork() {
  do { netId = esp_random(); } while (netId == 0);
  esp_fill_random(netKey, kKeyLen);
  prefs.putUInt("netid", netId);
  prefs.putBytes("key", netKey, kKeyLen);
}

static void loadConfig() {
  prefs.begin("bcm", false);
  netId = prefs.getUInt("netid", 0);
  if (netId == 0 || !prefs.isKey("key") || prefs.getBytes("key", netKey, kKeyLen) != kKeyLen)
    newNetwork();

  for (uint8_t d = 0; d < ROLE_COUNT; d++) {
    char k[8];
    macKey(d, k);
    doors[d].paired = prefs.isKey(k) && prefs.getBytes(k, doors[d].mac, 6) == 6;
    if (doors[d].paired && !DOOR_INSTALLED[d]) {  // pareamento antigo de porta removida
      doors[d] = DoorSlot();
      prefs.remove(k);
    }
  }

  // Contador anti-replay: reserva blocos para nunca repetir valores apos reset.
  txCounter = prefs.getUInt("ctr", 1);
  counterReserved = txCounter + kCounterBlock;
  prefs.putUInt("ctr", counterReserved);
}

static uint32_t nextCounter() {
  if (++txCounter >= counterReserved) {
    counterReserved += kCounterBlock;
    prefs.putUInt("ctr", counterReserved);
  }
  return txCounter;
}

static void forgetDoor(uint8_t d) {
  if (!doors[d].paired) return;
  radio.removePeer(doors[d].mac);
  doors[d] = DoorSlot();
  char k[8];
  macKey(d, k);
  prefs.remove(k);
}

// ---------------------------------------------------------------------------
// Envio de comandos
// ---------------------------------------------------------------------------
static const char* argName(uint8_t cmd, uint8_t arg) {
  static const char* const win[] = {"PARAR", "SUBIR", "DESCER"};
  static const char* const mir[] = {"PARAR", "CIMA", "BAIXO", "ESQUERDA", "DIREITA"};
  switch (cmd) {
    case CMD_WINDOW: return arg < 3 ? win[arg] : "?";
    case CMD_MIRROR: return arg < 5 ? mir[arg] : "?";
    case CMD_LOCK: return arg ? "TRAVAR" : "DESTRAVAR";
    case CMD_LAMP: return arg ? "ACESA" : "APAGADA";
    case CMD_FOLD: return arg ? "REBATER" : "ABRIR";
    default: return "";
  }
}

static void transmit(uint8_t door, uint8_t cmd, uint8_t arg, uint8_t flags, uint16_t reqId) {
  Packet p = {};
  p.type = MSG_COMMAND;
  p.flags = flags;
  p.netId = netId;
  p.counter = nextCounter();  // retransmissoes ganham contador novo; reqId continua o mesmo
  p.reqId = reqId;
  p.role = door;
  p.cmd = cmd;
  p.arg = arg;
  radio.send(doors[door].mac, p, netKey);
}

static void sendCommand(uint8_t door, uint8_t cmd, uint8_t arg, SendMode mode) {
  if (door >= ROLE_COUNT || !doors[door].paired) return;

  // Um comando mais novo do mesmo tipo cancela as retransmissoes do anterior
  // (evita, por exemplo, um "SUBIR" atrasado chegar depois do "PARAR").
  for (auto& e : pend)
    if (e.used && e.door == door && e.cmd == cmd) e.retry = false;

  uint16_t id = ++reqSeq;
  transmit(door, cmd, arg, mode == FIRE_AND_FORGET ? FLAG_NO_ACK : 0, id);
  if (mode != RELIABLE) return;

  for (auto& e : pend) {
    if (!e.used) {
      e = {true, true, door, cmd, arg, id, 1, millis()};
      return;
    }
  }
  Serial.println("! fila de comandos pendentes cheia");
}

static void processRetries() {
  uint32_t now = millis();
  for (auto& e : pend) {
    if (!e.used || now - e.sentAt < RETRY_TIMEOUT_MS) continue;
    if (!e.retry) { e.used = false; continue; }
    if (e.tries >= MAX_TRIES) {
      Serial.printf("[%s] %s %s -> SEM RESPOSTA (%u tentativas)\n", roleName(e.door),
                    cmdName(e.cmd), argName(e.cmd, e.arg), e.tries);
      e.used = false;
      continue;
    }
    e.tries++;
    e.sentAt = now;
    transmit(e.door, e.cmd, e.arg, 0, e.reqId);
  }
}

// ---------------------------------------------------------------------------
// Recepcao
// ---------------------------------------------------------------------------
static int doorByMac(const uint8_t mac[6]) {
  for (uint8_t d = 0; d < ROLE_COUNT; d++)
    if (doors[d].paired && macEqual(doors[d].mac, mac)) return d;
  return -1;
}

static void onPairRequest(const RxFrame& f) {
  if (!pairing) return;
  uint8_t r = f.pkt.role;
  if (r >= ROLE_COUNT) return;
  if (!DOOR_INSTALLED[r]) {
    static uint32_t lastWarn = 0;
    if (millis() - lastWarn > 2000) {
      lastWarn = millis();
      Serial.printf("! Modulo %s pediu pareamento, mas essa porta esta desativada em "
                    "DOOR_INSTALLED (confira os jumpers)\n", roleName(r));
    }
    return;
  }

  // O mesmo modulo estava em outra posicao (jumpers trocados)? Libera a antiga.
  for (uint8_t d = 0; d < ROLE_COUNT; d++)
    if (d != r && doors[d].paired && macEqual(doors[d].mac, f.mac)) forgetDoor(d);

  if (!doors[r].paired || !macEqual(doors[r].mac, f.mac)) {
    forgetDoor(r);  // substitui o modulo antigo desta posicao
    memcpy(doors[r].mac, f.mac, 6);
    doors[r].paired = true;
    char k[8];
    macKey(r, k);
    prefs.putBytes(k, f.mac, 6);
    Serial.printf("[%s] PAREADA  mac=%s\n", roleName(r), macToString(f.mac).c_str());
  }
  radio.addPeer(f.mac);

  Packet a = {};
  a.type = MSG_PAIR_ACCEPT;
  a.netId = netId;
  a.counter = nextCounter();
  a.role = r;
  memcpy(a.payload, netKey, kKeyLen);
  radio.send(f.mac, a, nullptr);

  bool all = true;
  for (uint8_t d = 0; d < ROLE_COUNT; d++) all &= doors[d].paired || !DOOR_INSTALLED[d];
  if (all && pairingUntil - millis() > 3000) {
    Serial.println("Todas as portas pareadas - encerrando pareamento em 3 s");
    pairingUntil = millis() + 3000;  // da tempo da porta receber o ACCEPT
  }
}

static void onAck(uint8_t d, const Packet& p) {
  for (auto& e : pend) {
    if (e.used && e.door == d && e.reqId == p.reqId) {
      e.used = false;
      Serial.printf("[%s] %s %s -> %s", roleName(d), cmdName(e.cmd), argName(e.cmd, e.arg),
                    statusName(p.status));
      if (e.tries > 1) Serial.printf(" (tentativa %u)", e.tries);
      Serial.println();
      return;
    }
  }
}

static void handleFrame(const RxFrame& f) {
  const Packet& p = f.pkt;
  if (p.type == MSG_PAIR_REQUEST) { onPairRequest(f); return; }
  if (p.type != MSG_ACK && p.type != MSG_RESULT && p.type != MSG_PAIR_DONE) return;

  int d = doorByMac(f.mac);
  if (d < 0 || p.netId != netId || !Link::verify(p, netKey)) return;
  doors[d].lastSeen = millis();
  doors[d].state = p.state;

  if (p.type == MSG_ACK) onAck(d, p);
  else if (p.type == MSG_RESULT)
    Serial.printf("[%s] %s %s -> %s\n", roleName(d), cmdName(p.cmd), argName(p.cmd, p.arg),
                  statusName(p.status));
  else
    Serial.printf("[%s] pareamento confirmado pela porta\n", roleName(d));
}

// ---------------------------------------------------------------------------
// Status / pareamento
// ---------------------------------------------------------------------------
static void printStatus() {
  Serial.printf("\n--- Rede %08lX | pareamento: %s ---\n", (unsigned long)netId,
                pairing ? "ATIVO" : "inativo");
  for (uint8_t d = 0; d < ROLE_COUNT; d++) {
    const DoorSlot& s = doors[d];
    if (!DOOR_INSTALLED[d]) { Serial.printf("  %s: nao instalada\n", roleName(d)); continue; }
    if (!s.paired) { Serial.printf("  %s: nao pareada\n", roleName(d)); continue; }
    Serial.printf("  %s: %s  %-7s  %s%s%s%s\n", roleName(d), macToString(s.mac).c_str(),
                  s.online ? "ONLINE" : "offline", s.state & STATE_LOCKED ? "travada " : "destravada ",
                  isFront(d) ? (s.state & STATE_FOLDED ? "rebatido " : "espelho aberto ") : "",
                  s.state & STATE_WINDOW_MOVING ? "vidro-movendo " : "",
                  s.state & STATE_LAMP_ON ? "seta-acesa" : "");
  }
  Serial.println();
}

static void startPairing() {
  pairing = true;
  pairingUntil = millis() + PAIR_WINDOW_MS;
  Serial.printf(">> MODO PAREAMENTO por %lu s: segure o botao PAIR de cada porta por 3 s\n",
                (unsigned long)(PAIR_WINDOW_MS / 1000));
}

static void stopPairing() {
  pairing = false;
  Serial.println(">> Pareamento encerrado");
  printStatus();
}

static void factoryReset() {
  for (uint8_t d = 0; d < ROLE_COUNT; d++) forgetDoor(d);
  newNetwork();
  Serial.printf(">> Pareamentos apagados. Nova rede %08lX - pareie as portas novamente.\n",
                (unsigned long)netId);
}

static void handlePairing() {
  uint32_t now = millis();
  if (btnPair.released()) {
    uint32_t h = btnPair.heldOnReleaseMs();
    if (h >= RESET_HOLD_MS) factoryReset();
    else if (h >= PAIR_HOLD_MS) pairing ? stopPairing() : startPairing();
    else printStatus();
  }
  if (!pairing) return;
  if ((int32_t)(now - pairingUntil) >= 0) { stopPairing(); return; }
  if (now - lastBeacon >= 250) {
    lastBeacon = now;
    Packet b = {};
    b.type = MSG_PAIR_BEACON;
    b.netId = netId;
    b.role = ROLE_CENTRAL;
    radio.send(kBroadcast, b, nullptr);
  }
}

static uint8_t foldRefDoor() { return doors[ROLE_FL].paired ? ROLE_FL : ROLE_FR; }

static void updatePresence() {
  uint32_t now = millis();
  if (now - lastPing >= PING_INTERVAL_MS) {
    lastPing = now;
    for (uint8_t d = 0; d < ROLE_COUNT; d++) sendCommand(d, CMD_PING, 0, ACK_ONLY);
  }
  for (uint8_t d = 0; d < ROLE_COUNT; d++) {
    DoorSlot& s = doors[d];
    if (!s.paired) continue;
    bool on = s.lastSeen != 0 && now - s.lastSeen < OFFLINE_AFTER_MS;
    if (on == s.online) continue;
    s.online = on;
    Serial.printf("[%s] %s\n", roleName(d), on ? "ONLINE" : "OFFLINE (sem resposta)");
    if (on && d == foldRefDoor()) foldState = s.state & STATE_FOLDED;
  }
}

// ---------------------------------------------------------------------------
// Funcoes do carro
// ---------------------------------------------------------------------------
static uint8_t virt(const VirtualHold& v) {
  return (v.dir != MOVE_STOP && (int32_t)(millis() - v.until) < 0) ? v.dir : MOVE_STOP;
}

// Comandos "segurar para mover": borda -> confiavel; enquanto segura -> refresh a cada 100 ms.
static void serviceHold(uint8_t door, uint8_t cmd, uint8_t want, uint8_t& last, uint32_t& lastTx) {
  uint32_t now = millis();
  if (want != last) {
    if (want != MOVE_STOP && !doors[door].paired)
      Serial.printf("[%s] porta nao pareada - comando ignorado\n", roleName(door));
    sendCommand(door, cmd, want, RELIABLE);
    last = want;
    lastTx = now;
  } else if (want != MOVE_STOP && now - lastTx >= kHoldRefreshMs) {
    sendCommand(door, cmd, want, FIRE_AND_FORGET);
    lastTx = now;
  }
}

static void handleWindows() {
  for (uint8_t d = 0; d < ROLE_COUNT; d++) {
    bool up = btnWin[d * 2].isPressed(), down = btnWin[d * 2 + 1].isPressed();
    uint8_t want = (up == down) ? MOVE_STOP : up ? MOVE_UP : MOVE_DOWN;
    if (want == MOVE_STOP) want = virt(cliWin[d]);
    serviceHold(d, CMD_WINDOW, want, winLast[d], winTx[d]);
  }
}

static void setFold(bool fold) {
  if (!FRONT_INSTALLED) return;
  foldState = fold;
  Serial.printf(">> %s espelhos\n", fold ? "Rebatendo" : "Abrindo");
  sendCommand(ROLE_FL, CMD_FOLD, fold ? FOLD_FOLD : FOLD_UNFOLD, RELIABLE);
  sendCommand(ROLE_FR, CMD_FOLD, fold ? FOLD_FOLD : FOLD_UNFOLD, RELIABLE);
}

static void setLock(bool lock) {
  Serial.printf(">> %s portas\n", lock ? "Travando" : "Destravando");
  for (uint8_t d = 0; d < ROLE_COUNT; d++)
    sendCommand(d, CMD_LOCK, lock ? LOCK_LOCK : LOCK_UNLOCK, RELIABLE);
  if (AUTO_FOLD_ON_LOCK) setFold(lock);
}

static void handleLocks() {
  if (btnLock.pressed()) setLock(true);
  if (btnUnlock.pressed()) setLock(false);
}

static void handleMirrors() {
  uint8_t door = btnMirSel.isPressed() ? ROLE_FR : ROLE_FL;
  uint8_t want = btnMirUp.isPressed()      ? MOVE_UP
                 : btnMirDown.isPressed()  ? MOVE_DOWN
                 : btnMirLeft.isPressed()  ? MOVE_LEFT
                 : btnMirRight.isPressed() ? MOVE_RIGHT
                                           : MOVE_STOP;
  if (want == MOVE_STOP && virt(cliMir) != MOVE_STOP) {
    want = virt(cliMir);
    door = cliMirDoor;
  }
  if (door != mirLastDoor) {  // trocou o seletor com o espelho em movimento
    if (mirLast != MOVE_STOP) sendCommand(mirLastDoor, CMD_MIRROR, MOVE_STOP, RELIABLE);
    mirLast = MOVE_STOP;
    mirLastDoor = door;
  }
  serviceHold(door, CMD_MIRROR, want, mirLast, mirTx);

  if (btnFold.pressed()) setFold(!foldState);
}

static bool lampWanted(uint8_t door, uint8_t mode) {
  return mode == TURN_HAZARD || (mode == TURN_LEFT && door == ROLE_FL) ||
         (mode == TURN_RIGHT && door == ROLE_FR);
}

// A central dita o ritmo do pisca (as duas portas ficam sincronizadas no alerta).
static void handleTurnSignals() {
  if (btnHazard.pressed()) {
    hazard = !hazard;
    Serial.printf(">> Pisca-alerta %s\n", hazard ? "LIGADO" : "DESLIGADO");
  }
  bool l = btnTurnL.isPressed() || cliTurn == TURN_LEFT;
  bool r = btnTurnR.isPressed() || cliTurn == TURN_RIGHT;
  uint8_t mode = hazard ? TURN_HAZARD : (l && !r) ? TURN_LEFT : (r && !l) ? TURN_RIGHT : TURN_OFF;
  uint32_t now = millis();

  if (mode != turnMode) {
    turnMode = mode;
    blinkOn = mode != TURN_OFF;
    blinkAt = now;
    for (uint8_t d : {ROLE_FL, ROLE_FR})
      sendCommand(d, CMD_LAMP, blinkOn && lampWanted(d, mode), RELIABLE);
  } else if (mode != TURN_OFF && now - blinkAt >= BLINK_HALF_MS) {
    blinkOn = !blinkOn;
    blinkAt = now;
    for (uint8_t d : {ROLE_FL, ROLE_FR})
      if (lampWanted(d, mode)) sendCommand(d, CMD_LAMP, blinkOn, FIRE_AND_FORGET);
  }
  digitalWrite(PIN_TURN_INDICATOR, turnMode != TURN_OFF && blinkOn);
}

static void updateLed() {
  uint32_t now = millis();
  bool on;
  uint32_t held = btnPair.heldMs();
  if (held >= RESET_HOLD_MS) on = true;                    // solte para apagar tudo
  else if (held >= PAIR_HOLD_MS) on = (now / 50) % 2;      // solte para parear
  else if (pairing) on = (now / 100) % 2;                  // pareando
  else {
    uint8_t paired = 0, online = 0;
    for (auto& d : doors) { paired += d.paired; online += d.online; }
    if (paired == 0) on = (now % 2000) < 60;               // nada pareado
    else if (online == paired) on = true;                  // tudo ok
    else on = (now / 500) % 2;                             // alguma porta offline
  }
  digitalWrite(PIN_LED, on);
}

// ---------------------------------------------------------------------------
// Terminal serial (para testar sem montar todos os botoes)
// ---------------------------------------------------------------------------
static void printHelp() {
  Serial.println(
      "Comandos (Serial 115200, fim de linha LF):\n"
      "  parear | reset | status\n"
      "  travar | destravar\n"
      "  vidro <de|dd|te|td> <sobe|desce|para> [ms=2000]\n"
      "  seta <e|d|off>  |  alerta\n"
      "  espelho <e|d> <cima|baixo|esq|dir> [ms=500]\n"
      "  rebater | abrir");
}

static int parseDoor(const char* s) {
  if (!s) return -1;
  for (uint8_t d = 0; d < ROLE_COUNT; d++) {
    String n = roleName(d);
    n.toLowerCase();
    if (n == s) return d;
  }
  return -1;
}

static uint32_t parseMs(const char* s, uint32_t def) { return s ? strtoul(s, nullptr, 10) : def; }

static void runCommand(char* line) {
  for (char* c = line; *c; c++) *c = tolower(*c);
  char* a0 = strtok(line, " ");
  char* a1 = strtok(nullptr, " ");
  char* a2 = strtok(nullptr, " ");
  char* a3 = strtok(nullptr, " ");
  if (!a0) return;
  String cmd = a0;

  if (cmd == "parear") startPairing();
  else if (cmd == "reset") factoryReset();
  else if (cmd == "status") printStatus();
  else if (cmd == "travar") setLock(true);
  else if (cmd == "destravar") setLock(false);
  else if (cmd == "rebater") setFold(true);
  else if (cmd == "abrir") setFold(false);
  else if (cmd == "alerta") { hazard = !hazard; Serial.printf(">> Pisca-alerta %s\n", hazard ? "LIGADO" : "DESLIGADO"); }
  else if (cmd == "seta" && a1) {
    String s = a1;
    cliTurn = s == "e" ? TURN_LEFT : s == "d" ? TURN_RIGHT : TURN_OFF;
  } else if (cmd == "vidro" && parseDoor(a1) >= 0 && a2) {
    String s = a2;
    VirtualHold& v = cliWin[parseDoor(a1)];
    v.dir = s == "sobe" ? MOVE_UP : s == "desce" ? MOVE_DOWN : MOVE_STOP;
    v.until = millis() + parseMs(a3, 2000);
  } else if (cmd == "espelho" && a1 && a2) {
    String side = a1, s = a2;
    cliMirDoor = side == "d" ? ROLE_FR : ROLE_FL;
    cliMir.dir = s == "cima" ? MOVE_UP : s == "baixo" ? MOVE_DOWN : s == "esq" ? MOVE_LEFT
               : s == "dir" ? MOVE_RIGHT : MOVE_STOP;
    cliMir.until = millis() + parseMs(a3, 500);
  } else printHelp();
}

static void handleSerial() {
  static char buf[64];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[n] = 0; runCommand(buf); n = 0; }
    else if (n < sizeof(buf) - 1) buf[n++] = c;
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_TURN_INDICATOR, OUTPUT);
  for (Button* b : allButtons) b->begin();

  if (!radio.begin()) {
    Serial.println("Falha ao iniciar ESP-NOW");
    while (true) delay(1000);
  }
  loadConfig();  // depois do Wi-Fi ligado: esp_random() usa o RF como fonte de entropia
  reqSeq = esp_random();
  radio.addPeer(kBroadcast);
  for (auto& d : doors)
    if (d.paired) radio.addPeer(d.mac);

  Serial.println("\n=== Esp_BCM CENTRAL ===");
  printStatus();
  printHelp();
}

void loop() {
  RxFrame f;
  while (radio.receive(f)) handleFrame(f);
  for (Button* b : allButtons) b->update();

  handlePairing();
  handleWindows();
  handleLocks();
  handleTurnSignals();
  handleMirrors();
  updatePresence();
  processRetries();
  updateLed();
  handleSerial();
  delay(1);
}
