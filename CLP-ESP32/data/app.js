'use strict';

// ===========================================================================
// Constantes e estado
// ===========================================================================
const CW = 100;   // largura de uma célula da lógica
const CH = 84;    // altura de uma linha de elementos
const WY = 46;    // posição vertical do fio dentro da célula
const RAIL = 14;  // margem do barramento esquerdo
const OUTW = 150; // largura da coluna de saídas
const MIN_COLS = 5;

let COUNTS = { I: 10, Q: 6, AI: 5, M: 128, D: 32, T: 16, C: 16, S: 3 };
const SYS_NAMES = { S0: '1º scan', S1: 'Pisca 1 Hz', S2: 'Sempre ON' };
const TIMERS = ['ton', 'tof', 'tp'];
const COUNTERS = ['ctu', 'ctd'];
const BOXES = [...TIMERS, ...COUNTERS, 'mov'];
const CMP_OPS = ['>', '>=', '<', '<=', '==', '!='];

const online = location.protocol.startsWith('http');
let prog = emptyProgram();
let sel = null;       // seleção atual (ver hits em renderRungSvg)
let dirty = false;
let state = null;     // último estado recebido do CLP
let monitor = true;
let logicCols = MIN_COLS;
const rungHits = [];  // por linha: registros clicáveis do último desenho

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"']/g, c =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

function emptyProgram() { return { version: 1, rungs: [], symbols: {} }; }
function newRung() { return { comment: '', logic: { t: 's', items: [] }, outputs: [] }; }

// ===========================================================================
// Endereços
// ===========================================================================
const ADDR_RE = /^(AI|I|Q|M|S|T|C|D)(\d+)(?:\.(ET|CV))?$/;

function parseAddr(s) {
  const m = ADDR_RE.exec(String(s ?? '').replace(/\s+/g, '').toUpperCase());
  if (!m) return null;
  const area = m[1], idx = parseInt(m[2], 10), suf = m[3] || '';
  if (idx >= COUNTS[area]) return null;
  if ((suf === 'ET' && area !== 'T') || (suf === 'CV' && area !== 'C')) return null;
  return { area, idx, suf, name: area + idx + (suf ? '.' + suf : '') };
}

// Valida um endereço (ou nome simbólico) para um uso específico.
function resolveAddr(text, kind) {
  const s = String(text ?? '').trim();
  if (kind === 'intR' && /^[+-]?\d+$/.test(s)) return { name: String(parseInt(s, 10)), k: true };
  let a = parseAddr(s);
  if (!a) {
    const hit = Object.entries(prog.symbols).find(([, v]) => v.toLowerCase() === s.toLowerCase());
    if (hit) a = parseAddr(hit[0]);
  }
  if (!a) return null;
  const bit = !a.suf;
  const ok = {
    bitR: bit && ['I', 'Q', 'M', 'S', 'T', 'C'].includes(a.area),
    bitW: bit && ['Q', 'M'].includes(a.area),
    rst: bit && ['Q', 'M', 'T', 'C'].includes(a.area),
    intR: ['AI', 'D'].includes(a.area) || !bit,
    intW: a.area === 'D',
    timer: bit && a.area === 'T',
    counter: bit && a.area === 'C',
  }[kind];
  return ok ? a : null;
}

function symOf(addr) {
  const a = parseAddr(addr);
  if (!a) return '';
  const base = a.area + a.idx;
  return prog.symbols[base] || SYS_NAMES[base] || '';
}

function allAddrs() {
  const out = [];
  for (const area of ['I', 'Q', 'AI', 'M', 'D', 'T', 'C', 'S']) {
    for (let i = 0; i < COUNTS[area]; i++) out.push(area + i);
  }
  return out;
}

// ===========================================================================
// Valores ao vivo
// ===========================================================================
function bitVal(name) {
  const a = parseAddr(name);
  if (!state || !a) return false;
  switch (a.area) {
    case 'I': return !!((state.i >>> a.idx) & 1);
    case 'Q': return !!((state.q >>> a.idx) & 1);
    case 'M': return !!((state.m[a.idx >> 5] >>> (a.idx & 31)) & 1);
    case 'S': return !!((state.s >>> a.idx) & 1);
    case 'T': return !!state.t[a.idx][0];
    case 'C': return !!state.c[a.idx][0];
  }
  return false;
}

function intVal(name) {
  const s = String(name ?? '').trim();
  if (/^[+-]?\d+$/.test(s)) return parseInt(s, 10);
  const a = parseAddr(s);
  if (!state || !a) return 0;
  if (a.area === 'AI') return state.ai[a.idx];
  if (a.area === 'D') return state.d[a.idx];
  if (a.suf === 'ET') return state.t[a.idx][1];
  if (a.suf === 'CV') return state.c[a.idx][1];
  return 0;
}

function cmpEval(n) {
  const x = intVal(n.a), y = intVal(n.b);
  switch (n.op) {
    case '>': return x > y;
    case '>=': return x >= y;
    case '<': return x < y;
    case '<=': return x <= y;
    case '==': return x === y;
    default: return x !== y;
  }
}

function fmtTime(ms) {
  ms = Number(ms) || 0;
  if (ms < 1000) return ms + 'ms';
  if (ms % 3600000 === 0) return ms / 3600000 + 'h';
  if (ms % 60000 === 0) return ms / 60000 + 'min';
  return +(ms / 1000).toFixed(1) + 's';
}

// ===========================================================================
// Layout e desenho do Ladder
// ===========================================================================
function size(n) {
  if (n.t === 's') {
    let w = 0, h = 1;
    for (const it of n.items) { const z = size(it); w += z.w; h = Math.max(h, z.h); }
    return { w: Math.max(w, 1), h };
  }
  if (n.t === 'p') {
    let w = 1, h = 0;
    for (const b of n.branches) { const z = size(b); w = Math.max(w, z.w); h += z.h; }
    return { w, h: Math.max(h, 1) };
  }
  return { w: 1, h: 1 };
}

