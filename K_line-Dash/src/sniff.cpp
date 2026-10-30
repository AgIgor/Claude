#include "sniff.h"
#include "obd.h"
#include "log.h"

namespace {

struct Entry {
  uint32_t id;           // com CAN_EFF_FLAG se for ID de 29 bits
  uint8_t dlc;
  uint8_t d[8], mn[8], mx[8];
  uint8_t everChg;       // bytes que já mudaram alguma vez (bit por byte)
  uint32_t chgAt[8];     // último instante em que cada byte mudou
  uint32_t n, nPrev;
  float hz;
};

struct Sig {
  char key[8];
  uint32_t id;
  uint8_t pos, len;
  bool le, sgn;
  float mul, add;
  uint32_t mask;
};

const int MAX_IDS = 96;
const int MAX_SIGS = 16;

Entry tab[MAX_IDS];
int nTab = 0;
Sig sigs[MAX_SIGS];
int nSigs = 0;
String sigText;
uint32_t frames = 0, framesPrev = 0, lastTick = 0, overflows = 0, dropped = 0;
float totalFps = 0;
SemaphoreHandle_t mx;
void (*sink)(const char*, float) = nullptr;

// A tabela fica ordenada por ID; busca binária.
int findOrInsert(uint32_t id) {
  int lo = 0, hi = nTab;
  while (lo < hi) {
    int m = (lo + hi) / 2;
    if (tab[m].id < id) lo = m + 1; else hi = m;
  }
  if (lo < nTab && tab[lo].id == id) return lo;
  if (nTab >= MAX_IDS) return -1;
  memmove(&tab[lo + 1], &tab[lo], (nTab - lo) * sizeof(Entry));
  nTab++;
  Entry& e = tab[lo];
  memset(&e, 0, sizeof(e));
  e.id = id;
  return lo;
}

float decode(const Sig& s, const uint8_t* d) {
  uint32_t raw = 0;
  for (int i = 0; i < s.len; i++) {
    uint8_t b = s.le ? d[s.pos + s.len - 1 - i] : d[s.pos + i];
    raw = (raw << 8) | b;
  }
  raw &= s.mask;
  float v;
  if (s.sgn) {
    // extensão de sinal a partir do bit mais alto da máscara
    int top = 31 - __builtin_clz(s.mask ? s.mask : 1);
    if (top < 31 && (raw & (1UL << top))) raw |= ~((1UL << (top + 1)) - 1);
    v = (float)(int32_t)raw;
  } else {
    v = (float)raw;
  }
  return v * s.mul + s.add;
}

void hex8(String& out, const uint8_t* d, int n) {
  static const char H[] = "0123456789ABCDEF";
  for (int i = 0; i < n; i++) { out += H[d[i] >> 4]; out += H[d[i] & 15]; }
}

bool knownKey(const String& k) {
  for (size_t i = 0; i < NPIDS; i++)
    if (k == PIDS[i].key && PIDS[i].key[0] != '_') return true;
  return false;
}

}  // namespace

