"use strict";
// Interface do BCM Lights: conversa com o ESP32 (ou com sim_server.py) por WebSocket.

const $ = (id) => document.getElementById(id);

const MODE_LABEL = {
  OFF: "Desligado",
  LEAVING_HOME: "Leaving Home (boas-vindas)",
  COMING_HOME: "Coming Home (acompanhamento)",
  NORMAL: "Normal (ignição ligada)",
};
const MODE_LEVEL = { OFF: 0, LEAVING_HOME: 1, COMING_HOME: 2, NORMAL: 3 };

// [chave, rótulo, passo]
const CONFIG_GROUPS = [
  ["Coming Home", [
    ["coming_home_auto", "Automático (AUTO + escuro)"],
    ["coming_home_time_s", "Duração (s)", 1],
    ["coming_home_flash_window_s", "Janela p/ lampejo (s)", 0.5],
    ["coming_home_start_on_door_close", "Conta só com portas fechadas"],
    ["coming_home_restart_on_lock", "Travar reinicia o tempo"],
  ]],
  ["Leaving Home", [
    ["leaving_home_enabled", "Habilitado"],
    ["leaving_home_time_s", "Duração (s)", 1],
  ]],
  ["Travas e piscas", [
    ["auto_relock_time_s", "Re-travamento automático (s)", 1],
    ["lock_flashes", "Piscadas ao travar", 1],
    ["unlock_flashes", "Piscadas ao destravar", 1],
    ["indicator_period_s", "Período do pisca (s)", 0.05],
  ]],
  ["Luz interna", [
    ["interior_delay_s", "Retardo após fechar porta (s)", 1],
    ["interior_unlock_time_s", "Acesa ao destravar (s)", 1],
    ["interior_fade_in_s", "Fade ao acender (s)", 0.1],
    ["interior_fade_s", "Fade ao apagar (s)", 0.1],
  ]],
  ["Faróis e lanternas", [
    ["exterior_fade_in_s", "Fade ao acender (s)", 0.1],
    ["exterior_fade_out_s", "Fade ao apagar (s)", 0.1],
  ]],
  ["Sensor de luz", [
    ["dark_threshold_lux", "Limiar escuro (lux)", 1],
    ["light_threshold_lux", "Limiar claro (lux)", 1],
    ["dark_debounce_s", "Filtro de tempo (s)", 0.5],
  ]],
  ["Bateria", [
    ["battery_saver_time_s", "Proteção de bateria (s)", 10],
    ["low_battery_v", "Tensão mínima (V)", 0.1],
  ]],
];

const OUTPUT_LANES = [
  ["Farol baixo", "low", "#e0a800", false], ["Lanterna", "park", "#e5484d", false],
  ["Luz de chão", "puddle", "#3b82f6", false], ["Luz interna", "int", "#f97316", false],
  ["Piscas", "ind", "#f5b400", true],
];
const INPUT_LANES = [
  ["Ignição", "ign", "#22a559", true], ["Travado", "locked", "#9b6bd6", true],
  ["Porta aberta", "door", "#a26b4d", true], ["Escuro", "dark", "#5b7da8", true],
];

let ws = null;
let lastMsgAt = 0;
let state = null;
let config = null;
let logNeedsClear = false;
const userTouched = {};  // controle -> instante da última interação (evita "brigar" com o usuário)

// ===================================================================== conexão
function connect() {
  const proto = location.protocol === "https:" ? "wss://" : "ws://";
  ws = new WebSocket(proto + location.host + "/ws");
  ws.onopen = () => { setConn(true); logNeedsClear = true; lastMsgAt = Date.now(); };
  ws.onclose = () => { setConn(false); setTimeout(connect, 1500); };
  ws.onerror = () => ws.close();
  ws.onmessage = (ev) => {
    lastMsgAt = Date.now();
    const m = JSON.parse(ev.data);
    if (m.type === "state") onState(m);
    else if (m.type === "config") onConfig(m);
    else if (m.type === "logs") onLogs(m.items);
  };
}
// Sem mensagens por 3 s = conexão morta (ex.: saiu do alcance do Wi-Fi).
setInterval(() => {
  if (ws && ws.readyState === 1 && Date.now() - lastMsgAt > 3000) ws.close();
}, 1000);

