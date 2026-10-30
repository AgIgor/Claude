// Gráfico simples em canvas, no estilo de folha de dinamômetro.

export interface Eixo {
  min: number;
  max: number;
  rotulo: string;
  casas?: number;
}

export interface Serie {
  pontos: [number, number][];
  cor: string;
  largura?: number;
  tracejado?: boolean;
  eixo?: 'esq' | 'dir';
  alpha?: number;
}

export interface Faixa {
  de: number;
  ate: number;
  cor: string;
  eixo?: 'esq' | 'dir';
}

export interface OpcoesGrafico {
  xMin: number;
  xMax: number;
  passoX: number;
  esq: Eixo;
  dir?: Eixo;
  series: Serie[];
  faixas?: Faixa[];
  cursorX?: number;
  textoVazio?: string;
}

const COR_GRADE = 'rgba(160, 205, 255, 0.12)';
const COR_EIXO = 'rgba(160, 205, 255, 0.45)';
const COR_TEXTO = '#9fbcd8';

export function arredondarMax(v: number, passos = 5): number {
  if (v <= 0) return 1;
  const bruto = v / passos;
  const mag = Math.pow(10, Math.floor(Math.log10(bruto)));
  const norm = bruto / mag;
  const passo = norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5 ? 2.5 : norm <= 5 ? 5 : 10;
  return Math.ceil(v / (passo * mag)) * passo * mag;
}

export function desenharGrafico(canvas: HTMLCanvasElement, op: OpcoesGrafico): void {
  const dpr = window.devicePixelRatio || 1;
  const larg = canvas.clientWidth;
  const alt = canvas.clientHeight;
  if (larg === 0 || alt === 0) return;
  if (canvas.width !== Math.round(larg * dpr) || canvas.height !== Math.round(alt * dpr)) {
    canvas.width = Math.round(larg * dpr);
    canvas.height = Math.round(alt * dpr);
  }
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, larg, alt);

  const m = { esq: 56, dir: op.dir ? 56 : 16, topo: 14, base: 30 };
  const w = larg - m.esq - m.dir;
  const h = alt - m.topo - m.base;
  const px = (x: number) => m.esq + ((x - op.xMin) / (op.xMax - op.xMin)) * w;
  const pyDe = (e: Eixo) => (y: number) => m.topo + h - ((y - e.min) / (e.max - e.min)) * h;
  const pyEsq = pyDe(op.esq);
  const pyDir = op.dir ? pyDe(op.dir) : pyEsq;

  ctx.font = '12px "IBM Plex Mono", monospace';
  ctx.lineWidth = 1;

  // Faixas
  for (const f of op.faixas ?? []) {
    const py = f.eixo === 'dir' ? pyDir : pyEsq;
    const e = f.eixo === 'dir' && op.dir ? op.dir : op.esq;
    const y1 = py(Math.min(f.ate, e.max));
    const y2 = py(Math.max(f.de, e.min));
    ctx.fillStyle = f.cor;
    ctx.fillRect(m.esq, y1, w, y2 - y1);
  }

  // Grade vertical
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  for (let x = Math.ceil(op.xMin / op.passoX) * op.passoX; x <= op.xMax + 1e-6; x += op.passoX) {
    ctx.strokeStyle = COR_GRADE;
    ctx.beginPath();
    ctx.moveTo(px(x) + 0.5, m.topo);
    ctx.lineTo(px(x) + 0.5, m.topo + h);
    ctx.stroke();
    ctx.fillStyle = COR_TEXTO;
    ctx.fillText(x >= 1000 ? `${(x / 1000).toLocaleString('pt-BR')}k` : String(x), px(x), m.topo + h + 8);
  }

  // Grade horizontal + rótulos
  const passos = 5;
  ctx.textBaseline = 'middle';
  for (let i = 0; i <= passos; i++) {
    const v = op.esq.min + ((op.esq.max - op.esq.min) * i) / passos;
    const y = Math.round(pyEsq(v)) + 0.5;
    ctx.strokeStyle = COR_GRADE;
    ctx.beginPath();
    ctx.moveTo(m.esq, y);
    ctx.lineTo(m.esq + w, y);
    ctx.stroke();
    ctx.fillStyle = COR_TEXTO;
    ctx.textAlign = 'right';
    ctx.fillText(v.toFixed(op.esq.casas ?? 0), m.esq - 8, y);
    if (op.dir) {
      const vd = op.dir.min + ((op.dir.max - op.dir.min) * i) / passos;
      ctx.textAlign = 'left';
      ctx.fillText(vd.toFixed(op.dir.casas ?? 0), m.esq + w + 8, y);
    }
  }

  // Eixos
  ctx.strokeStyle = COR_EIXO;
  ctx.strokeRect(m.esq + 0.5, m.topo + 0.5, w, h);

  // Rótulos dos eixos
  ctx.save();
  ctx.fillStyle = COR_TEXTO;
  ctx.font = '11px "IBM Plex Mono", monospace';
  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
  ctx.fillText(op.esq.rotulo, m.esq + 6, m.topo + 4);
  if (op.dir) {
    ctx.textAlign = 'right';
    ctx.fillText(op.dir.rotulo, m.esq + w - 6, m.topo + 4);
  }
  ctx.restore();

  // Séries
  ctx.save();
  ctx.beginPath();
  ctx.rect(m.esq, m.topo, w, h);
  ctx.clip();
  for (const s of op.series) {
    if (s.pontos.length < 2) continue;
    const py = s.eixo === 'dir' ? pyDir : pyEsq;
    ctx.globalAlpha = s.alpha ?? 1;
    ctx.strokeStyle = s.cor;
    ctx.lineWidth = s.largura ?? 2;
    ctx.lineJoin = 'round';
    ctx.setLineDash(s.tracejado ? [7, 5] : []);
    ctx.beginPath();
    s.pontos.forEach(([x, y], i) => (i === 0 ? ctx.moveTo(px(x), py(y)) : ctx.lineTo(px(x), py(y))));
    ctx.stroke();
  }
  ctx.restore();
  ctx.globalAlpha = 1;
  ctx.setLineDash([]);

  // Cursor
  if (op.cursorX !== undefined) {
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.6)';
    ctx.setLineDash([3, 4]);
    ctx.beginPath();
    ctx.moveTo(px(op.cursorX) + 0.5, m.topo);
    ctx.lineTo(px(op.cursorX) + 0.5, m.topo + h);
    ctx.stroke();
    ctx.setLineDash([]);
  }

  if (op.textoVazio && op.series.every((s) => s.pontos.length < 2)) {
    ctx.fillStyle = COR_TEXTO;
    ctx.font = '18px "Barlow Condensed", sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(op.textoVazio, m.esq + w / 2, m.topo + h / 2);
  }
}
