#include "plc.h"
#include <ArduinoJson.h>
#include "io_hal.h"
#include "settings.h"

Plc g_plc;

// ===========================================================================
// Compilador: JSON Ladder -> bytecode
//
// Formato do programa:
// { "rungs": [ { "comment": "...",
//                "logic":   {"t":"s","items":[ <nó>, ... ]},
//                "outputs": [ <saída>, ... ] } ],
//   "symbols": { "I0": "Interruptor sala", ... } }      (ignorado aqui)
//
// Nó:     {"t":"no","v":"I0"} contato NA     {"t":"nc","v":"I0"} contato NF
//         {"t":"re","v":"I0"} borda subida   {"t":"fe","v":"I0"} borda descida
//         {"t":"cmp","op":">","a":"AI0","b":"1500"}
//         {"t":"s","items":[...]} série      {"t":"p","branches":[<série>,...]} paralelo
// Saída:  {"t":"out"|"set"|"rst","v":"Q0"}
//         {"t":"ton"|"tof"|"tp","v":"T0","pt":5000}
//         {"t":"ctu"|"ctd","v":"C0","pv":10}
//         {"t":"mov","a":"AI0","v":"D0"}
// ===========================================================================

namespace {

enum RefKind { RK_BIT_R, RK_BIT_W, RK_RST, RK_INT_R, RK_INT_W, RK_TIMER, RK_COUNTER };

bool parseNumber(const char* s, int32_t& v) {
  char* end = nullptr;
  long x = strtol(s, &end, 10);
  if (end == s || *end != '\0') return false;
  v = (int32_t)x;
  return true;
}

bool parseRef(const char* src, RefKind kind, Ref& r) {
  if (!src) return false;
  char buf[16];
  size_t n = 0;
  for (const char* p = src; *p && n < sizeof(buf) - 1; p++) {
    if (*p != ' ') buf[n++] = (char)toupper((unsigned char)*p);
  }
  buf[n] = '\0';
  if (n == 0) return false;

  if (isdigit((unsigned char)buf[0]) || buf[0] == '-' || buf[0] == '+') {
    if (kind != RK_INT_R) return false;
    r.area = Area::K;
    r.idx = 0;
    return parseNumber(buf, r.k);
  }

  const char* p = buf;
  Area area;
  uint16_t max;
  if (strncmp(p, "AI", 2) == 0) { area = Area::AI; max = PLC_NUM_AI; p += 2; }
  else if (*p == 'I') { area = Area::I; max = PLC_NUM_DI; p++; }
  else if (*p == 'Q') { area = Area::Q; max = PLC_NUM_DO; p++; }
  else if (*p == 'M') { area = Area::M; max = PLC_NUM_M; p++; }
  else if (*p == 'S') { area = Area::S; max = PLC_NUM_S; p++; }
  else if (*p == 'T') { area = Area::T; max = PLC_NUM_T; p++; }
  else if (*p == 'C') { area = Area::C; max = PLC_NUM_C; p++; }
  else if (*p == 'D') { area = Area::D; max = PLC_NUM_D; p++; }
  else return false;

  if (!isdigit((unsigned char)*p)) return false;
  int idx = 0;
  while (isdigit((unsigned char)*p)) {
    idx = idx * 10 + (*p - '0');
    if (idx > 9999) return false;
    p++;
  }
  if (idx >= max) return false;

  if (*p == '.') {
    if (area == Area::T && strcmp(p, ".ET") == 0) area = Area::TET;
    else if (area == Area::C && strcmp(p, ".CV") == 0) area = Area::CCV;
    else return false;
  } else if (*p != '\0') {
    return false;
  }

  bool ok = false;
  switch (kind) {
    case RK_BIT_R: ok = area == Area::I || area == Area::Q || area == Area::M ||
                        area == Area::S || area == Area::T || area == Area::C; break;
    case RK_BIT_W: ok = area == Area::Q || area == Area::M; break;
    case RK_RST: ok = area == Area::Q || area == Area::M || area == Area::T || area == Area::C; break;
    case RK_INT_R: ok = area == Area::AI || area == Area::D || area == Area::TET || area == Area::CCV; break;
    case RK_INT_W: ok = area == Area::D; break;
    case RK_TIMER: ok = area == Area::T; break;
    case RK_COUNTER: ok = area == Area::C; break;
  }
  if (!ok) return false;
  r.area = area;
  r.idx = (uint16_t)idx;
  r.k = 0;
  return true;
}

struct Compiler {
  Instr* out;
  uint16_t n = 0;
  uint16_t edges = 0;
  int sp = 0;
  int rung = 0;
  uint32_t timersUsed = 0;
  String err;

