// Tela do dinamômetro: mostradores, curva, puxadas, datalog e diagnóstico.

import { corDaPuxada, estado, MAX_PUXADAS, notificar } from '../estado.ts';
import type { Canal } from '../estado.ts';
import { diagnosticar, puxar, PASSO_RPM, RPM_POR_SEGUNDO } from '../sim/dino.ts';
import type { Puxada } from '../sim/dino.ts';
import { CARRO } from '../sim/dados.ts';
import { KGFM_POR_NM } from '../sim/motor.ts';
import type { Ponto } from '../sim/motor.ts';
import { arredondarMax, desenharGrafico } from './grafico.ts';
import type { Faixa, OpcoesGrafico, Serie } from './grafico.ts';
import { atualizarMotor, estouro, ligarMotor, pararMotor } from './som.ts';

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

let animando: { px: Puxada; idx: number } | null = null;

export function estaAnimando(): boolean {
  return animando !== null;
}

// ---------- Mostradores ----------

const MOSTRADORES = [
  { id: 'rpm', rotulo: 'RPM' },
  { id: 'cv', rotulo: 'cv roda' },
  { id: 'torque', rotulo: 'kgfm' },
  { id: 'pressao', rotulo: 'Pressão bar' },
  { id: 'lambda', rotulo: 'Lambda' },
  { id: 'ign', rotulo: 'Ignição °' },
  { id: 'adm', rotulo: 'Admissão °C' },
  { id: 'egt', rotulo: 'Escape °C' },
  { id: 'det', rotulo: 'Detonação' },
] as const;

export function montarMostradores(): void {
  const alvo = $('mostradores');
  alvo.replaceChildren();
  for (const m of MOSTRADORES) {
    const d = document.createElement('div');
    d.className = 'mostrador';
    d.id = `m-${m.id}`;
    d.innerHTML = `<span class="valor">—</span><span class="rotulo">${m.rotulo}</span>`;
    alvo.append(d);
  }
}

function definirMostrador(id: string, texto: string, nivel: '' | 'atencao' | 'alerta' = ''): void {
  const d = document.getElementById(`m-${id}`);
  if (!d) return;
  d.className = `mostrador ${nivel}`;
  const v = d.querySelector('.valor');
  if (v) v.textContent = texto;
}

const virgula = (n: number, casas: number) => n.toFixed(casas).replace('.', ',');

function atualizarMostradores(p: Ponto | null): void {
  if (!p) {
    MOSTRADORES.forEach((m) => definirMostrador(m.id, '—'));
    const det = document.querySelector('#m-det .valor');
    if (det) det.innerHTML = '<span class="lampada"></span>';
    return;
  }
  definirMostrador('rpm', Math.round(p.rpm).toLocaleString('pt-BR'), p.flutuacao ? 'alerta' : '');
  definirMostrador('cv', p.potenciaRodaCv.toFixed(0));
  definirMostrador('torque', virgula(p.torqueNm * KGFM_POR_NM, 1));
  definirMostrador('pressao', virgula(p.pressao, 2), p.turboNoLimite ? 'atencao' : '');
  definirMostrador('lambda', virgula(p.lambda, 2), p.lambda > 0.95 ? 'alerta' : p.faltaCombustivel ? 'atencao' : '');
  definirMostrador('ign', virgula(p.ignicao, 1));
  definirMostrador('adm', p.tempAdmissaoC.toFixed(0), p.tempAdmissaoC > 70 ? 'atencao' : '');
  definirMostrador('egt', p.egtC.toFixed(0), p.egtC > 950 ? 'alerta' : p.egtC > 900 ? 'atencao' : '');
  const det = document.getElementById('m-det');
  if (det) {
    det.className = `mostrador ${p.detonacao > 0.3 ? 'alerta' : ''}`;
    const v = det.querySelector('.valor');
    if (v) v.innerHTML = `<span class="lampada ${p.detonacao > 0.3 ? 'acesa' : ''}"></span>${p.detonacao > 0.3 ? ` ${virgula(p.detonacao, 1)}°` : ''}`;
  }
}

// ---------- Gráfico principal ----------

