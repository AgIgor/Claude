// ============================================================================
//  Esp_BCM - MODULO DE PORTA
//  Recebe comandos da central, executa e responde com ACK.
//  O mesmo firmware serve nas 4 portas; a posicao vem dos jumpers CODE0/CODE1.
//  Portas traseiras: vidro + trava.  Dianteiras: + seta, espelho e rebatimento.
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
#include <Motor.h>
#include "config.h"

using namespace bcm;

static Link radio;
static Preferences prefs;
static Button btnPair(PIN_PAIR);

static Motor window(PIN_WINDOW[0], PIN_WINDOW[1], PIN_WINDOW_SENSE, WINDOW_STALL_ADC);
static Motor lockMotor(PIN_LOCK[0], PIN_LOCK[1]);
static Motor mirV(PIN_MIR_V[0], PIN_MIR_V[1]);
static Motor mirH(PIN_MIR_H[0], PIN_MIR_H[1]);
static Motor fold(PIN_FOLD[0], PIN_FOLD[1], PIN_FOLD_SENSE, FOLD_STALL_ADC);

static uint8_t myRole = ROLE_FL;
static bool front = true;

static bool paired = false;
static uint8_t centralMac[6];
static uint32_t netId = 0;
static uint8_t netKey[kKeyLen];
static uint32_t lastCounter = 0, savedCounter = 0, txCounter = 0;
static uint32_t lastRx = 0;

static bool pairing = false;
static uint32_t pairingUntil = 0;  // 0 = sem prazo (modulo ainda nao pareado)
static uint32_t lastRequest = 0;
static uint8_t requestedMac[6];

static bool locked = false, folded = false, lampOn = false;
static uint32_t lampAt = 0;

// Para enviar RESULT quando uma acao termina sozinha.
struct ActionRef { uint16_t reqId; uint8_t arg; };
static ActionRef windowRef, lockRef, foldRef;

// ---------------------------------------------------------------------------
static uint8_t readRole() {
  pinMode(PIN_CODE0, INPUT_PULLUP);
  pinMode(PIN_CODE1, INPUT_PULLUP);
  delay(5);
  uint8_t code = (digitalRead(PIN_CODE1) == LOW) << 1 | (digitalRead(PIN_CODE0) == LOW);
  return code;  // 0=DE 1=DD 2=TE 3=TD (mesma ordem de bcm::Role)
}

static void loadConfig() {
  prefs.begin("bcmdoor", false);
  locked = prefs.getBool("locked", false);
  folded = prefs.getBool("folded", false);
  if (!prefs.isKey("cmac") || !prefs.isKey("key")) return;

  prefs.getBytes("cmac", centralMac, 6);
  prefs.getBytes("key", netKey, kKeyLen);
  netId = prefs.getUInt("netid", 0);
  lastCounter = savedCounter = prefs.getUInt("ctr", 0);
  paired = prefs.getUChar("role", 0xFF) == myRole;
  if (!paired)
    Serial.println("! Posicao (jumpers) diferente da usada no pareamento - pareie novamente");
}

static void saveCounter() {
  savedCounter = lastCounter;
  prefs.putUInt("ctr", savedCounter);
}

static uint8_t stateBits() {
  uint8_t s = 0;
  if (locked) s |= STATE_LOCKED;
  if (folded) s |= STATE_FOLDED;
  if (window.running()) s |= STATE_WINDOW_MOVING;
  if (lampOn) s |= STATE_LAMP_ON;
  if (mirV.running() || mirH.running()) s |= STATE_MIRROR_MOVING;
  return s;
}

static void sendToCentral(uint8_t type, uint16_t reqId, uint8_t cmd, uint8_t arg, uint8_t status) {
  if (!paired) return;
  Packet p = {};
  p.type = type;
  p.netId = netId;
  p.counter = ++txCounter;
  p.reqId = reqId;
  p.role = myRole;
  p.cmd = cmd;
  p.arg = arg;
  p.status = status;
  p.state = stateBits();
  radio.send(centralMac, p, netKey);
}

static void setLamp(bool on) {
  lampOn = on;
  lampAt = millis();
  digitalWrite(PIN_LAMP, on);
}

// ---------------------------------------------------------------------------
// Pareamento
// ---------------------------------------------------------------------------
static void startPairing(bool indefinite) {
  pairing = true;
  pairingUntil = indefinite ? 0 : millis() + PAIR_WINDOW_MS;
  Serial.printf(">> [%s] procurando central em modo pareamento...\n", roleName(myRole));
}

static void forgetCentral() {
  if (paired) radio.removePeer(centralMac);
  paired = false;
  prefs.remove("cmac");
  prefs.remove("key");
  prefs.remove("netid");
  prefs.remove("role");
  prefs.remove("ctr");
  Serial.println(">> Central esquecida");
  startPairing(true);
}