const trunc = (s, n) => (s.length > n ? s.slice(0, n - 1) + '…' : s);

function wire(ctx, x1, y1, x2, y2, p) {
  ctx.out.push(`<line class="w${p ? ' p' : ''}" x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}"/>`);
}
function text(ctx, x, y, s, cls = '', anchor = 'middle') {
  ctx.out.push(`<text class="${cls}" x="${x}" y="${y}" text-anchor="${anchor}">${esc(s)}</text>`);
}
function addHit(ctx, rec) {
  rec.r = ctx.r;
  ctx.hits.push(rec);
  return ctx.hits.length - 1;
}
function hitRect(ctx, h, x, y, w, hgt) {
  ctx.out.push(`<rect class="hit" data-h="${h}" x="${x}" y="${y}" width="${w}" height="${hgt}"/>`);
}
function selRect(ctx, x, y, w, h) {
  ctx.out.push(`<rect class="selr" x="${x + 3}" y="${y + 3}" width="${w - 6}" height="${h - 6}"/>`);
}

function isSelNode(items, i) {
  if (!sel || sel.kind !== 'node' || sel.items !== items) return false;
  return i >= Math.min(sel.i, sel.j) && i <= Math.max(sel.i, sel.j);
}

// Desenha uma série ocupando a largura W; retorna a energia na saída.
function drawSeries(ctx, s, x, y, W, pin, par) {
  if (!s.items.length) {
    const h = addHit(ctx, { kind: 'empty', series: s, par });
    if (sel && sel.kind === 'empty' && sel.series === s) selRect(ctx, x, y, CW, CH);
    wire(ctx, x, y + WY, x + W, y + WY, pin);
    ctx.out.push(`<rect class="ph" x="${x + CW / 2 - 18}" y="${y + WY - 13}" width="36" height="26"/>`);
    text(ctx, x + CW / 2, y + WY + 4, '+', 'symn');
    hitRect(ctx, h, x, y, CW, CH);
    return pin;
  }
  let cx = x, p = pin;
  s.items.forEach((it, i) => {
    const w = size(it).w * CW;
    p = drawNode(ctx, it, cx, y, w, p, s, i, par);
    cx += w;
  });
  if (cx < x + W) wire(ctx, cx, y + WY, x + W, y + WY, p);
  return p;
}

function drawNode(ctx, n, x, y, W, pin, series, i, par) {
  if (n.t === 's') return drawSeries(ctx, n, x, y, W, pin, par);
  if (n.t === 'p') {
    let by = y, outP = false;
    const ys = [];
    for (const b of n.branches) {
      const bp = drawSeries(ctx, b, x, by, W, pin, n);
      outP = outP || bp;
      ys.push(by + WY);
      by += size(b).h * CH;
    }
    wire(ctx, x, ys[0], x, ys[ys.length - 1], pin);
    wire(ctx, x + W, ys[0], x + W, ys[ys.length - 1], outP);
    return outP;
  }
  return drawLeaf(ctx, n, x, y, pin, series, i, par);
}

function drawLeaf(ctx, n, x, y, pin, series, i, par) {
  const live = ctx.live;
  const cy = y + WY, cx = x + CW / 2;
  let on = false;
  if (live) {
    if (n.t === 'no') on = bitVal(n.v);
    else if (n.t === 'nc') on = !bitVal(n.v);
    else if (n.t === 'cmp') on = cmpEval(n);
    // bordas duram 1 scan: não dá para mostrar com atualização a cada 200 ms
  }
  const pout = pin && on;
  const h = addHit(ctx, { kind: 'node', items: series.items, i, j: i, par });
  if (isSelNode(series.items, i)) selRect(ctx, x, y, CW, CH);

  if (n.t === 'cmp') {
    wire(ctx, x, cy, x + 8, cy, pin);
    wire(ctx, x + CW - 8, cy, x + CW, cy, pout);
    ctx.out.push(`<rect class="box${on ? ' on' : ''}" x="${x + 8}" y="${cy - 17}" width="${CW - 16}" height="34" rx="4"/>`);
    text(ctx, cx, cy - 3, trunc(n.a, 11));
    text(ctx, cx, cy + 12, trunc(`${n.op} ${n.b}`, 11));
    text(ctx, cx, y + 20, trunc(symOf(n.a) || 'Comparação', 15), 'symn');
    if (live) text(ctx, cx, y + CH - 6, `${intVal(n.a)} ${n.op} ${intVal(n.b)}`, 'live');
  } else {
    wire(ctx, x, cy, cx - 10, cy, pin);
    wire(ctx, cx + 10, cy, x + CW, cy, pout);
    let d = `M${cx - 10} ${cy - 14}V${cy + 14}M${cx + 10} ${cy - 14}V${cy + 14}`;
    if (n.t === 'nc') d += `M${cx - 13} ${cy + 12}L${cx + 13} ${cy - 12}`;
    ctx.out.push(`<path class="sym${on ? ' on' : ''}" d="${d}"/>`);
    if (n.t === 're' || n.t === 'fe') text(ctx, cx, cy + 4, n.t === 're' ? 'P' : 'N', 'ttl');
    text(ctx, cx, y + 16, n.v, 'addr');
    text(ctx, cx, y + 29, trunc(symOf(n.v), 15), 'symn');
  }
  hitRect(ctx, h, x, y, CW, CH);
  return pout;
}

