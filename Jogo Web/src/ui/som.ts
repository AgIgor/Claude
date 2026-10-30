// Som de motor sintetizado (WebAudio). Simples, mas dá vida à puxada.

let ctx: AudioContext | null = null;
let osc1: OscillatorNode | null = null;
let osc2: OscillatorNode | null = null;
let filtro: BiquadFilterNode | null = null;
let ganho: GainNode | null = null;
let ligado = true;

export function somLigado(): boolean {
  return ligado;
}

export function alternarSom(): boolean {
  ligado = !ligado;
  if (!ligado) pararMotor();
  return ligado;
}

function garantirContexto(): AudioContext | null {
  if (!ligado) return null;
  try {
    if (!ctx) ctx = new AudioContext();
    if (ctx.state === 'suspended') void ctx.resume();
    return ctx;
  } catch {
    return null;
  }
}

export function ligarMotor(): void {
  const c = garantirContexto();
  if (!c || osc1) return;
  ganho = c.createGain();
  ganho.gain.value = 0;
  filtro = c.createBiquadFilter();
  filtro.type = 'lowpass';
  filtro.frequency.value = 600;
  filtro.Q.value = 4;
  osc1 = c.createOscillator();
  osc1.type = 'sawtooth';
  osc2 = c.createOscillator();
  osc2.type = 'square';
  const g2 = c.createGain();
  g2.gain.value = 0.35;
  osc1.connect(filtro);
  osc2.connect(g2).connect(filtro);
  filtro.connect(ganho).connect(c.destination);
  osc1.start();
  osc2.start();
  ganho.gain.setTargetAtTime(0.12, c.currentTime, 0.05);
}

/** rpm do motor e carga 0..1 (pressão do turbo deixa o som mais cheio) */
export function atualizarMotor(rpm: number, carga: number): void {
  if (!ctx || !osc1 || !osc2 || !filtro || !ganho) return;
  const f = rpm / 30; // frequência de explosão de um 4 cilindros
  const t = ctx.currentTime;
  osc1.frequency.setTargetAtTime(f, t, 0.02);
  osc2.frequency.setTargetAtTime(f * 0.5, t, 0.02);
  filtro.frequency.setTargetAtTime(400 + rpm * 0.25 + carga * 900, t, 0.03);
  ganho.gain.setTargetAtTime(0.09 + carga * 0.06, t, 0.05);
}

export function pararMotor(): void {
  if (!ctx || !ganho) return;
  const t = ctx.currentTime;
  ganho.gain.setTargetAtTime(0, t, 0.08);
  const [a, b] = [osc1, osc2];
  setTimeout(() => {
    try { a?.stop(); b?.stop(); } catch { /* já parado */ }
  }, 400);
  osc1 = osc2 = null;
  filtro = null;
  ganho = null;
}

export function estouro(): void {
  const c = garantirContexto();
  if (!c) return;
  const dur = 0.6;
  const buffer = c.createBuffer(1, c.sampleRate * dur, c.sampleRate);
  const dados = buffer.getChannelData(0);
  for (let i = 0; i < dados.length; i++) dados[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / dados.length, 3);
  const fonte = c.createBufferSource();
  fonte.buffer = buffer;
  const f = c.createBiquadFilter();
  f.type = 'lowpass';
  f.frequency.value = 900;
  const g = c.createGain();
  g.gain.value = 0.5;
  fonte.connect(f).connect(g).connect(c.destination);
  fonte.start();
}

export function bip(freq = 880, dur = 0.12): void {
  const c = garantirContexto();
  if (!c) return;
  const o = c.createOscillator();
  const g = c.createGain();
  o.frequency.value = freq;
  g.gain.value = 0.08;
  o.connect(g).connect(c.destination);
  o.start();
  g.gain.setTargetAtTime(0, c.currentTime + dur * 0.6, 0.02);
  o.stop(c.currentTime + dur + 0.1);
}