function setConn(on) {
  $("conn").classList.toggle("on", on);
  $("connText").textContent = on ? "conectado" : "reconectando…";
}

function send(obj) {
  if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj));
}

function touched(key) { userTouched[key] = Date.now(); }
function recentlyTouched(key, ms = 1200) { return Date.now() - (userTouched[key] || 0) < ms; }

function throttle(fn, ms) {
  let last = 0, timer = null, pending = null;
  return (...args) => {
    pending = args;
    const now = Date.now();
    if (now - last >= ms) {
      last = now; fn(...pending); pending = null;
    } else if (!timer) {
      timer = setTimeout(() => { timer = null; last = Date.now(); if (pending) fn(...pending); pending = null; }, ms - (now - last));
    }
  };
}

// ===================================================================== controles
function setupControls() {
  document.querySelectorAll(".tab").forEach((tab) => tab.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((t) => t.classList.toggle("active", t === tab));
    document.querySelectorAll(".tabpane").forEach((p) => { p.hidden = p.id !== "tab-" + tab.dataset.tab; });
  }));

  segmented("ignSeg", (v) => { touched("ign"); send({ cmd: "input", ign: Number(v) }); });
  segmented("swSeg", (v) => { touched("sw"); send({ cmd: "input", sw: v }); });
  segmented("luxSrcSeg", (v) => { touched("luxsrc"); send({ cmd: "io", lux_src: v }); });

  $("btnLock").onclick = () => send({ cmd: "lock" });
  $("btnUnlock").onclick = () => send({ cmd: "unlock" });
  $("btnFlash").onclick = () => send({ cmd: "flash" });

  $("doors").querySelectorAll("button").forEach((b) => b.addEventListener("click", () => {
    const i = Number(b.dataset.i);
    const open = !(state && state.web.doors[i]);
    b.classList.toggle("on", open);
    touched("door" + i);
    send({ cmd: "input", door: [i, open] });
  }));

  const sendLux = throttle((lux) => send({ cmd: "input", lux }), 120);
  $("lux").addEventListener("input", (e) => {
    touched("lux");
    const lux = Math.pow(10, Number(e.target.value));
    $("luxVal").textContent = fmtLux(lux);
    sendLux(round3(lux));
  });
  $("luxPresets").querySelectorAll("button").forEach((b) => b.addEventListener("click", () => {
    touched("lux");
    const lux = Number(b.dataset.lux);
    $("lux").value = Math.log10(lux);
    send({ cmd: "input", lux });
  }));

  const sendBatt = throttle((batt) => send({ cmd: "input", batt }), 120);
  $("batt").addEventListener("input", (e) => {
    touched("batt");
    $("battVal").textContent = Number(e.target.value).toFixed(1) + " V";
    sendBatt(Number(e.target.value));
  });

  $("speed").addEventListener("change", (e) => { touched("speed"); send({ cmd: "sim", speed: Number(e.target.value) }); });
  $("btnPause").onclick = () => send({ cmd: "sim", paused: !(state && state.sim.paused) });
  $("btnReset").onclick = () => send({ cmd: "reset" });
  $("window").addEventListener("change", requestDraw);

  $("physInputs").addEventListener("change", (e) => { touched("phys"); send({ cmd: "io", phys: e.target.checked }); });
  $("btnDefaults").onclick = () => {
    if (confirm("Restaurar todos os parâmetros para o padrão?")) send({ cmd: "defaults" });
  };
}

function segmented(id, onPick) {
  $(id).querySelectorAll("button").forEach((b) => b.addEventListener("click", () => {
    setSeg(id, b.dataset.v);
    onPick(b.dataset.v);
  }));
}
function setSeg(id, value) {
  $(id).querySelectorAll("button").forEach((b) => b.classList.toggle("on", b.dataset.v === String(value)));
}