  bool fail(const String& msg) {
    if (err.isEmpty()) err = "Linha " + String(rung + 1) + ": " + msg;
    return false;
  }

  bool emit(uint8_t op, const Ref& a = Ref(), const Ref& b = Ref(), uint8_t cmp = 0, uint16_t slot = 0) {
    if (n >= PLC_MAX_INSTR) return fail("programa grande demais");
    Instr& i = out[n++];
    i.op = op;
    i.cmp = cmp;
    i.slot = slot;
    i.a = a;
    i.b = b;
    return true;
  }

  bool push() {
    if (++sp > PLC_STACK_DEPTH) return fail("aninhamento de paralelos profundo demais");
    return true;
  }

  bool allocEdge(uint16_t& slot) {
    if (edges >= PLC_MAX_EDGES) return fail("contatos de borda demais");
    slot = edges++;
    return true;
  }

  bool ref(JsonVariantConst v, RefKind kind, Ref& r, const char* what) {
    const char* s = v.as<const char*>();
    String tmp;
    if (!s && v.is<long>()) { tmp = String(v.as<long>()); s = tmp.c_str(); }
    if (!parseRef(s, kind, r)) return fail(String("endereço inválido em ") + what + ": '" + (s ? s : "") + "'");
    return true;
  }

  // Cada nó empilha exatamente um valor booleano.
  bool node(JsonObjectConst nd, int depth) {
    if (depth > 12) return fail("aninhamento profundo demais");
    const char* t = nd["t"] | "";
    Ref a, b;
    if (!strcmp(t, "s")) return series(nd, depth);
    if (!strcmp(t, "p")) {
      JsonArrayConst br = nd["branches"].as<JsonArrayConst>();
      if (br.isNull() || br.size() == 0) return fail("paralelo sem ramos");
      if (!emit(OP_PUSH0) || !push()) return false;
      for (JsonObjectConst s : br) {
        if (!series(s, depth + 1)) return false;
        if (!emit(OP_OR)) return false;
        sp--;
      }
      return true;
    }
    if (!strcmp(t, "no") || !strcmp(t, "nc")) {
      if (!ref(nd["v"], RK_BIT_R, a, "contato")) return false;
      return emit(t[1] == 'o' ? OP_LD : OP_LDN, a) && push();
    }
    if (!strcmp(t, "re") || !strcmp(t, "fe")) {
      uint16_t slot;
      if (!ref(nd["v"], RK_BIT_R, a, "borda") || !allocEdge(slot)) return false;
      return emit(t[0] == 'r' ? OP_RE : OP_FE, a, Ref(), 0, slot) && push();
    }
    if (!strcmp(t, "cmp")) {
      static const char* ops[] = {">", ">=", "<", "<=", "==", "!="};
      const char* op = nd["op"] | "";
      int c = -1;
      for (int k = 0; k < 6; k++) if (!strcmp(op, ops[k])) c = k;
      if (c < 0) return fail(String("comparador inválido: '") + op + "'");
      if (!ref(nd["a"], RK_INT_R, a, "comparação") || !ref(nd["b"], RK_INT_R, b, "comparação")) return false;
      return emit(OP_CMP, a, b, (uint8_t)c) && push();
    }
    return fail(String("elemento desconhecido: '") + t + "'");
  }

  bool series(JsonObjectConst s, int depth) {
    if (!emit(OP_PUSH1) || !push()) return false;
    JsonArrayConst items = s["items"].as<JsonArrayConst>();
    for (JsonObjectConst it : items) {
      if (!node(it, depth + 1)) return false;
      if (!emit(OP_AND)) return false;
      sp--;
    }
    return true;
  }