function drawOutput(ctx, o, k, xo, y, p) {
  const live = ctx.live;
  const cy = y + WY, xr = xo + OUTW;
  const h = addHit(ctx, { kind: 'out', i: k });
  if (sel && sel.kind === 'out' && sel.r === ctx.r && sel.i === k) selRect(ctx, xo, y, OUTW, CH);

  if (!BOXES.includes(o.t)) {
    const cx = xo + OUTW / 2;
    const on = live && (o.t === 'out' ? bitVal(o.v) : p);
    wire(ctx, xo, cy, cx - 14, cy, p);
    wire(ctx, cx + 14, cy, xr, cy, false);
    const d = `M${cx - 8} ${cy - 14}A16 16 0 0 0 ${cx - 8} ${cy + 14}M${cx + 8} ${cy - 14}A16 16 0 0 1 ${cx + 8} ${cy + 14}`;
    ctx.out.push(`<path class="sym${on ? ' on' : ''}" d="${d}"/>`);
    if (o.t !== 'out') text(ctx, cx, cy + 4, o.t === 'set' ? 'S' : 'R', 'ttl');
    text(ctx, cx, y + 16, o.v, 'addr');
    text(ctx, cx, y + 29, trunc(symOf(o.v), 18), 'symn');
  } else {
    const bx = xo + 16, bw = OUTW - 32;
    let on = false, line2 = '', liveTxt = '';
    if (TIMERS.includes(o.t)) {
      const a = parseAddr(o.v);
      line2 = `${o.v}  ${fmtTime(o.pt)}`;
      if (live && a) { on = !!state.t[a.idx][0]; liveTxt = 'ET ' + fmtTime(state.t[a.idx][1]); }
    } else if (COUNTERS.includes(o.t)) {
      const a = parseAddr(o.v);
      line2 = `${o.v}  PV ${o.pv}`;
      if (live && a) { on = !!state.c[a.idx][0]; liveTxt = 'CV ' + state.c[a.idx][1]; }
    } else {
      line2 = trunc(`${o.a}→${o.v}`, 15);
      if (live) { on = p; liveTxt = `${o.v} = ${intVal(o.v)}`; }
    }
    wire(ctx, xo, cy, bx, cy, p);
    wire(ctx, bx + bw, cy, xr, cy, false);
    ctx.out.push(`<rect class="box${on ? ' on' : ''}" x="${bx}" y="${cy - 20}" width="${bw}" height="40" rx="4"/>`);
    text(ctx, bx + 6, cy - 5, o.t.toUpperCase(), 'ttl', 'start');
    text(ctx, bx + 6, cy + 12, line2, '', 'start');
    text(ctx, xo + OUTW / 2, y + 18, trunc(symOf(o.v), 20), 'symn');
    if (liveTxt) text(ctx, xo + OUTW / 2, y + CH - 4, liveTxt, 'live');
  }
  hitRect(ctx, h, xo, y, OUTW, CH);
}

function renderRungSvg(r) {
  const rung = prog.rungs[r];
  const ctx = { r, out: [], hits: [], live: monitor && !!state };
  const outs = rung.outputs;
  const rows = Math.max(size(rung.logic).h, outs.length, 1);
  const xo = RAIL + logicCols * CW, xr = xo + OUTW;
  const W = xr + RAIL, H = rows * CH;

  ctx.out.push(`<rect class="hit" data-h="-1" x="0" y="0" width="${W}" height="${H}"/>`);
  ctx.out.push(`<line class="rail" x1="${RAIL}" y1="0" x2="${RAIL}" y2="${H}"/>`);
  ctx.out.push(`<line class="rail" x1="${xr}" y1="0" x2="${xr}" y2="${H}"/>`);

  const p = drawSeries(ctx, rung.logic, RAIL, 0, logicCols * CW, ctx.live, null);
  if (outs.length > 1) wire(ctx, xo, WY, xo, (outs.length - 1) * CH + WY, p);
  outs.forEach((o, k) => drawOutput(ctx, o, k, xo, k * CH, p));
  if (!outs.length) {
    wire(ctx, xo, WY, xr, WY, false);
    ctx.out.push(`<rect class="ph" x="${xo + 20}" y="${WY - 13}" width="${OUTW - 40}" height="26"/>`);
    text(ctx, xo + OUTW / 2, WY + 4, 'adicione uma saída', 'symn');
  }
  rungHits[r] = ctx.hits;
  return `<svg width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" data-r="${r}">${ctx.out.join('')}</svg>`;
}

function renderAllSvgs() {
  prog.rungs.forEach((_, r) => {
    const body = $(`#rung-${r} .rung-body`);
    if (body) body.innerHTML = renderRungSvg(r);
    $(`#rung-${r}`)?.classList.toggle('selected', !!sel && sel.r === r);
  });
}

function renderAll() {
  logicCols = Math.max(MIN_COLS, ...prog.rungs.map(r => size(r.logic).w));
  const box = $('#rungs');
  if (!prog.rungs.length) {
    box.innerHTML = `<div class="empty-state">Programa vazio. <button data-act="addRung">+ Adicionar linha</button></div>`;
  } else {
    box.innerHTML = prog.rungs.map((rung, r) => `
      <div class="rung" id="rung-${r}">
        <div class="rung-head">
          <span class="rung-num">${String(r + 1).padStart(3, '0')}</span>
          <input data-comment="${r}" value="${esc(rung.comment)}" placeholder="Comentário da linha">
        </div>
        <div class="rung-body"></div>
      </div>`).join('');
  }
  renderAllSvgs();
  $('#dirty').textContent = dirty ? '● alterações não enviadas' : '';
}