// ===================================================================== configuração
function onConfig(m) {
  config = m;
  const form = $("configForm");
  if (!form.childElementCount) buildConfigForm(m.fields);
  for (const f of m.fields) {
    const el = form.querySelector(`[data-key="${f.k}"]`);
    if (!el || el === document.activeElement) continue;
    if (f.t === "bool") el.checked = !!f.v;
    else el.value = f.v;
  }
  if (!recentlyTouched("luxsrc")) setSeg("luxSrcSeg", m.io.lux_src);
  if (!recentlyTouched("phys")) $("physInputs").checked = m.io.phys;
  $("lux").disabled = m.io.lux_src === "ldr";
  $("luxPresets").querySelectorAll("button").forEach((b) => { b.disabled = m.io.lux_src === "ldr"; });
  requestDraw();
}

function buildConfigForm(fields) {
  const byKey = Object.fromEntries(fields.map((f) => [f.k, f]));
  const form = $("configForm");
  for (const [title, items] of CONFIG_GROUPS) {
    const group = document.createElement("div");
    group.className = "group";
    group.innerHTML = `<h3>${title}</h3>`;
    for (const [key, label, step] of items) {
      const f = byKey[key];
      if (!f) continue;
      if (f.t === "bool") {
        const row = document.createElement("label");
        row.className = "switch";
        row.innerHTML = `<input type="checkbox" data-key="${key}"><span>${label}</span>`;
        row.querySelector("input").addEventListener("change", (e) =>
          send({ cmd: "config", key, value: e.target.checked ? 1 : 0 }));
        group.appendChild(row);
      } else {
        const row = document.createElement("div");
        row.className = "cfg-row";
        row.innerHTML = `<span>${label}</span><input type="number" data-key="${key}" min="${f.min}" max="${f.max}" step="${step || 0.1}">`;
        row.querySelector("input").addEventListener("change", (e) => {
          const v = Number(String(e.target.value).replace(",", "."));
          if (Number.isFinite(v)) send({ cmd: "config", key, value: v });
        });
        group.appendChild(row);
      }
    }
    form.appendChild(group);
  }
}

function cfgValue(key, fallback) {
  const f = config && config.fields.find((x) => x.k === key);
  return f ? f.v : fallback;
}

// ===================================================================== estado
const HIST_KEYS = ["t", "lux", "mode", "low", "park", "puddle", "int", "ind", "ign", "locked", "door", "dark"];
const hist = Object.fromEntries(HIST_KEYS.map((k) => [k, []]));
const HIST_SECONDS = 320;

function onState(s) {
  if (state && s.t < state.t - 0.001) HIST_KEYS.forEach((k) => { hist[k].length = 0; });  // reiniciou
  state = s;

  hist.t.push(s.t);
  hist.lux.push(s.in.lux);
  hist.mode.push(MODE_LEVEL[s.mode] ?? 0);
  hist.low.push(s.out.low);
  hist.park.push(s.out.park);
  hist.puddle.push(s.out.puddle);
  hist.int.push(s.out.int);
  hist.ind.push(s.out.ind ? 1 : 0);
  hist.ign.push(s.in.ign / 2);
  hist.locked.push(s.locked ? 1 : 0);
  hist.door.push(s.in.doors.some(Boolean) ? 1 : 0);
  hist.dark.push(s.dark ? 1 : 0);
  let drop = 0;
  while (drop < hist.t.length && hist.t[drop] < s.t - HIST_SECONDS) drop++;
  if (drop) HIST_KEYS.forEach((k) => hist[k].splice(0, drop));

  updateControls(s);
  updateStatus(s);
  drawCar(s);
  requestDraw();
}