static void onBeacon(const RxFrame& f) {
  uint32_t now = millis();
  if (!pairing || now - lastRequest < 300) return;
  lastRequest = now;
  memcpy(requestedMac, f.mac, 6);
  radio.addPeer(f.mac);
  Packet p = {};
  p.type = MSG_PAIR_REQUEST;
  p.netId = f.pkt.netId;
  p.role = myRole;
  radio.send(f.mac, p, nullptr);
}

static void onAccept(const RxFrame& f) {
  if (!pairing || f.pkt.role != myRole || !macEqual(f.mac, requestedMac)) return;
  if (paired && !macEqual(centralMac, f.mac)) radio.removePeer(centralMac);

  memcpy(centralMac, f.mac, 6);
  netId = f.pkt.netId;
  memcpy(netKey, f.pkt.payload, kKeyLen);
  lastCounter = f.pkt.counter;
  prefs.putBytes("cmac", centralMac, 6);
  prefs.putBytes("key", netKey, kKeyLen);
  prefs.putUInt("netid", netId);
  prefs.putUChar("role", myRole);
  saveCounter();

  paired = true;
  pairing = false;
  lastRx = millis();
  radio.addPeer(centralMac);
  sendToCentral(MSG_PAIR_DONE, 0, 0, 0, ST_OK);
  Serial.printf(">> PAREADO como %s com a central %s\n", roleName(myRole),
                macToString(centralMac).c_str());
}

// ---------------------------------------------------------------------------
// Execucao de comandos
// ---------------------------------------------------------------------------
static uint8_t execute(const Packet& p) {
  switch (p.cmd) {
    case CMD_PING:
      return ST_OK;

    case CMD_WINDOW:
      if (p.arg == MOVE_STOP) { window.stop(); return ST_OK; }
      if (p.arg != MOVE_UP && p.arg != MOVE_DOWN) return ST_BAD_ARG;
      windowRef = {p.reqId, p.arg};
      return window.runHold(p.arg == MOVE_UP ? 1 : -1, WINDOW_MAX_MS, kHoldTimeoutMs)
                 ? ST_OK : ST_ALREADY;

    case CMD_LOCK:
      if (p.arg > LOCK_LOCK) return ST_BAD_ARG;
      // Sempre pulsa o atuador (garante sincronismo mesmo se o estado salvo divergir).
      lockMotor.runTimed(p.arg == LOCK_LOCK ? 1 : -1, LOCK_PULSE_MS);
      lockRef = {p.reqId, p.arg};
      if (locked != (p.arg == LOCK_LOCK)) {
        locked = p.arg == LOCK_LOCK;
        prefs.putBool("locked", locked);
      }
      return ST_OK;

    case CMD_LAMP:
      if (!front) return ST_UNSUPPORTED;
      setLamp(p.arg != 0);
      return ST_OK;

    case CMD_MIRROR: {
      if (!front) return ST_UNSUPPORTED;
      bool ok = true;
      switch (p.arg) {
        case MOVE_STOP: mirV.stop(); mirH.stop(); break;
        case MOVE_UP:    mirH.stop(); ok = mirV.runHold(+1, MIRROR_MAX_MS, kHoldTimeoutMs); break;
        case MOVE_DOWN:  mirH.stop(); ok = mirV.runHold(-1, MIRROR_MAX_MS, kHoldTimeoutMs); break;
        case MOVE_RIGHT: mirV.stop(); ok = mirH.runHold(+1, MIRROR_MAX_MS, kHoldTimeoutMs); break;
        case MOVE_LEFT:  mirV.stop(); ok = mirH.runHold(-1, MIRROR_MAX_MS, kHoldTimeoutMs); break;
        default: return ST_BAD_ARG;
      }
      return ok ? ST_OK : ST_ALREADY;
    }

    case CMD_FOLD: {
      if (!front) return ST_UNSUPPORTED;
      if (p.arg > FOLD_FOLD) return ST_BAD_ARG;
      bool want = p.arg == FOLD_FOLD;
      if (want == folded) return ST_ALREADY;
      fold.runTimed(want ? 1 : -1, FOLD_MS);
      foldRef = {p.reqId, p.arg};
      folded = want;
      prefs.putBool("folded", folded);
      return ST_OK;
    }

    case CMD_SLEEP:
      saveCounter();  // sem isto, o reboot volta a um contador antigo e reabre o replay
      return ST_OK;

    default:
      return ST_UNSUPPORTED;
  }
}

