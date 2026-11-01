#include "ac_link.h"
#include <freertos/queue.h>
#include "ac_protocol.h"
#include "config.h"

struct Command {
  uint16_t prop;
  uint8_t len;
  uint8_t value[4];
};

static AcState acState;
static volatile bool active = false;
static bool uartOn = false;
static SemaphoreHandle_t txMx;
static QueueHandle_t queue;
static uint16_t counter = AC_REQUEST_FLAG;

static Command pending;
static bool pendingBusy = false;
static uint16_t pendingCounter;
static volatile bool pendingAcked = false;
static uint8_t pendingTries;
static uint32_t pendingAt, nextCommandAt, lastHeartbeat;

static volatile uint32_t acksSent, heartbeats, heartbeatsAcked, commandsOk, commandsFailed, lastRxMs;

static const uint8_t HEARTBEAT_VALUE[] = AC_HEARTBEAT_VALUE;

static void writeFrame(const uint8_t* d, size_t n) {
  xSemaphoreTake(txMx, portMAX_DELAY);
  if (uartOn) Serial1.write(d, n);
  xSemaphoreGive(txMx);
}

static uint16_t nextCounter() {
  counter = (counter + 1) | AC_REQUEST_FLAG;
  return counter;
}

static void sendHeartbeat() {
  uint8_t out[AC_MAX_FRAME];
  size_t n = acBuild(out, sizeof(out), nextCounter(), "CA1", "NIU", AC_PROP_HEARTBEAT, AC_OP_NOTIFY,
                     HEARTBEAT_VALUE, sizeof(HEARTBEAT_VALUE));
  writeFrame(out, n);
  heartbeats = heartbeats + 1;
}

static void sendPending() {
  uint8_t out[AC_MAX_FRAME];
  pendingCounter = nextCounter();
  pendingAcked = false;
  size_t n = acBuild(out, sizeof(out), pendingCounter, "CA1", "RP1", pending.prop, AC_OP_WRITE,
                     pending.value, pending.len);
  writeFrame(out, n);
  pendingAt = millis();
  pendingTries++;
}

static void serviceCommands(uint32_t now) {
  if (pendingBusy) {
    if (pendingAcked) {
      commandsOk = commandsOk + 1;
      pendingBusy = false;
      nextCommandAt = now + AC_COMMAND_GAP_MS;
    } else if (now - pendingAt >= AC_ACK_TIMEOUT_MS) {
      if (pendingTries < AC_COMMAND_TRIES) {
        sendPending();
      } else {
        commandsFailed = commandsFailed + 1;
        pendingBusy = false;
      }
    }
    return;
  }
  if ((int32_t)(now - nextCommandAt) < 0) return;
  if (xQueueReceive(queue, &pending, 0) == pdTRUE) {
    pendingBusy = true;
    pendingTries = 0;
    sendPending();
  }
}

static void linkTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10));
    if (active && !uartOn) {
      xSemaphoreTake(txMx, portMAX_DELAY);
      Serial1.begin(AC_BAUD, SERIAL_8E1, -1, PIN_AC_TX);
      uartOn = true;
      xSemaphoreGive(txMx);
      lastHeartbeat = millis() - AC_HEARTBEAT_MS;  // o primeiro sai logo
    } else if (!active && uartOn) {
      xSemaphoreTake(txMx, portMAX_DELAY);
      Serial1.end();
      pinMode(PIN_AC_TX, INPUT);
      uartOn = false;
      xSemaphoreGive(txMx);
      pendingBusy = false;
      xQueueReset(queue);
    }
    if (!uartOn) continue;
    const uint32_t now = millis();
    if (now - lastHeartbeat >= AC_HEARTBEAT_MS) {
      lastHeartbeat = now;
      sendHeartbeat();
    }
    serviceCommands(now);
  }
}

static void handleFrame(const AcFrame& f) {
  lastRxMs = millis();
  acApply(acState, f);
  if (!active) return;
  if (f.op == AC_OP_NOTIFY && (f.counter & AC_REQUEST_FLAG)) {
    uint8_t out[AC_MAX_FRAME];
    size_t n = acBuildAck(out, sizeof(out), f);
    writeFrame(out, n);
    acksSent = acksSent + 1;
  } else if (f.op == AC_OP_WRITE_ACK && f.counter == (pendingCounter & ~AC_REQUEST_FLAG)) {
    pendingAcked = true;
  } else if (f.op == AC_OP_NOTIFY_ACK && f.prop == AC_PROP_HEARTBEAT) {
    heartbeatsAcked = heartbeatsAcked + 1;
  }
}

namespace AcLink {

void begin(bool on) {
  acStateReset(acState);
  txMx = xSemaphoreCreateMutex();
  queue = xQueueCreate(AC_QUEUE_LEN, sizeof(Command));
  pinMode(PIN_AC_TX, INPUT);
  active = on;
  xTaskCreatePinnedToCore(linkTask, "aclink", 3072, nullptr, 2, nullptr, 1);
}

void setEnabled(bool on) { active = on; }

bool enabled() { return active; }

// A pausa entre quadros pode ser curta demais para separá-los: vários podem vir na mesma rajada.
void onFrames(int ch, const uint8_t* data, size_t n) {
  if (ch != AC_RX_LINE) return;
  while (n) {
    const size_t size = acFrameSize(data, n);
    if (!size) return;
    AcFrame f;
    if (acParse(data, size, f)) handleFrame(f);
    data += size;
    n -= size;
  }
}

const AcState& state() { return acState; }

LinkStats stats() { return {heartbeats, heartbeatsAcked, acksSent, commandsOk, commandsFailed, lastRxMs}; }

bool enqueue(uint16_t prop, const uint8_t* value, uint8_t len) {
  if (!active) return false;
  Command c{prop, len, {}};
  memcpy(c.value, value, len);
  return xQueueSend(queue, &c, 0) == pdTRUE;
}

}  // namespace AcLink