function updateControls(s) {
  if (!recentlyTouched("ign")) setSeg("ignSeg", s.in.ign);
  $("ignPhys").hidden = !(s.phys.ign > s.web.ign && config && config.io.phys);
  if (!recentlyTouched("sw")) setSeg("swSeg", s.web.sw);
  $("doors").querySelectorAll("button").forEach((b) => {
    const i = Number(b.dataset.i);
    if (!recentlyTouched("door" + i)) b.classList.toggle("on", !!s.web.doors[i]);
    b.classList.toggle("phys", !!s.phys.doors[i] && !!(config && config.io.phys));
  });
  if (!recentlyTouched("lux")) $("lux").value = Math.log10(Math.max(1, s.web.lux));
  $("luxVal").textContent = fmtLux(s.in.lux);
  $("ldrInfo").textContent = `Sensor LDR: ${fmtLux(s.phys.lux)}`;
  if (!recentlyTouched("batt")) $("batt").value = s.web.batt;
  $("battVal").textContent = s.in.batt.toFixed(1) + " V";
  if (!recentlyTouched("speed") && document.activeElement !== $("speed")) $("speed").value = String(s.sim.speed);
  $("btnPause").textContent = s.sim.paused ? "Continuar" : "Pausar";
  $("topinfo").textContent = `t = ${s.t.toFixed(1)} s${s.sim.paused ? " (pausado)" : ""} · ` +
    `${s.sys.clients} conectado(s)` + (s.sys.heap ? ` · memória livre ${(s.sys.heap / 1024).toFixed(0)} kB` : "");
}

function updateStatus(s) {
  const courtesy = s.mode === "COMING_HOME" || s.mode === "LEAVING_HOME";
  $("mode").textContent = MODE_LABEL[s.mode] || s.mode;
  $("stTimer").textContent = courtesy ? s.timer.toFixed(1) + " s" : "—";
  $("stRelock").textContent = s.relock > 0 ? s.relock.toFixed(1) + " s" : "—";
  $("stInterior").textContent = s.itimer > 0 ? s.itimer.toFixed(1) + " s" : "—";
  $("stSensor").textContent = `${s.dark ? "ESCURO" : "CLARO"} (${fmtLux(s.in.lux)})`;
  $("stSaver").textContent = s.saver ? "ATUOU" : "ok";
  const pct = (v) => Math.round(v * 100) + "%";
  $("stOut").textContent = `farol ${pct(s.out.low)} · lant. ${pct(s.out.park)} · chão ${pct(s.out.puddle)} · int. ${pct(s.out.int)}`;
}

// ===================================================================== eventos
function onLogs(items) {
  const list = $("log");
  if (logNeedsClear) { list.innerHTML = ""; logNeedsClear = false; }
  const atBottom = list.scrollHeight - list.scrollTop - list.clientHeight < 30;
  for (const [t, msg] of items) {
    const li = document.createElement("li");
    li.innerHTML = `<span>[${t.toFixed(1).padStart(7, " ")} s]</span>`;
    li.appendChild(document.createTextNode(msg));
    list.appendChild(li);
  }
  while (list.childElementCount > 300) list.firstElementChild.remove();
  if (atBottom) list.scrollTop = list.scrollHeight;
}

// ===================================================================== carro
function lerpColor(c1, c2, t) {
  t = Math.max(0, Math.min(1, t));
  const a = [1, 3, 5].map((i) => parseInt(c1.slice(i, i + 2), 16));
  const b = [1, 3, 5].map((i) => parseInt(c2.slice(i, i + 2), 16));
  return "#" + a.map((x, i) => Math.round(x + (b[i] - x) * t).toString(16).padStart(2, "0")).join("");
}

const DOOR_GEOM = [[-1, 105, 122, 178], [1, 215, 122, 178], [-1, 105, 182, 238], [1, 215, 182, 238]];