function desenharPrincipal(): void {
  const canvas = $<HTMLCanvasElement>('grafico-dino');
  const lista = [...estado.puxadas];
  if (animando) lista.push({ ...animando.px, pontos: animando.px.pontos.slice(0, animando.idx + 1) });

  let maxCv = 100, maxTq = 15, maxRpm = 7000;
  for (const px of lista) {
    for (const p of px.pontos) {
      maxCv = Math.max(maxCv, p.potenciaRodaCv);
      maxTq = Math.max(maxTq, p.torqueNm * KGFM_POR_NM);
    }
    maxRpm = Math.max(maxRpm, px.setup.mapa.corte, ...px.pontos.map((p) => p.rpm));
  }
  if (animando) maxRpm = Math.max(maxRpm, animando.px.pontos[animando.px.pontos.length - 1].rpm);

  const series: Serie[] = [];
  const selId = animando ? animando.px.id : estado.selecionada;
  for (const px of lista) {
    const sel = px.id === selId;
    const cor = corDaPuxada(px.id);
    const alpha = sel ? 1 : 0.4;
    series.push({ pontos: px.pontos.map((p) => [p.rpm, p.potenciaRodaCv]), cor, largura: sel ? 3 : 1.6, alpha });
    series.push({ pontos: px.pontos.map((p) => [p.rpm, p.torqueNm * KGFM_POR_NM]), cor, largura: sel ? 2.4 : 1.4, tracejado: true, eixo: 'dir', alpha });
  }

  const op: OpcoesGrafico = {
    xMin: 2000,
    xMax: Math.ceil(maxRpm / 1000) * 1000,
    passoX: 1000,
    esq: { min: 0, max: arredondarMax(maxCv * 1.08), rotulo: 'cv roda' },
    dir: { min: 0, max: arredondarMax(maxTq * 1.08), rotulo: 'kgfm', casas: 0 },
    series,
    cursorX: animando ? animando.px.pontos[animando.idx].rpm : undefined,
    textoVazio: 'Monte o carro, acerte o mapa e faça a primeira puxada.',
  };
  desenharGrafico(canvas, op);
}

// ---------- Datalog ----------

const CANAIS: { id: Canal; nome: string }[] = [
  { id: 'lambda', nome: 'Lambda' },
  { id: 'ignicao', nome: 'Ignição' },
  { id: 'pressao', nome: 'Pressão' },
  { id: 'detonacao', nome: 'Detonação' },
  { id: 'temperaturas', nome: 'Temperaturas' },
  { id: 'bicos', nome: 'Bicos %' },
];

export function montarCanais(): void {
  const alvo = $('canais');
  alvo.replaceChildren();
  for (const c of CANAIS) {
    const b = document.createElement('button');
    b.type = 'button';
    b.textContent = c.nome;
    b.className = c.id === estado.canal ? 'ativo' : '';
    b.setAttribute('aria-pressed', String(c.id === estado.canal));
    b.addEventListener('click', () => {
      estado.canal = c.id;
      montarCanais();
      desenharLog();
    });
    alvo.append(b);
  }
}

function puxadaEmFoco(): Puxada | null {
  if (animando) return { ...animando.px, pontos: animando.px.pontos.slice(0, animando.idx + 1) };
  return estado.puxadas.find((p) => p.id === estado.selecionada) ?? null;
}

const VERMELHO_FAIXA = 'rgba(255, 107, 87, 0.13)';