// ===========================================================================
// Edição
// ===========================================================================
const ELEM = {
  no: ['Contato NA', [['v', 'Endereço', 'bitR']]],
  nc: ['Contato NF', [['v', 'Endereço', 'bitR']]],
  re: ['Borda de subida', [['v', 'Endereço', 'bitR']]],
  fe: ['Borda de descida', [['v', 'Endereço', 'bitR']]],
  cmp: ['Comparação', [['a', 'Valor A (AI, D, T.ET, C.CV ou número)', 'intR'], ['op', 'Operador', 'op'], ['b', 'Valor B', 'intR']]],
  out: ['Bobina', [['v', 'Endereço (Q ou M)', 'bitW']]],
  set: ['Set: liga e mantém', [['v', 'Endereço (Q ou M)', 'bitW']]],
  rst: ['Reset', [['v', 'Endereço (Q, M, T ou C)', 'rst']]],
  ton: ['TON: retardo para ligar', [['v', 'Temporizador', 'timer'], ['pt', 'Tempo', 'time']]],
  tof: ['TOF: retardo para desligar', [['v', 'Temporizador', 'timer'], ['pt', 'Tempo', 'time']]],
  tp: ['TP: pulso de duração fixa', [['v', 'Temporizador', 'timer'], ['pt', 'Tempo', 'time']]],
  ctu: ['CTU: contador crescente', [['v', 'Contador', 'counter'], ['pv', 'Preset', 'int']]],
  ctd: ['CTD: contador decrescente', [['v', 'Contador', 'counter'], ['pv', 'Preset', 'int']]],
  mov: ['MOV: copiar valor', [['a', 'Origem (AI, D, T.ET, C.CV ou número)', 'intR'], ['v', 'Destino (D)', 'intW']]],
};

function usedTimers(except) {
  const used = new Set();
  for (const r of prog.rungs) for (const o of r.outputs) {
    if (o !== except && TIMERS.includes(o.t)) used.add(o.v);
  }
  return used;
}

function defaults(t) {
  if (['no', 'nc', 're', 'fe'].includes(t)) return { t, v: 'I0' };
  if (t === 'cmp') return { t, a: 'AI0', op: '>', b: '1500' };
  if (TIMERS.includes(t)) {
    const used = usedTimers();
    let k = 0;
    while (k < COUNTS.T - 1 && used.has('T' + k)) k++;
    return { t, v: 'T' + k, pt: 1000 };
  }
  if (COUNTERS.includes(t)) return { t, v: 'C0', pv: 10 };
  if (t === 'mov') return { t, a: 'AI0', v: 'D0' };
  return { t, v: 'Q0' };
}

function changed() {
  dirty = true;
  renderAll();
}

function targetRung() {
  if (!prog.rungs.length) prog.rungs.push(newRung());
  return sel ? sel.r : prog.rungs.length - 1;
}

function addLeaf(t) {
  const n = defaults(t);
  const r = targetRung();
  let items, idx, par = null;
  if (sel && sel.kind === 'node') {
    items = sel.items; idx = Math.max(sel.i, sel.j) + 1; par = sel.par;
  } else if (sel && sel.kind === 'empty') {
    items = sel.series.items; idx = items.length; par = sel.par;
  } else {
    items = prog.rungs[r].logic.items; idx = items.length;
  }
  items.splice(idx, 0, n);
  sel = { kind: 'node', r, items, i: idx, j: idx, par };
  changed();
  openEditor(n, () => { items.splice(items.indexOf(n), 1); sel = null; changed(); });
}

function addOutput(t) {
  const n = defaults(t);
  const r = targetRung();
  const outs = prog.rungs[r].outputs;
  const idx = sel && sel.kind === 'out' && sel.r === r ? sel.i + 1 : outs.length;
  outs.splice(idx, 0, n);
  sel = { kind: 'out', r, i: idx };
  changed();
  openEditor(n, () => { outs.splice(outs.indexOf(n), 1); sel = { kind: 'rung', r }; changed(); });
}

function wrapParallel() {
  if (!sel || sel.kind !== 'node') return showMsg('Selecione um ou mais contatos para colocar em paralelo.', true);
  const a = Math.min(sel.i, sel.j), b = Math.max(sel.i, sel.j);
  const moved = sel.items.splice(a, b - a + 1);
  const nb = { t: 's', items: [] };
  const p = { t: 'p', branches: [{ t: 's', items: moved }, nb] };
  sel.items.splice(a, 0, p);
  sel = { kind: 'empty', r: sel.r, series: nb, par: p };
  changed();
}

function addBranch() {
  if (sel && (sel.kind === 'node' || sel.kind === 'empty') && sel.par) {
    const nb = { t: 's', items: [] };
    sel.par.branches.push(nb);
    sel = { kind: 'empty', r: sel.r, series: nb, par: sel.par };
    changed();
  } else {
    wrapParallel();
  }
}

// Remove paralelos vazios e achata paralelos com um único ramo.
function normalize(s) {
  for (let i = 0; i < s.items.length; i++) {
    const it = s.items[i];
    if (it.t !== 'p') continue;
    it.branches.forEach(normalize);
    if (it.branches.length === 0) s.items.splice(i--, 1);
    else if (it.branches.length === 1) s.items.splice(i--, 1, ...it.branches[0].items);
  }
}

function deleteSel() {
  if (!sel) return;
  const rung = prog.rungs[sel.r];
  if (sel.kind === 'node') {
    const a = Math.min(sel.i, sel.j), b = Math.max(sel.i, sel.j);
    sel.items.splice(a, b - a + 1);
  } else if (sel.kind === 'empty' && sel.par) {
    sel.par.branches.splice(sel.par.branches.indexOf(sel.series), 1);
  } else if (sel.kind === 'out') {
    rung.outputs.splice(sel.i, 1);
  } else {
    return;
  }
  normalize(rung.logic);
  sel = { kind: 'rung', r: sel.r };
  changed();
}

function selectedNode() {
  if (!sel) return null;
  if (sel.kind === 'node' && sel.i === sel.j) return sel.items[sel.i];
  if (sel.kind === 'out') return prog.rungs[sel.r].outputs[sel.i];
  return null;
}

function rungAction(act) {
  const r = sel ? sel.r : prog.rungs.length - 1;
  if (act === 'addRung') {
    prog.rungs.splice(r + 1, 0, newRung());
    sel = { kind: 'rung', r: r + 1 };
  } else if (r < 0) {
    return;
  } else if (act === 'delRung') {
    if (!confirm(`Excluir a linha ${r + 1}?`)) return;
    prog.rungs.splice(r, 1);
    sel = null;
  } else {
    const to = act === 'upRung' ? r - 1 : r + 1;
    if (to < 0 || to >= prog.rungs.length) return;
    [prog.rungs[r], prog.rungs[to]] = [prog.rungs[to], prog.rungs[r]];
    sel = sel ? { ...sel, r: to } : { kind: 'rung', r: to };
  }
  changed();
}