  bool output(JsonObjectConst o) {
    const char* t = o["t"] | "";
    Ref a, b;
    if (!strcmp(t, "out") || !strcmp(t, "set")) {
      if (!ref(o["v"], RK_BIT_W, a, "bobina")) return false;
      return emit(t[0] == 'o' ? OP_OUT : OP_SET, a);
    }
    if (!strcmp(t, "rst")) {
      if (!ref(o["v"], RK_RST, a, "reset")) return false;
      return emit(OP_RST, a);
    }
    if (!strcmp(t, "ton") || !strcmp(t, "tof") || !strcmp(t, "tp")) {
      if (!ref(o["v"], RK_TIMER, a, "temporizador")) return false;
      if (timersUsed & (1UL << a.idx)) return fail("T" + String(a.idx) + " usado em mais de um bloco");
      timersUsed |= 1UL << a.idx;
      b.k = o["pt"] | -1;
      if (b.k < 0) return fail("tempo (pt) inválido em T" + String(a.idx));
      uint8_t op = !strcmp(t, "ton") ? OP_TON : !strcmp(t, "tof") ? OP_TOF : OP_TP;
      return emit(op, a, b);
    }
    if (!strcmp(t, "ctu") || !strcmp(t, "ctd")) {
      uint16_t slot;
      if (!ref(o["v"], RK_COUNTER, a, "contador") || !allocEdge(slot)) return false;
      b.k = o["pv"] | -1;
      if (b.k < 0) return fail("preset (pv) inválido em C" + String(a.idx));
      return emit(t[2] == 'u' ? OP_CTU : OP_CTD, a, b, 0, slot);
    }
    if (!strcmp(t, "mov")) {
      if (!ref(o["a"], RK_INT_R, b, "MOV origem") || !ref(o["v"], RK_INT_W, a, "MOV destino")) return false;
      return emit(OP_MOV, a, b);
    }
    return fail(String("saída desconhecida: '") + t + "'");
  }
};

}  // namespace

bool plcCompile(const char* json, size_t len, Instr* out, uint16_t& outLen,
                uint16_t& rungCount, String& err) {
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, json, len);
  if (de) {
    err = String("JSON inválido: ") + de.c_str();
    return false;
  }
  JsonArrayConst rungs = doc["rungs"].as<JsonArrayConst>();
  if (rungs.isNull()) {
    err = "campo 'rungs' ausente";
    return false;
  }

  Compiler c;
  c.out = out;
  for (JsonObjectConst r : rungs) {
    JsonArrayConst outs = r["outputs"].as<JsonArrayConst>();
    if (outs.isNull() || outs.size() == 0) {
      c.fail("linha sem saída");
      break;
    }
    if (!c.series(r["logic"].as<JsonObjectConst>(), 0)) break;
    bool ok = true;
    for (JsonObjectConst o : outs) {
      if (!(ok = c.output(o))) break;
    }
    if (!ok || !c.emit(OP_POP)) break;
    c.sp--;
    c.rung++;
  }
  if (!c.err.isEmpty()) {
    err = c.err;
    return false;
  }
  outLen = c.n;
  rungCount = (uint16_t)c.rung;
  return true;
}

// ===========================================================================
// Execução
// ===========================================================================

void Plc::begin() {
  mutex_ = xSemaphoreCreateMutex();
  retainLoad(img_.d, PLC_NUM_D);
  lastScanMs_ = millis();
}

bool Plc::loadProgram(const char* json, size_t len, String& err) {
  Instr* buf = (Instr*)malloc(sizeof(Instr) * PLC_MAX_INSTR);
  if (!buf) {
    err = "sem memória para compilar";
    return false;
  }
  uint16_t n = 0, rungs = 0;
  if (!plcCompile(json, len, buf, n, rungs, err)) {
    free(buf);
    return false;
  }
  Instr* code = (Instr*)realloc(buf, sizeof(Instr) * (n ? n : 1));
  if (!code) code = buf;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  Instr* old = code_;
  code_ = code;
  codeLen_ = n;
  rungCount_ = rungs;
  resetVolatile();
  xSemaphoreGive(mutex_);
  free(old);
  return true;
}

void Plc::resetVolatile() {
  img_.q = 0;
  memset(img_.m, 0, sizeof(img_.m));
  memset(img_.t, 0, sizeof(img_.t));
  memset(img_.c, 0, sizeof(img_.c));
  memset(edges_, 0, sizeof(edges_));
  firstScan_ = true;
}