function drawCar(s) {
  const daylight = Math.max(0, Math.min(1, Math.log10(Math.max(s.in.lux, 1)) / 3.3));
  const bg = lerpColor("#070b16", "#a9bccc", daylight);
  $("sky").setAttribute("fill", bg);
  const beam = s.out.low > 0.01 ? lerpColor(bg, "#fff3b0", s.out.low * 0.55) : bg;
  $("beamL").setAttribute("fill", beam);
  $("beamR").setAttribute("fill", beam);
  const puddle = s.out.puddle > 0.01 ? lerpColor(bg, "#cfe0ff", s.out.puddle * 0.6) : bg;
  $("puddleL").setAttribute("fill", puddle);
  $("puddleR").setAttribute("fill", puddle);
  $("cabin").setAttribute("fill", lerpColor("#1a2230", "#ffd27a", s.out.int));
  const head = lerpColor("#56606a", "#fffbe0", Math.max(s.out.low, s.out.park * 0.35));
  $("headL").setAttribute("fill", head);
  $("headR").setAttribute("fill", head);
  const tail = lerpColor("#4a1c1c", "#ff2a2a", s.out.park);
  $("tailL").setAttribute("fill", tail);
  $("tailR").setAttribute("fill", tail);
  $("indicators").setAttribute("fill", s.out.ind ? "#ffb000" : "#5a4a20");

  const ang = 55 * Math.PI / 180;
  DOOR_GEOM.forEach(([side, hx, ya, yb], i) => {
    const el = $("door" + i);
    const open = !!s.in.doors[i];
    const L = yb - ya;
    el.setAttribute("x2", open ? hx + side * L * Math.sin(ang) : hx);
    el.setAttribute("y2", open ? ya + L * Math.cos(ang) : yb);
    el.classList.toggle("open", open);
  });

  const lock = $("lockText");
  lock.textContent = s.locked ? "TRAVADO" : "DESTRAVADO";
  lock.setAttribute("fill", daylight < 0.5 ? "#ffffff" : "#111111");
}

// ===================================================================== gráfico
let drawPending = false;
function requestDraw() {
  if (drawPending) return;
  drawPending = true;
  requestAnimationFrame(() => { drawPending = false; drawChart(); });
}
window.addEventListener("resize", requestDraw);

function lowerBound(arr, x) {
  let lo = 0, hi = arr.length;
  while (lo < hi) { const mid = (lo + hi) >> 1; if (arr[mid] < x) lo = mid + 1; else hi = mid; }
  return lo;
}