function desenharLog(): void {
  const canvas = $<HTMLCanvasElement>('grafico-log');
  const px = puxadaEmFoco();
  const pts = px?.pontos ?? [];
  const xMax = Math.ceil(Math.max(7000, px?.setup.mapa.corte ?? 0, ...pts.map((p) => p.rpm)) / 1000) * 1000;
  const serie = (f: (p: Ponto) => number, cor: string, extra: Partial<Serie> = {}): Serie => ({ pontos: pts.map((p) => [p.rpm, f(p)]), cor, largura: 2, ...extra });

  let op: Omit<OpcoesGrafico, 'xMin' | 'xMax' | 'passoX'>;
  let faixas: Faixa[] = [];
  switch (estado.canal) {
    case 'lambda':
      faixas = [{ de: 0.95, ate: 1.2, cor: VERMELHO_FAIXA }];
      op = { esq: { min: 0.6, max: 1.2, rotulo: 'lambda (real e alvo tracejado)', casas: 2 }, series: [serie((p) => p.lambdaAlvo, '#ffffff', { tracejado: true, largura: 1.4 }), serie((p) => p.lambda, '#6fd3ff')] };
      break;
    case 'ignicao':
      op = { esq: { min: 0, max: 45, rotulo: 'graus de avanço' }, series: [serie((p) => p.ignicao, '#ffc24b')] };
      break;
    case 'pressao': {
      const max = arredondarMax(Math.max(1, ...pts.map((p) => p.pressaoAlvo)) * 1.15);
      op = { esq: { min: 0, max, rotulo: 'bar (real e alvo tracejado)', casas: 1 }, series: [serie((p) => p.pressaoAlvo, '#ffffff', { tracejado: true, largura: 1.4 }), serie((p) => p.pressao, '#6fd3ff')] };
      break;
    }
    case 'detonacao': {
      const max = arredondarMax(Math.max(4, ...pts.map((p) => p.detonacao)));
      op = { esq: { min: 0, max, rotulo: 'graus de detonação', casas: 0 }, series: [serie((p) => p.detonacao, '#ff6b57', { largura: 2.4 })] };
      break;
    }
    case 'temperaturas':
      faixas = [{ de: 950, ate: 1200, cor: VERMELHO_FAIXA, eixo: 'dir' }];
      op = {
        esq: { min: 0, max: 150, rotulo: 'admissão °C' },
        dir: { min: 600, max: 1100, rotulo: 'escape °C' },
        series: [serie((p) => p.tempAdmissaoC, '#6fd3ff'), serie((p) => p.egtC, '#ffc24b', { eixo: 'dir' })],
      };
      break;
    case 'bicos': {
      const max = arredondarMax(Math.max(120, ...pts.map((p) => p.dutyBicos * 100)));
      faixas = [{ de: 100, ate: max, cor: VERMELHO_FAIXA }];
      op = { esq: { min: 0, max, rotulo: '% de abertura dos bicos' }, series: [serie((p) => p.dutyBicos * 100, '#ffc24b')] };
      break;
    }
  }
  desenharGrafico(canvas, {
    xMin: 2000, xMax, passoX: 1000, faixas, ...op,
    cursorX: animando ? animando.px.pontos[animando.idx].rpm : undefined,
    textoVazio: 'Sem puxada selecionada',
  });
}

// ---------- Resultado, lista e observações ----------

function renderResultado(): void {
  const alvo = $('resultado');
  const px = estado.puxadas.find((p) => p.id === estado.selecionada);
  if (!px) {
    alvo.innerHTML = '<span class="vazio">Nenhuma puxada ainda.</span>';
    return;
  }
  const idx = estado.puxadas.indexOf(px);
  const anterior = idx > 0 ? estado.puxadas[idx - 1] : null;
  const delta = anterior ? px.picoCvRoda - anterior.picoCvRoda : null;
  const motorCv = px.picoCvRoda / (1 - CARRO.perdaTransmissao);
  alvo.innerHTML = `
    <div class="item"><span class="rotulo">Pico na roda</span><span class="grande">${px.picoCvRoda.toFixed(1).replace('.', ',')} cv</span>
      ${delta !== null ? `<span class="delta ${delta >= 0 ? 'mais' : 'menos'}">${delta >= 0 ? '+' : ''}${delta.toFixed(1).replace('.', ',')} cv vs anterior</span>` : `<span class="delta">a ${px.rpmPicoCv.toLocaleString('pt-BR')} rpm</span>`}
    </div>
    <div class="item"><span class="rotulo">No motor ≈</span><span class="medio">${motorCv.toFixed(0)} cv</span></div>
    <div class="item"><span class="rotulo">Torque</span><span class="medio">${(px.picoTorqueNm * KGFM_POR_NM).toFixed(1).replace('.', ',')} kgfm</span><span class="delta">a ${px.rpmPicoTorque.toLocaleString('pt-BR')} rpm</span></div>
  `;
}

function renderPuxadas(): void {
  const alvo = $('puxadas');
  alvo.replaceChildren();
  for (const px of estado.puxadas) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = `chip ${px.id === estado.selecionada ? 'sel' : ''}`;
    b.setAttribute('aria-pressed', String(px.id === estado.selecionada));
    b.innerHTML = `<span class="cor" style="background:${corDaPuxada(px.id)}"></span><span>#${px.id}</span><span class="mono">${px.picoCvRoda.toFixed(0)} cv</span>${px.quebra ? '<span class="x">quebrou</span>' : ''}`;
    b.title = px.setupResumo;
    b.addEventListener('click', () => {
      estado.selecionada = px.id;
      atualizarTelaDino();
    });
    alvo.append(b);
  }
  if (estado.puxadas.length > 0) {
    const limpar = document.createElement('button');
    limpar.type = 'button';
    limpar.className = 'chip';
    limpar.textContent = 'Limpar';
    limpar.addEventListener('click', () => {
      estado.puxadas = [];
      estado.selecionada = null;
      atualizarTelaDino();
    });
    alvo.append(limpar);
  }
}