// ---------- diálogo de edição ----------
let dlgCtx = null;
const TIME_UNITS = [[1, 'ms'], [1000, 's'], [60000, 'min'], [3600000, 'h']];

function fieldHtml(key, label, kind, val) {
  if (kind === 'op') {
    return `<label>${label}<select name="${key}">${CMP_OPS.map(o =>
      `<option${o === val ? ' selected' : ''}>${esc(o)}</option>`).join('')}</select></label>`;
  }
  if (kind === 'time') {
    const ms = Number(val) || 0;
    let unit = 1;
    for (const [u] of TIME_UNITS) if (ms && ms % u === 0) unit = u;
    if (ms >= 1000 && unit === 1) unit = 1000;
    return `<label>${label}<div class="inline">
      <input name="pt_num" type="number" min="0" step="any" value="${ms / unit}" required>
      <select name="pt_unit">${TIME_UNITS.map(([u, n]) =>
        `<option value="${u}"${u === unit ? ' selected' : ''}>${n}</option>`).join('')}</select></div></label>`;
  }
  if (kind === 'int') {
    return `<label>${label}<input name="${key}" type="number" min="0" step="1" value="${esc(val)}" required></label>`;
  }
  return `<label>${label}<input name="${key}" list="addrList" value="${esc(val)}" autocomplete="off" required>
    <small class="muted" data-symfor="${key}">${esc(symOf(val))}</small></label>`;
}

function openEditor(n, onCancel) {
  const spec = ELEM[n.t];
  if (!spec) return;
  const [title, fields] = spec;
  $('#dlgTitle').textContent = title;
  $('#dlgErr').textContent = '';
  $('#dlgFields').innerHTML = fields.map(([k, l, kind]) => fieldHtml(k, l, kind, n[k])).join('');
  const form = $('#dlgForm');
  dlgCtx = {
    cancel: onCancel,
    apply() {
      const upd = {};
      for (const [k, label, kind] of fields) {
        if (kind === 'op') {
          upd.op = form.elements.op.value;
        } else if (kind === 'time') {
          const ms = Math.round(parseFloat(form.elements.pt_num.value) * Number(form.elements.pt_unit.value));
          if (!(ms >= 0) || ms > 2147483647) return 'Tempo inválido.';
          upd.pt = ms;
        } else if (kind === 'int') {
          const v = parseInt(form.elements[k].value, 10);
          if (!(v >= 0)) return `${label}: valor inválido.`;
          upd[k] = v;
        } else {
          const a = resolveAddr(form.elements[k].value, kind);
          if (!a) return `${label}: endereço inválido ou não permitido aqui.`;
          upd[k] = a.name;
        }
      }
      if (TIMERS.includes(n.t) && usedTimers(n).has(upd.v)) return `${upd.v} já é usado por outro temporizador.`;
      Object.assign(n, upd);
      return null;
    },
  };
  $('#dlg').showModal();
  const first = $('#dlgFields input, #dlgFields select');
  if (first) { first.focus(); first.select?.(); }
}

function closeEditor(cancelled) {
  const c = dlgCtx;
  dlgCtx = null;
  $('#dlg').close();
  if (cancelled && c && c.cancel) c.cancel();
}

// ===========================================================================
// Validação, envio e carga
// ===========================================================================
function walkSeries(s, fn) {
  fn(s);
  for (const it of s.items) if (it.t === 'p') it.branches.forEach(b => walkSeries(b, fn));
}

function validate() {
  const errors = [], warnings = [];
  const timers = {}, coils = {};
  prog.rungs.forEach((r, i) => {
    const L = `Linha ${i + 1}`;
    if (!r.outputs.length) errors.push(`${L}: sem saída.`);
    walkSeries(r.logic, s => { if (s !== r.logic && !s.items.length) errors.push(`${L}: ramo paralelo vazio.`); });
    for (const o of r.outputs) {
      if (TIMERS.includes(o.t)) {
        if (timers[o.v]) errors.push(`${L}: ${o.v} já usado na linha ${timers[o.v]}.`);
        else timers[o.v] = i + 1;
      }
      if (o.t === 'out') {
        if (coils[o.v]) warnings.push(`${L}: bobina ${o.v} também aparece na linha ${coils[o.v]} (a última vence).`);
        else coils[o.v] = i + 1;
      }
    }
  });
  return { errors, warnings };
}

function sanitize(p) {
  const out = emptyProgram();
  if (p && typeof p.symbols === 'object' && p.symbols) {
    for (const [k, v] of Object.entries(p.symbols)) if (typeof v === 'string' && v) out.symbols[k.toUpperCase()] = v;
  }
  const fixSeries = s => ({
    t: 's',
    items: (s && Array.isArray(s.items) ? s.items : []).map(it =>
      it && it.t === 'p' ? { t: 'p', branches: (it.branches || []).map(fixSeries) } : it).filter(Boolean),
  });
  for (const r of (p && Array.isArray(p.rungs) ? p.rungs : [])) {
    out.rungs.push({
      comment: String(r.comment || ''),
      logic: fixSeries(r.logic),
      outputs: Array.isArray(r.outputs) ? r.outputs.filter(Boolean) : [],
    });
  }
  return out;
}

let msgTimer = 0;
function showMsg(text, error = false, keep = false) {
  const m = $('#msg');
  m.textContent = text;
  m.className = 'msg ' + (error ? 'error' : 'ok');
  m.hidden = false;
  clearTimeout(msgTimer);
  if (!keep) msgTimer = setTimeout(() => { m.hidden = true; }, 5000);
}

