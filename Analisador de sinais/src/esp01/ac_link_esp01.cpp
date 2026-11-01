// Transporte do ESP-01: UART de hardware, atendida no laço principal. Sempre ativo, normalmente em
// paralelo com o módulo original (AC_PARALLEL).
#include "ac_link.h"
#include "ac_protocol.h"
#include "config_esp01.h"

struct Command {
  uint16_t prop;
  uint8_t len;
  uint8_t value[4];
};

static AcState acState;
static AcReader reader;
static LinkStats linkStats;
static uint16_t counter = AC_REQUEST_FLAG;

static Command queue[AC_QUEUE_LEN];
static uint8_t queueHead = 0, queueCount = 0;

static Command pending;
static bool pendingBusy = false, pendingAcked = false;
static uint16_t pendingCounter;
static uint8_t pendingTries;
static uint32_t pendingAt, nextCommandAt, lastHeartbeat, lastByteAt;

static bool pinTesting = false;
static uint32_t pinTestAt;

static const uint8_t HEARTBEAT_VALUE[] = AC_HEARTBEAT_VALUE;

static uint16_t nextCounter() {
  counter = (counter + 1) | AC_REQUEST_FLAG;
  return counter;
}

static void sendHeartbeat() {
  uint8_t out[AC_MAX_FRAME];
  const size_t n = acBuild(out, sizeof(out), nextCounter(), "CA1", "NIU", AC_PROP_HEARTBEAT,
                           AC_OP_NOTIFY, HEARTBEAT_VALUE, sizeof(HEARTBEAT_VALUE));
  Serial.write(out, n);
  linkStats.heartbeats++;
}

static void sendPending() {
  uint8_t out[AC_MAX_FRAME];
  pendingCounter = nextCounter();
  pendingAcked = false;
  const size_t n = acBuild(out, sizeof(out), pendingCounter, "CA1", "RP1", pending.prop,
                           AC_OP_WRITE, pending.value, pending.len);
  Serial.write(out, n);
  pendingAt = millis();
  pendingTries++;
}

static void serviceCommands(uint32_t now) {
  if (pendingBusy) {
    if (pendingAcked) {
      linkStats.commandsOk++;
      pendingBusy = false;
      nextCommandAt = now + AC_COMMAND_GAP_MS;
    } else if (now - pendingAt >= AC_ACK_TIMEOUT_MS) {
      if (pendingTries < AC_COMMAND_TRIES) {
        sendPending();
      } else {
        linkStats.commandsFailed++;
        pendingBusy = false;
      }
    }
    return;
  }
  if ((int32_t)(now - nextCommandAt) < 0 || !queueCount) return;
  pending = queue[queueHead];
  queueHead = (queueHead + 1) % AC_QUEUE_LEN;
  queueCount--;
  pendingBusy = true;
  pendingTries = 0;
  sendPending();
}

static void handleFrame(const AcFrame& f) {
  linkStats.lastRxMs = millis();
  acApply(acState, f);
  if (f.op == AC_OP_NOTIFY && (f.counter & AC_REQUEST_FLAG)) {
    if (AC_PARALLEL) return;  // duas confirmações colidiriam na linha
    uint8_t out[AC_MAX_FRAME];
    const size_t n = acBuildAck(out, sizeof(out), f);
    Serial.write(out, n);
    linkStats.acksSent++;
  } else if (f.op == AC_OP_WRITE_ACK && f.counter == (pendingCounter & ~AC_REQUEST_FLAG)) {
    pendingAcked = true;
  } else if (f.op == AC_OP_NOTIFY_ACK && f.prop == AC_PROP_HEARTBEAT) {
    linkStats.heartbeatsAcked++;
  }
}

static void servicePinTest(uint32_t now) {
  if (!pinTesting) return;
  const uint32_t elapsed = now - pinTestAt;
  if (elapsed >= AC_PIN_TEST_MS) {
    pinTesting = false;
    pinMode(AC_TX_PIN, SPECIAL);  // devolve o pino à UART
    return;
  }
  digitalWrite(AC_TX_PIN, (elapsed / AC_PIN_TEST_STEP_MS) & 1);
}

namespace AcLink {

void testTxPin() {
  pinMode(AC_TX_PIN, OUTPUT);
  pinTestAt = millis();
  pinTesting = true;
}

void begin() {
  acStateReset(acState);
  Serial.setRxBufferSize(AC_RX_BUFFER);
  Serial.begin(AC_BAUD, SERIAL_8E1);
  if (AC_TX_INVERTED) USC0(UART0) |= BIT(UCTXI);  // begin() inverteria também o RX
  GPF(AC_RX_PIN) &= ~(1 << GPFPU);  // o pull-up interno soma com o resistor série e sobe o nível baixo
  lastHeartbeat = millis() - AC_HEARTBEAT_MS;  // o primeiro sai logo
}

void loop() {
  const uint32_t now = millis();
  if (Serial.available() && now - lastByteAt > AC_FRAME_GAP_MS) reader.reset();
  while (Serial.available()) {
    AcFrame f;
    if (reader.push(Serial.read(), f)) handleFrame(f);
    linkStats.rxBytes++;
    lastByteAt = millis();
  }
  if (Serial.hasRxError()) linkStats.rxErrors++;
  if (!AC_PARALLEL && now - lastHeartbeat >= AC_HEARTBEAT_MS) {
    lastHeartbeat = now;
    sendHeartbeat();
  }
  serviceCommands(now);
  servicePinTest(now);
}

bool enabled() { return true; }

const AcState& state() { return acState; }

LinkStats stats() { return linkStats; }

bool enqueue(uint16_t prop, const uint8_t* value, uint8_t len) {
  if (queueCount == AC_QUEUE_LEN) return false;
  Command& c = queue[(queueHead + queueCount) % AC_QUEUE_LEN];
  c.prop = prop;
  c.len = len;
  memcpy(c.value, value, len);
  queueCount++;
  return true;
}

}  // namespace AcLink