namespace Sniff {

void begin(void (*valueSink)(const char*, float)) {
  mx = xSemaphoreCreateMutex();
  sink = valueSink;
}

void reset() {
  xSemaphoreTake(mx, portMAX_DELAY);
  nTab = 0;
  frames = framesPrev = 0;
  overflows = dropped = 0;
  totalFps = 0;
  xSemaphoreGive(mx);
}

void onFrame(const can_frame& f) {
  if (f.can_id & CAN_RTR_FLAG) return;
  uint32_t id = f.can_id & ((f.can_id & CAN_EFF_FLAG) ? (CAN_EFF_MASK | CAN_EFF_FLAG) : CAN_SFF_MASK);
  uint8_t dlc = min<uint8_t>(f.can_dlc, 8);
  uint32_t now = millis();
  float out[MAX_SIGS];
  const char* keys[MAX_SIGS];
  int nOut = 0;

  xSemaphoreTake(mx, portMAX_DELAY);
  frames++;
  int i = findOrInsert(id);
  if (i < 0) {
    dropped++;
  } else {
    Entry& e = tab[i];
    if (e.n == 0) {
      memcpy(e.mn, f.data, dlc);
      memcpy(e.mx, f.data, dlc);
    } else {
      for (int b = 0; b < dlc; b++) {
        if (f.data[b] != e.d[b]) { e.everChg |= 1 << b; e.chgAt[b] = now; }
        if (f.data[b] < e.mn[b]) e.mn[b] = f.data[b];
        if (f.data[b] > e.mx[b]) e.mx[b] = f.data[b];
      }
    }
    memcpy(e.d, f.data, dlc);
    e.dlc = dlc;
    e.n++;
  }
  for (int s = 0; s < nSigs; s++) {
    if (sigs[s].id != id || sigs[s].pos + sigs[s].len > dlc) continue;
    keys[nOut] = sigs[s].key;
    out[nOut++] = decode(sigs[s], f.data);
  }
  xSemaphoreGive(mx);

  if (sink) for (int k = 0; k < nOut; k++) sink(keys[k], out[k]);
}

void tick() {
  xSemaphoreTake(mx, portMAX_DELAY);
  uint32_t now = millis();
  float dt = lastTick ? (now - lastTick) / 1000.0f : 1.0f;
  if (dt <= 0) dt = 1;
  for (int i = 0; i < nTab; i++) {
    tab[i].hz = (tab[i].n - tab[i].nPrev) / dt;
    tab[i].nPrev = tab[i].n;
  }
  totalFps = (frames - framesPrev) / dt;
  framesPrev = frames;
  lastTick = now;
  xSemaphoreGive(mx);
}

void addOverflows(uint32_t n) {
  if (!n) return;
  xSemaphoreTake(mx, portMAX_DELAY);
  overflows += n;
  xSemaphoreGive(mx);
}

uint16_t idCount() { return nTab; }
float fps() { return totalFps; }

// {"fps":..,"frames":..,"ovr":..,"drop":..,"ids":[[id,hz,dlc,"dados","mín","máx",mudouAgora,jáMudou],...]}
String json() {
  String s;
  xSemaphoreTake(mx, portMAX_DELAY);
  s.reserve(120 + nTab * 80);
  uint32_t now = millis();
  s += "{\"fps\":"; s += String(totalFps, 0);
  s += ",\"frames\":"; s += frames;
  s += ",\"ovr\":"; s += overflows;
  s += ",\"drop\":"; s += dropped;
  s += ",\"ids\":[";
  char idb[12];
  for (int i = 0; i < nTab; i++) {
    const Entry& e = tab[i];
    uint8_t recent = 0;
    for (int b = 0; b < e.dlc; b++)
      if ((e.everChg >> b & 1) && now - e.chgAt[b] < 1500) recent |= 1 << b;
    if (e.id & CAN_EFF_FLAG) snprintf(idb, sizeof(idb), "%08lX", (unsigned long)(e.id & CAN_EFF_MASK));
    else snprintf(idb, sizeof(idb), "%03lX", (unsigned long)e.id);
    if (i) s += ',';
    s += "[\""; s += idb; s += "\",";
    s += String(e.hz, 1); s += ','; s += e.dlc; s += ",\"";
    hex8(s, e.d, e.dlc); s += "\",\"";
    hex8(s, e.mn, e.dlc); s += "\",\"";
    hex8(s, e.mx, e.dlc); s += "\",";
    s += recent; s += ','; s += e.everChg; s += ']';
  }
  xSemaphoreGive(mx);
  return s + "]}";
}

bool setSignals(const String& text, String& err) {
  Sig tmp[MAX_SIGS];
  int n = 0, lineNo = 0;
  int start = 0;
  String clean;
  while (start <= (int)text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String line = text.substring(start, end);
    start = end + 1;
    lineNo++;
    int hash = line.indexOf('#');
    String body = hash >= 0 ? line.substring(0, hash) : line;
    body.trim();
    line.trim();
    if (line.length()) { clean += line; clean += '\n'; }
    if (!body.length()) continue;

    String f[8];
    int nf = 0, p = 0;
    while (nf < 8) {
      int c = body.indexOf(',', p);
      f[nf] = c < 0 ? body.substring(p) : body.substring(p, c);
      f[nf].trim();
      nf++;
      if (c < 0) break;
      p = c + 1;
    }
    char where[24];
    snprintf(where, sizeof(where), "linha %d: ", lineNo);
    if (nf < 7) { err = String(where) + "precisa de 7 campos (chave,ID,byte,tamanho,ordem,mult,soma)"; return false; }
    if (n >= MAX_SIGS) { err = "máximo de " + String(MAX_SIGS) + " sinais"; return false; }
    Sig& s = tmp[n];
    f[0].toLowerCase();
    if (!knownKey(f[0])) { err = String(where) + "chave desconhecida \"" + f[0] + "\""; return false; }
    strlcpy(s.key, f[0].c_str(), sizeof(s.key));
    char* e;
    s.id = strtoul(f[1].c_str(), &e, 16);
    if (*e || !f[1].length() || s.id > 0x1FFFFFFF) { err = String(where) + "ID inválido (hexadecimal, ex. 201)"; return false; }
    if (s.id > 0x7FF) s.id |= CAN_EFF_FLAG;
    int pos = f[2].toInt(), len = f[3].toInt();
    if (pos < 0 || pos > 7 || len < 1 || len > 4 || pos + len > 8) { err = String(where) + "byte 0-7 e tamanho 1-4 (sem passar do byte 7)"; return false; }
    s.pos = pos;
    s.len = len;
    f[4].toUpperCase();
    if (f[4] != "B" && f[4] != "L" && f[4] != "BS" && f[4] != "LS") { err = String(where) + "ordem deve ser B, L, BS ou LS"; return false; }
    s.le = f[4][0] == 'L';
    s.sgn = f[4].length() > 1;
    s.mul = f[5].toFloat();
    s.add = f[6].toFloat();
    if (s.mul == 0) { err = String(where) + "multiplicador não pode ser 0"; return false; }
    uint32_t full = len == 4 ? 0xFFFFFFFFUL : ((1UL << (8 * len)) - 1);
    s.mask = full;
    if (nf >= 8 && f[7].length()) {
      s.mask = strtoul(f[7].c_str(), &e, 16) & full;
      if (*e || !s.mask) { err = String(where) + "máscara inválida (hexadecimal, ex. 0FFF)"; return false; }
    }
    n++;
  }
  xSemaphoreTake(mx, portMAX_DELAY);
  memcpy(sigs, tmp, sizeof(Sig) * n);
  nSigs = n;
  sigText = clean;
  xSemaphoreGive(mx);
  return true;
}

String signals() {
  xSemaphoreTake(mx, portMAX_DELAY);
  String s = sigText;
  xSemaphoreGive(mx);
  return s;
}

}  // namespace Sniff