async function downloadProgram() {
  if (!online) return showMsg('Abra esta página pelo endereço do CLP para enviar o programa.', true);
  const { errors, warnings } = validate();
  if (errors.length) return showMsg('Corrija antes de enviar:\n' + errors.join('\n'), true, true);
  try {
    const res = await fetch('/api/program', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(prog),
    });
    const j = await res.json().catch(() => ({}));
    if (!res.ok || !j.ok) return showMsg('O CLP recusou o programa: ' + (j.error || res.status), true, true);
    dirty = false;
    renderAll();
    showMsg('Programa enviado e em execução.' + (warnings.length ? '\nAvisos:\n' + warnings.join('\n') : ''), false, warnings.length > 0);
  } catch (e) {
    showMsg('Falha de comunicação: ' + e.message, true);
  }
}

async function loadProgram() {
  if (!online) { renderAll(); return; }
  try {
    const res = await fetch('/api/program');
    prog = sanitize(await res.json());
    sel = null;
    dirty = false;
    renderAll();
    buildMonitor();
    buildSymbols();
    updateDatalist();
  } catch (e) {
    showMsg('Não foi possível ler o programa do CLP: ' + e.message, true);
  }
}

function exportProgram() {
  const blob = new Blob([JSON.stringify(prog, null, 2)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'programa-clp.json';
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

function importProgram(file) {
  const rd = new FileReader();
  rd.onload = () => {
    try {
      prog = sanitize(JSON.parse(rd.result));
      sel = null;
      changed();
      buildMonitor();
      buildSymbols();
      updateDatalist();
      showMsg('Programa importado. Use "Enviar ao CLP" para gravar.');
    } catch (e) {
      showMsg('Arquivo inválido: ' + e.message, true);
    }
  };
  rd.readAsText(file);
}

// ===========================================================================
// Comunicação em tempo real
// ===========================================================================
let ws = null;
function connect() {
  if (!online) return;
  ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
  ws.onopen = () => setConn(true);
  ws.onclose = () => {
    setConn(false);
    state = null;
    onState();
    setTimeout(connect, 2000);
  };
  ws.onmessage = e => {
    try { state = JSON.parse(e.data); } catch { return; }
    onState();
  };
}
function send(o) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(o)); }

function setConn(on) {
  const c = $('#conn');
  c.textContent = on ? 'conectado' : 'offline';
  c.className = 'chip ' + (on ? 'on' : 'off');
}

function activeTab() { return $('.tabs button.active').dataset.tab; }

function onState() {
  const rs = $('#runState');
  if (state) {
    rs.textContent = state.run ? 'RUN' : 'STOP';
    rs.className = 'chip ' + (state.run ? 'on' : 'stop');
    $('#scanInfo').textContent = `scan ${state.scan} µs (máx ${state.smax} µs)`;
  } else {
    rs.textContent = '—';
    rs.className = 'chip';
    $('#scanInfo').textContent = '';
  }
  const tab = activeTab();
  if (tab === 'ladder' && monitor) renderAllSvgs();
  if (tab === 'monitor') updateMonitor();
}

// ===========================================================================
// Aba Monitor
// ===========================================================================
function ioCell(area, i) {
  const name = area + i;
  return `<div class="io" data-force="${area}" data-i="${i}" id="io-${name}">
    <span class="led"></span>
    <span class="name"><b>${name}</b><small>${esc(prog.symbols[name] || '')}</small></span>
    <span class="fbadge" hidden>F</span></div>`;
}

function buildMonitor() {
  $('#monI').innerHTML = Array.from({ length: COUNTS.I }, (_, i) => ioCell('I', i)).join('');
  $('#monQ').innerHTML = Array.from({ length: COUNTS.Q }, (_, i) => ioCell('Q', i)).join('');
  $('#monAI').innerHTML = Array.from({ length: COUNTS.AI }, (_, i) => `
    <div class="ai"><div class="lbl"><span><b>AI${i}</b> <span class="muted small">${esc(prog.symbols['AI' + i] || '')}</span></span>
    <span id="aiv-${i}">—</span></div><div class="bar"><div id="aib-${i}"></div></div></div>`).join('');
  $('#monD').innerHTML = Array.from({ length: COUNTS.D }, (_, i) => `
    <label>D${i} <span class="muted">${esc(trunc(prog.symbols['D' + i] || '', 14))}</span>
    <input type="number" step="1" data-d="${i}" id="dv-${i}"></label>`).join('');
  $('#monM').innerHTML = Array.from({ length: COUNTS.M }, (_, i) =>
    `<div class="mbit" id="mb-${i}" title="${esc(prog.symbols['M' + i] || '')}">M${i}</div>`).join('');

  const used = { T: new Set(), C: new Set() };
  for (const r of prog.rungs) for (const o of r.outputs) {
    if (TIMERS.includes(o.t)) used.T.add(parseAddr(o.v)?.idx);
    if (COUNTERS.includes(o.t)) used.C.add(parseAddr(o.v)?.idx);
  }
  const list = (area, label) => {
    const ids = [...used[area]].filter(x => x !== undefined).sort((a, b) => a - b);
    if (!ids.length) return `<div class="muted small">Nenhum ${label} no programa.</div>`;
    return ids.map(i => `<div class="tc"><span><b>${area}${i}</b> <span class="muted small">${esc(prog.symbols[area + i] || '')}</span></span>
      <span id="${area.toLowerCase()}v-${i}">—</span></div>`).join('');
  };
  $('#monT').innerHTML = list('T', 'temporizador');
  $('#monC').innerHTML = list('C', 'contador');
  updateMonitor();
}

function updateMonitor() {
  const s = state;
  const setIo = (area, i, on, forced) => {
    const el = $(`#io-${area}${i}`);
    if (!el) return;
    el.querySelector('.led').classList.toggle('on', on);
    el.querySelector('.fbadge').hidden = !forced;
  };
  for (let i = 0; i < COUNTS.I; i++) setIo('I', i, !!s && !!((s.i >>> i) & 1), !!s && !!((s.fie >>> i) & 1));
  for (let i = 0; i < COUNTS.Q; i++) setIo('Q', i, !!s && !!((s.qp >>> i) & 1), !!s && !!((s.fqe >>> i) & 1));
  for (let i = 0; i < COUNTS.AI; i++) {
    const v = s ? s.ai[i] : null;
    $(`#aiv-${i}`).textContent = v === null ? '—' : `${v} mV`;
    $(`#aib-${i}`).style.width = v === null ? '0' : Math.min(100, v / 33) + '%';
  }
  for (let i = 0; i < COUNTS.D; i++) {
    const inp = $(`#dv-${i}`);
    if (inp && document.activeElement !== inp) inp.value = s ? s.d[i] : '';
  }
  for (let i = 0; i < COUNTS.M; i++) {
    $(`#mb-${i}`)?.classList.toggle('on', !!s && !!((s.m[i >> 5] >>> (i & 31)) & 1));
  }
  for (let i = 0; i < COUNTS.T; i++) {
    const el = $(`#tv-${i}`);
    if (el) el.textContent = s ? `${s.t[i][0] ? 'ON' : 'off'} · ${fmtTime(s.t[i][1])} / ${fmtTime(s.t[i][2])}` : '—';
  }
  for (let i = 0; i < COUNTS.C; i++) {
    const el = $(`#cv-${i}`);
    if (el) el.textContent = s ? `${s.c[i][0] ? 'ON' : 'off'} · ${s.c[i][1]} / ${s.c[i][2]}` : '—';
  }
  $('#monSys').innerHTML = s ? `
    <div class="tc"><span>Estado</span><b>${s.run ? 'RUN' : 'STOP'}</b></div>
    <div class="tc"><span>Scan atual / máx.</span><span>${s.scan} / ${s.smax} µs</span></div>
    ${[0, 1, 2].map(i => `<div class="tc"><span>S${i} ${SYS_NAMES['S' + i]}</span><span>${(s.s >>> i) & 1}</span></div>`).join('')}`
    : '<div class="muted">Sem conexão com o CLP.</div>';
}

function cycleForce(area, i) {
  if (!state) return;
  const en = area === 'I' ? state.fie : state.fqe;
  const val = area === 'I' ? state.fiv : state.fqv;
  const forced = (en >>> i) & 1, v = (val >>> i) & 1;
  const m = !forced ? 1 : v ? 0 : -1;  // normal -> 1 -> 0 -> normal
  send({ cmd: 'force', a: area, i, m });
}

// ===========================================================================
// Aba Variáveis
// ===========================================================================
function buildSymbols() {
  const groups = [
    ['I', 'Entradas digitais', true], ['Q', 'Saídas digitais', true], ['AI', 'Entradas analógicas (mV)', true],
    ['M', 'Bits internos', false], ['D', 'Registradores', false], ['T', 'Temporizadores', false], ['C', 'Contadores', false],
  ];
  $('#symTables').innerHTML = groups.map(([area, title, open]) => {
    const rows = Array.from({ length: COUNTS[area] }, (_, i) => area + i);
    const any = rows.some(a => prog.symbols[a]);
    return `<details${open || any ? ' open' : ''}><summary>${title}</summary><table class="sym">${rows.map(a =>
      `<tr><td>${a}</td><td><input data-sym="${a}" value="${esc(prog.symbols[a] || '')}" maxlength="40"></td></tr>`).join('')}
      </table></details>`;
  }).join('');
}

function updateDatalist() {
  $('#addrList').innerHTML = allAddrs().map(a => `<option value="${a}">${esc(symOf(a))}</option>`).join('');
}

// ===========================================================================
// Aba Configuração
// ===========================================================================
async function loadConfig() {
  if (!online) return;
  try {
    const [cfg, info] = await Promise.all([
      fetch('/api/config').then(r => r.json()),
      fetch('/api/info').then(r => r.json()),
    ]);
    const f = $('#cfgForm');
    f.elements.ssid.value = cfg.ssid || '';
    f.elements.pass.value = '';
    f.elements.hostname.value = cfg.hostname || '';
    f.elements.diPullUp.checked = !!cfg.diPullUp;
    $('#invList').innerHTML = Array.from({ length: COUNTS.I }, (_, i) =>
      `<label><input type="checkbox" data-inv="${i}"${(cfg.diInvert >>> i) & 1 ? ' checked' : ''}> I${i}</label>`).join('');
    $('#infoBox').innerHTML = `
      <div class="tc"><span>Firmware</span><span>${esc(info.version)}</span></div>
      <div class="tc"><span>Rede</span><span>${cfg.mode === 'ap' ? 'Ponto de acesso' : 'Wi-Fi'} · ${esc(cfg.ip)}${cfg.mode === 'sta' ? ` · ${cfg.rssi} dBm` : ''}</span></div>
      <div class="tc"><span>Ligado há</span><span>${Math.floor(info.uptime / 3600)}h ${Math.floor(info.uptime / 60) % 60}min</span></div>
      <div class="tc"><span>Memória livre</span><span>${Math.round(info.heap / 1024)} KB</span></div>
      <div class="tc"><span>Programa</span><span>${info.rungs} linhas · ${info.instr} instruções</span></div>`;
  } catch (e) {
    $('#cfgMsg').textContent = 'Falha ao ler a configuração: ' + e.message;
  }
}

async function saveConfig(e) {
  e.preventDefault();
  const f = e.target;
  let inv = 0;
  $$('[data-inv]').forEach(c => { if (c.checked) inv |= 1 << Number(c.dataset.inv); });
  const body = {
    ssid: f.elements.ssid.value.trim(),
    hostname: f.elements.hostname.value.trim(),
    diPullUp: f.elements.diPullUp.checked,
    diInvert: inv >>> 0,
  };
  if (f.elements.pass.value) body.pass = f.elements.pass.value;
  const msg = $('#cfgMsg');
  try {
    const res = await fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    const j = await res.json();
    if (!res.ok || !j.ok) throw new Error(j.error || res.status);
    msg.textContent = j.restart ? 'Salvo. O CLP vai reiniciar. Se mudou a rede, acesse pelo novo endereço.' : 'Salvo.';
  } catch (err) {
    msg.textContent = 'Erro: ' + err.message;
  }
}

// ===========================================================================
// Eventos
// ===========================================================================
function bind() {
  $$('.tabs button').forEach(b => b.addEventListener('click', () => {
    $$('.tabs button').forEach(x => x.classList.toggle('active', x === b));
    $$('.tab').forEach(t => t.classList.toggle('active', t.id === 'tab-' + b.dataset.tab));
    if (b.dataset.tab === 'monitor') buildMonitor();
    if (b.dataset.tab === 'config') loadConfig();
    if (b.dataset.tab === 'ladder') renderAll();
  }));

  $('#btnRun').onclick = () => send({ cmd: 'run' });
  $('#btnStop').onclick = () => send({ cmd: 'stop' });
  $('#btnClrMax').onclick = () => send({ cmd: 'clrmax' });

  document.addEventListener('click', e => {
    const add = e.target.closest('[data-add]');
    if (add) {
      const t = add.dataset.add;
      if (['no', 'nc', 're', 'fe', 'cmp'].includes(t)) addLeaf(t); else addOutput(t);
      return;
    }
    const act = e.target.closest('[data-act]');
    if (act) {
      const a = act.dataset.act;
      if (a === 'wrap') wrapParallel();
      else if (a === 'branch') addBranch();
      else if (a === 'delete') deleteSel();
      else if (a === 'edit') { const n = selectedNode(); if (n) openEditor(n); }
      else rungAction(a);
    }
  });

  const rungs = $('#rungs');
  rungs.addEventListener('click', e => {
    const hit = e.target.closest('.hit');
    const svg = e.target.closest('svg');
    if (!hit || !svg) return;
    const r = Number(svg.dataset.r), h = Number(hit.dataset.h);
    if (h < 0) {
      sel = { kind: 'rung', r };
    } else {
      const rec = rungHits[r][h];
      if (e.shiftKey && sel && sel.kind === 'node' && rec.kind === 'node' && rec.items === sel.items) {
        sel = { ...sel, j: rec.i };
      } else {
        sel = { ...rec };
      }
    }
    renderAllSvgs();
  });
  rungs.addEventListener('dblclick', e => {
    if (!e.target.closest('.hit')) return;
    const n = selectedNode();
    if (n) openEditor(n);
  });
  rungs.addEventListener('change', e => {
    const r = e.target.dataset.comment;
    if (r === undefined) return;
    prog.rungs[r].comment = e.target.value;
    dirty = true;
    $('#dirty').textContent = '● alterações não enviadas';
  });

  document.addEventListener('keydown', e => {
    if ($('#dlg').open || e.target.matches('input, select, textarea')) return;
    if (activeTab() !== 'ladder') return;
    if (e.key === 'Delete' || e.key === 'Backspace') { e.preventDefault(); deleteSel(); }
    else if (e.key === 'Enter') { const n = selectedNode(); if (n) openEditor(n); }
    else if (e.key === 'Escape') { sel = null; renderAllSvgs(); }
  });

  $('#dlgForm').addEventListener('submit', e => {
    e.preventDefault();
    if (!dlgCtx) return;
    const err = dlgCtx.apply();
    if (err) { $('#dlgErr').textContent = err; return; }
    closeEditor(false);
    changed();
  });
  $('#dlgCancel').onclick = () => closeEditor(true);
  $('#dlg').addEventListener('cancel', e => { e.preventDefault(); closeEditor(true); });
  $('#dlgFields').addEventListener('input', e => {
    const k = e.target.name;
    const small = $(`[data-symfor="${k}"]`);
    if (small) small.textContent = symOf(resolveAddr(e.target.value, 'intR')?.name || resolveAddr(e.target.value, 'bitR')?.name || '');
  });

  $('#btnDownload').onclick = downloadProgram;
  $('#btnReload').onclick = () => {
    if (dirty && !confirm('Descartar as alterações não enviadas?')) return;
    loadProgram();
  };
  $('#btnExport').onclick = exportProgram;
  $('#fileImport').onchange = e => { if (e.target.files[0]) importProgram(e.target.files[0]); e.target.value = ''; };
  $('#chkMonitor').onchange = e => { monitor = e.target.checked; renderAllSvgs(); };

  $('#tab-monitor').addEventListener('click', e => {
    const io = e.target.closest('[data-force]');
    if (io) cycleForce(io.dataset.force, Number(io.dataset.i));
  });
  $('#monD').addEventListener('change', e => {
    const i = e.target.dataset.d;
    const v = parseInt(e.target.value, 10);
    if (i !== undefined && Number.isFinite(v)) send({ cmd: 'setd', i: Number(i), v });
  });

  $('#symTables').addEventListener('change', e => {
    const a = e.target.dataset.sym;
    if (!a) return;
    const v = e.target.value.trim();
    if (v) prog.symbols[a] = v; else delete prog.symbols[a];
    dirty = true;
    updateDatalist();
    renderAll();
  });

  $('#cfgForm').addEventListener('submit', saveConfig);
  window.addEventListener('beforeunload', e => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });
}

async function init() {
  bind();
  if (online) {
    try {
      const info = await fetch('/api/info').then(r => r.json());
      if (info.counts) COUNTS = { ...COUNTS, ...info.counts };
    } catch { /* usa os valores padrão */ }
  }
  updateDatalist();
  buildSymbols();
  buildMonitor();
  await loadProgram();
  connect();
}

init();