void Plc::setRunning(bool run) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (run && !running_) resetVolatile();
  running_ = run;
  xSemaphoreGive(mutex_);
}

void Plc::setForce(char area, uint8_t idx, int8_t mode) {
  uint32_t bit = 1UL << idx;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  uint32_t& en = area == 'I' ? fIEn_ : fQEn_;
  uint32_t& val = area == 'I' ? fIVal_ : fQVal_;
  if (mode < 0) {
    en &= ~bit;
  } else {
    en |= bit;
    if (mode) val |= bit; else val &= ~bit;
  }
  xSemaphoreGive(mutex_);
}

bool Plc::setD(uint8_t idx, int32_t value) {
  if (idx >= PLC_NUM_D) return false;
  int32_t copy[PLC_NUM_D];
  xSemaphoreTake(mutex_, portMAX_DELAY);
  img_.d[idx] = value;
  memcpy(copy, img_.d, sizeof(copy));
  xSemaphoreGive(mutex_);
  retainSave(copy, PLC_NUM_D);  // fora do mutex: gravar na flash é lento
  return true;
}

void Plc::snapshot(PlcSnapshot& o) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  o.img = img_;
  o.qPhys = qPhys_;
  o.forceIEn = fIEn_;
  o.forceIVal = fIVal_;
  o.forceQEn = fQEn_;
  o.forceQVal = fQVal_;
  o.running = running_;
  o.scanUs = scanUs_;
  o.scanMaxUs = scanMaxUs_;
  o.scanCount = scanCount_;
  o.instrCount = codeLen_;
  o.rungCount = rungCount_;
  xSemaphoreGive(mutex_);
}

void Plc::resetScanMax() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  scanMaxUs_ = 0;
  xSemaphoreGive(mutex_);
}

bool Plc::readBit(const Ref& r) const {
  switch (r.area) {
    case Area::I: return (img_.i >> r.idx) & 1;
    case Area::Q: return (img_.q >> r.idx) & 1;
    case Area::M: return (img_.m[r.idx >> 5] >> (r.idx & 31)) & 1;
    case Area::S: return (img_.s >> r.idx) & 1;
    case Area::T: return img_.t[r.idx].q;
    case Area::C: return img_.c[r.idx].q;
    default: return false;
  }
}

int32_t Plc::readInt(const Ref& r) const {
  switch (r.area) {
    case Area::AI: return img_.ai[r.idx];
    case Area::D: return img_.d[r.idx];
    case Area::TET: return img_.t[r.idx].et;
    case Area::CCV: return img_.c[r.idx].cv;
    case Area::K: return r.k;
    default: return 0;
  }
}

void Plc::writeBit(const Ref& r, bool v) {
  uint32_t* w;
  uint32_t bit;
  if (r.area == Area::Q) { w = &img_.q; bit = 1UL << r.idx; }
  else if (r.area == Area::M) { w = &img_.m[r.idx >> 5]; bit = 1UL << (r.idx & 31); }
  else return;
  if (v) *w |= bit; else *w &= ~bit;
}