function drawChart() {
  const cv = $("chart");
  const dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  if (!W || !H) return;
  if (cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)) {
    cv.width = Math.round(W * dpr);
    cv.height = Math.round(H * dpr);
  }
  const ctx = cv.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, W, H);

  const css = getComputedStyle(document.documentElement);
  const C = {
    text: css.getPropertyValue("--text").trim(), muted: css.getPropertyValue("--muted").trim(),
    grid: css.getPropertyValue("--grid").trim(), lane: css.getPropertyValue("--lane").trim(),
  };
  const win = Number($("window").value);
  const now = state ? state.t : 0;
  const left = 92, right = 10, top = 4, bottom = 22, titleH = 18;
  const ratios = [1, 0.8, 1.9, 1.5];
  const plotH = H - top - bottom - titleH * ratios.length;
  const sum = ratios.reduce((a, b) => a + b, 0);
  const xOf = (t) => left + ((t - now + win) / win) * (W - left - right);
  const i0 = Math.max(0, lowerBound(hist.t, now - win) - 1);

  // marcas de tempo
  const maxTicks = Math.max(2, Math.floor((W - left - right) / 70));
  const step = [5, 10, 20, 30, 60, 120, 300].find((s) => win / s <= maxTicks) || 300;
  const ticks = [];
  for (let r = 0; r >= -win; r -= step) ticks.push(r);

  let y = top;
  const panels = ratios.map((r) => {
    const p = { y0: y + titleH, h: (r / sum) * plotH };
    y = p.y0 + p.h;
    return p;
  });
  ctx.font = "11px system-ui, sans-serif";
  ctx.textBaseline = "middle";

  const frame = (p, title) => {
    ctx.fillStyle = C.text;
    ctx.font = "600 12px system-ui, sans-serif";
    ctx.textAlign = "left";
    ctx.fillText(title, left, p.y0 - 9);
    ctx.font = "11px system-ui, sans-serif";
    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 1;
    for (const r of ticks) {
      const x = Math.round(xOf(now + r)) + 0.5;
      ctx.beginPath(); ctx.moveTo(x, p.y0); ctx.lineTo(x, p.y0 + p.h); ctx.stroke();
    }
    ctx.strokeRect(left + 0.5, p.y0 + 0.5, W - left - right - 1, p.h - 1);
  };
  const yLabel = (text, yy) => {
    ctx.fillStyle = C.muted;
    ctx.textAlign = "right";
    ctx.fillText(text, left - 6, yy);
  };
  const series = (key, yOf, color, steps, width = 1.8) => {
    const t = hist.t, v = hist[key];
    if (t.length - i0 < 1) return;
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.beginPath();
    let prevY = yOf(v[i0]);
    ctx.moveTo(xOf(t[i0]), prevY);
    for (let i = i0 + 1; i < t.length; i++) {
      const x = xOf(t[i]), yy = yOf(v[i]);
      if (steps) ctx.lineTo(x, prevY);
      ctx.lineTo(x, yy);
      prevY = yy;
    }
    ctx.stroke();
  };
  const clip = (p, fn) => {
    ctx.save();
    ctx.beginPath();
    ctx.rect(left, p.y0, W - left - right, p.h);
    ctx.clip();
    fn();
    ctx.restore();
  };

  // 1) luminosidade (escala log 1..10000)
  const pl = panels[0];
  frame(pl, "Luminosidade (lux)");
  const yLux = (v) => pl.y0 + pl.h - (Math.log10(Math.min(1e4, Math.max(1, v))) / 4) * pl.h;
  for (const v of [1, 10, 100, 1000, 10000]) yLabel(v >= 1000 ? v / 1000 + "k" : String(v), yLux(v));
  clip(pl, () => {
    ctx.setLineDash([5, 4]);
    for (const [key, color] of [["dark_threshold_lux", "#5b7da8"], ["light_threshold_lux", "#e0a800"]]) {
      const yy = yLux(cfgValue(key, key.startsWith("dark") ? 50 : 150));
      ctx.strokeStyle = color; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(left, yy); ctx.lineTo(W - right, yy); ctx.stroke();
    }
    ctx.setLineDash([]);
    series("lux", yLux, C.text, true, 1.5);
  });

  // 2) modo
  const pm = panels[1];
  frame(pm, "Modo do BCM");
  const yMode = (v) => pm.y0 + pm.h - ((v + 0.5) / 4) * pm.h;
  ["OFF", "LEAVING", "COMING", "NORMAL"].forEach((label, i) => yLabel(label, yMode(i)));
  clip(pm, () => series("mode", yMode, "#9b6bd6", true, 2));

  // 3) e 4) faixas estilo analisador lógico
  const lanes = (p, title, defs) => {
    frame(p, title);
    const n = defs.length, laneH = p.h / n;
    defs.forEach(([label, key, color, steps], i) => {
      const base = p.y0 + laneH * (i + 1) - laneH * 0.15;
      const amp = laneH * 0.65;
      yLabel(label, base - amp / 2);
      ctx.strokeStyle = C.lane; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(left, Math.round(base) + 0.5); ctx.lineTo(W - right, Math.round(base) + 0.5); ctx.stroke();
      clip(p, () => series(key, (v) => base - v * amp, color, steps));
    });
  };
  lanes(panels[2], "Saídas (nível PWM)", OUTPUT_LANES);
  lanes(panels[3], "Entradas", INPUT_LANES);

  // eixo de tempo
  ctx.fillStyle = C.muted;
  ctx.textAlign = "center";
  for (const r of ticks) {
    ctx.textAlign = r === 0 ? "right" : "center";
    ctx.fillText(r === 0 ? "agora" : r + " s", xOf(now + r), H - bottom / 2);
  }
}

// ===================================================================== util
function fmtLux(v) {
  if (v >= 1000) return (v / 1000).toFixed(v >= 10000 ? 0 : 1) + "k lux";
  return (v >= 10 ? Math.round(v) : v.toFixed(1)) + " lux";
}
function round3(v) { return Math.round(v * 1000) / 1000; }

setupControls();
connect();
requestDraw();