static void onCommand(const RxFrame& f) {
  const Packet& p = f.pkt;
  if (!paired || !macEqual(f.mac, centralMac) || p.netId != netId || p.role != myRole) return;
  if (!Link::verify(p, netKey)) return;
  if (p.counter <= lastCounter) return;  // quadro repetido/reenviado por terceiros (replay)
  lastCounter = p.counter;
  if (lastCounter - savedCounter >= 500) saveCounter();
  lastRx = millis();

  uint8_t st = execute(p);
  bool quiet = p.cmd == CMD_PING || (p.flags & FLAG_NO_ACK);
  if (!quiet) Serial.printf("<< %s arg=%u -> %s\n", cmdName(p.cmd), p.arg, statusName(st));
  if (!(p.flags & FLAG_NO_ACK)) sendToCentral(MSG_ACK, p.reqId, p.cmd, p.arg, st);
}

static void handleFrame(const RxFrame& f) {
  switch (f.pkt.type) {
    case MSG_PAIR_BEACON: onBeacon(f); break;
    case MSG_PAIR_ACCEPT: onAccept(f); break;
    case MSG_COMMAND: onCommand(f); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Supervisao dos atuadores
// ---------------------------------------------------------------------------
static void report(Motor::End e, uint8_t cmd, const ActionRef& ref, bool stallIsNormal = false) {
  uint8_t st = e == Motor::END_MAX_TIME ? ST_TIMEOUT
             : e == Motor::END_STALL    ? (stallIsNormal ? ST_DONE : ST_STALL)
                                        : ST_DONE;
  Serial.printf("-- %s terminou: %s\n", cmdName(cmd), statusName(st));
  sendToCentral(MSG_RESULT, ref.reqId, cmd, ref.arg, st);
}

static void serviceActuators() {
  Motor::End e = window.update();
  if (e == Motor::END_MAX_TIME || e == Motor::END_STALL) report(e, CMD_WINDOW, windowRef);
  else if (e == Motor::END_REFRESH_TIMEOUT) Serial.println("-- vidro parado: comando parou de chegar");

  if ((e = lockMotor.update()) != Motor::END_NONE) report(e, CMD_LOCK, lockRef);

  if (!front) return;
  mirV.update();
  mirH.update();
  if ((e = fold.update()) != Motor::END_NONE) report(e, CMD_FOLD, foldRef, true);
  if (lampOn && millis() - lampAt > kLampTimeoutMs) setLamp(false);  // central sumiu
}

static void handlePairButton() {
  if (!btnPair.released()) return;
  uint32_t h = btnPair.heldOnReleaseMs();
  if (h >= RESET_HOLD_MS) forgetCentral();
  else if (h >= PAIR_HOLD_MS) startPairing(false);
  else
    Serial.printf("[%s] %s | %s | %s\n", roleName(myRole), paired ? "pareado" : "NAO pareado",
                  locked ? "travada" : "destravada",
                  front ? (folded ? "espelho rebatido" : "espelho aberto") : "traseira");
}

static void updateLed() {
  uint32_t now = millis();
  uint32_t held = btnPair.heldMs();
  bool on;
  if (held >= RESET_HOLD_MS) on = true;
  else if (held >= PAIR_HOLD_MS) on = (now / 50) % 2;
  else if (pairing) on = (now / 100) % 2;
  else if (now - lastRx < LINK_LOST_MS) on = true;  // pareado e falando com a central
  else on = (now / 500) % 2;                         // pareado, mas sem sinal
  digitalWrite(PIN_LED, on);
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  myRole = readRole();
  front = isFront(myRole);

  pinMode(PIN_LED, OUTPUT);
  btnPair.begin();
  window.begin();
  lockMotor.begin();
  if (front) {
    mirV.begin();
    mirH.begin();
    fold.begin();
    pinMode(PIN_LAMP, OUTPUT);
    setLamp(false);
  }

  if (!radio.begin()) {
    Serial.println("Falha ao iniciar ESP-NOW");
    while (true) delay(1000);
  }
  txCounter = esp_random();
  loadConfig();

  Serial.printf("\n=== Esp_BCM PORTA %s (%s) ===\n", roleName(myRole),
                front ? "dianteira: vidro, trava, seta, espelho" : "traseira: vidro, trava");
  if (paired) {
    radio.addPeer(centralMac);
    Serial.printf("Pareada com a central %s\n", macToString(centralMac).c_str());
  } else {
    startPairing(true);
  }
}

void loop() {
  RxFrame f;
  while (radio.receive(f)) handleFrame(f);
  btnPair.update();

  handlePairButton();
  if (pairing && pairingUntil && (int32_t)(millis() - pairingUntil) >= 0) {
    pairing = false;
    Serial.println(">> Tempo de pareamento esgotado");
  }
  serviceActuators();
  updateLed();
  delay(1);
}