void Plc::exec(uint32_t dt) {
  bool st[PLC_STACK_DEPTH];
  int sp = -1;  // índice do topo; o compilador garante o balanceamento
  for (uint16_t pc = 0; pc < codeLen_; pc++) {
    const Instr& in = code_[pc];
    switch (in.op) {
      case OP_PUSH0: st[++sp] = false; break;
      case OP_PUSH1: st[++sp] = true; break;
      case OP_AND: sp--; st[sp] = st[sp] && st[sp + 1]; break;
      case OP_OR: sp--; st[sp] = st[sp] || st[sp + 1]; break;
      case OP_POP: sp--; break;
      case OP_LD: st[++sp] = readBit(in.a); break;
      case OP_LDN: st[++sp] = !readBit(in.a); break;
      case OP_RE:
      case OP_FE: {
        bool cur = readBit(in.a);
        bool prev = edges_[in.slot];
        edges_[in.slot] = cur;
        st[++sp] = in.op == OP_RE ? (cur && !prev) : (!cur && prev);
        break;
      }
      case OP_CMP: {
        int32_t x = readInt(in.a), y = readInt(in.b);
        bool v;
        switch (in.cmp) {
          case CMP_GT: v = x > y; break;
          case CMP_GE: v = x >= y; break;
          case CMP_LT: v = x < y; break;
          case CMP_LE: v = x <= y; break;
          case CMP_EQ: v = x == y; break;
          default: v = x != y; break;
        }
        st[++sp] = v;
        break;
      }
      case OP_OUT: writeBit(in.a, st[sp]); break;
      case OP_SET: if (st[sp]) writeBit(in.a, true); break;
      case OP_RST:
        if (st[sp]) {
          if (in.a.area == Area::T) {
            PlcTimer& t = img_.t[in.a.idx];
            t.et = 0;
            t.q = false;
          } else if (in.a.area == Area::C) {
            PlcCounter& c = img_.c[in.a.idx];
            c.cv = 0;
            c.q = c.pv <= 0;
          } else {
            writeBit(in.a, false);
          }
        }
        break;
      case OP_TON: {
        PlcTimer& t = img_.t[in.a.idx];
        t.pt = in.b.k;
        if (st[sp]) {
          if (t.et < t.pt) t.et += dt;
          if (t.et >= t.pt) { t.et = t.pt; t.q = true; }
        } else {
          t.et = 0;
          t.q = false;
        }
        t.in = st[sp];
        break;
      }
      case OP_TOF: {
        PlcTimer& t = img_.t[in.a.idx];
        t.pt = in.b.k;
        if (st[sp]) {
          t.q = true;
          t.et = 0;
        } else if (t.q) {
          t.et += dt;
          if (t.et >= t.pt) { t.et = t.pt; t.q = false; }
        }
        t.in = st[sp];
        break;
      }
      case OP_TP: {
        PlcTimer& t = img_.t[in.a.idx];
        t.pt = in.b.k;
        if (t.q) {
          t.et += dt;
          if (t.et >= t.pt) { t.et = t.pt; t.q = false; }
        } else if (st[sp] && !t.in) {
          t.q = true;
          t.et = 0;
        } else if (!st[sp]) {
          t.et = 0;
        }
        t.in = st[sp];
        break;
      }
      case OP_CTU:
      case OP_CTD: {
        PlcCounter& c = img_.c[in.a.idx];
        c.pv = in.b.k;
        bool cur = st[sp];
        bool rise = cur && !edges_[in.slot];
        edges_[in.slot] = cur;
        if (rise) {
          if (in.op == OP_CTU && c.cv < INT32_MAX) c.cv++;
          if (in.op == OP_CTD && c.cv > 0) c.cv--;
        }
        c.q = c.cv >= c.pv;
        break;
      }
      case OP_MOV: if (st[sp]) img_.d[in.a.idx] = readInt(in.b); break;
    }
  }
}

void Plc::scan() {
  uint32_t t0 = micros();
  uint32_t raw = ioReadDI() ^ g_settings.diInvert;
  int32_t ai[PLC_NUM_AI];
  ioReadAI(ai);

  xSemaphoreTake(mutex_, portMAX_DELAY);
  uint32_t now = millis();
  uint32_t dt = now - lastScanMs_;
  lastScanMs_ = now;

  img_.i = ((raw & ~fIEn_) | (fIVal_ & fIEn_)) & ((1UL << PLC_NUM_DI) - 1);
  for (uint8_t k = 0; k < PLC_NUM_AI; k++) {
    if (!aiInit_) aiFilt_[k] = ai[k] << PLC_AI_FILTER_SHIFT;
    aiFilt_[k] += ai[k] - (aiFilt_[k] >> PLC_AI_FILTER_SHIFT);
    img_.ai[k] = aiFilt_[k] >> PLC_AI_FILTER_SHIFT;
  }
  aiInit_ = true;
  img_.s = (firstScan_ ? 1u : 0u) | (((now / 500) & 1) << 1) | (1u << 2);

  if (running_ && code_) {
    exec(dt);
    firstScan_ = false;
  }
  uint32_t q = running_ ? img_.q : 0;  // em STOP as saídas desligam (exceto forçadas)
  qPhys_ = ((q & ~fQEn_) | (fQVal_ & fQEn_)) & ((1UL << PLC_NUM_DO) - 1);
  ioWriteDO(qPhys_);

  scanUs_ = micros() - t0;
  if (scanUs_ > scanMaxUs_) scanMaxUs_ = scanUs_;
  scanCount_++;
  xSemaphoreGive(mutex_);
}