const TAGS = { perigo: 'PERIGO', atencao: 'ATENÇÃO', dica: 'DICA', ok: 'OK' } as const;

function renderObservacoes(): void {
  const alvo = $('observacoes');
  alvo.replaceChildren();
  const px = estado.puxadas.find((p) => p.id === estado.selecionada);
  if (!px) {
    const li = document.createElement('li');
    li.className = 'dica';
    li.innerHTML = '<span class="tag">DICA</span>Faça uma puxada de referência antes de mexer em qualquer coisa. Depois mude uma coisa de cada vez e compare as curvas.';
    alvo.append(li);
    return;
  }
  for (const o of diagnosticar(px, px.setup)) {
    const li = document.createElement('li');
    li.className = o.gravidade;
    const tag = document.createElement('span');
    tag.className = 'tag';
    tag.textContent = TAGS[o.gravidade];
    li.append(tag, document.createTextNode(o.texto));
    alvo.append(li);
  }
}

function renderBotao(): void {
  const b = $<HTMLButtonElement>('puxar');
  if (estado.vida <= 0) {
    b.textContent = 'RETIFICAR MOTOR';
    b.classList.add('retificar');
  } else {
    b.textContent = animando ? 'PUXANDO…' : 'PUXAR NO DINO';
    b.classList.remove('retificar');
  }
  b.disabled = animando !== null;
}

export function atualizarTelaDino(): void {
  renderBotao();
  renderResultado();
  renderPuxadas();
  renderObservacoes();
  desenharPrincipal();
  desenharLog();
  if (!animando) {
    const px = estado.puxadas.find((p) => p.id === estado.selecionada);
    atualizarMostradores(px ? px.pontos[px.pontos.length - 1] : null);
  }
}

export function redesenharGraficos(): void {
  desenharPrincipal();
  desenharLog();
}

// ---------- Puxada animada ----------

function mostrarQuebra(px: Puxada): void {
  const caixa = $('aviso-quebra');
  caixa.innerHTML = '';
  const div = document.createElement('div');
  div.className = 'aviso-caixa';
  div.setAttribute('role', 'alertdialog');
  div.setAttribute('aria-labelledby', 'quebra-titulo');
  div.innerHTML = `<h3 id="quebra-titulo">MOTOR QUEBROU</h3><p></p><p class="dica-mapa">Veja o datalog para entender o que aconteceu. O motor precisa ser retificado antes da próxima puxada.</p>`;
  (div.querySelector('p') as HTMLElement).textContent = `${px.quebra?.texto} a ${px.quebra?.rpm.toLocaleString('pt-BR')} rpm.`;
  const ok = document.createElement('button');
  ok.type = 'button';
  ok.className = 'botao-puxar retificar';
  ok.textContent = 'ENTENDI';
  ok.addEventListener('click', () => (caixa.hidden = true));
  div.append(ok);
  caixa.append(div);
  caixa.hidden = false;
  ok.focus();
}

export function aoClicarPuxar(): void {
  if (animando) return;
  if (estado.vida <= 0) {
    estado.vida = 100;
    notificar();
    return;
  }
  const px = puxar(estado.proxId++, estado.setup, estado.cond, estado.vida);
  animando = { px, idx: 0 };
  document.body.classList.add('puxando');
  renderBotao();
  ligarMotor();
  const dtPonto = (PASSO_RPM / RPM_POR_SEGUNDO) * 1000;
  const inicio = performance.now();

  const quadro = (agora: number) => {
    if (!animando) return;
    const idx = Math.min(px.pontos.length - 1, Math.floor((agora - inicio) / dtPonto));
    animando.idx = idx;
    const p = px.pontos[idx];
    atualizarMostradores(p);
    atualizarMotor(p.rpm, Math.min(1, p.pressao / 2 + 0.3));
    desenharPrincipal();
    desenharLog();
    if (idx < px.pontos.length - 1) {
      requestAnimationFrame(quadro);
    } else {
      finalizar(px);
    }
  };
  requestAnimationFrame(quadro);
}

function finalizar(px: Puxada): void {
  animando = null;
  document.body.classList.remove('puxando');
  pararMotor();
  estado.puxadas.push(px);
  if (estado.puxadas.length > MAX_PUXADAS) estado.puxadas.shift();
  estado.selecionada = px.id;
  estado.vida = Math.max(0, estado.vida - px.desgaste);
  if (px.quebra) {
    estado.vida = 0;
    estado.quebras++;
    estouro();
    mostrarQuebra(px);
  }
  notificar();
}
