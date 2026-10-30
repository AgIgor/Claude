// Tela da pista: árvore, largada com tempo de reação e corrida animada.

import { estado, notificar } from '../estado.ts';
import { corteEfetivo } from '../sim/motor.ts';
import { simularPista } from '../sim/pista.ts';
import type { ResultadoPista } from '../sim/pista.ts';
import { svgCarro } from './carro.ts';
import { atualizarMotor, bip, ligarMotor, pararMotor } from './som.ts';

type Fase = 'parado' | 'contagem' | 'correndo' | 'fim';

let fase: Fase = 'parado';
let resultado: ResultadoPista | null = null;
let reacao: number | null = null;
let queimou = false;
let tempoVerde = 0;
let timers: number[] = [];

const X0 = 90;
const X1 = 960;

const $ = <T extends HTMLElement>(sel: string) => document.querySelector(sel) as T;
const fmt = (n: number | null, casas = 3) => (n === null ? '—' : n.toFixed(casas).replace('.', ','));

export function montarTelaPista(alvo: HTMLElement): void {
  alvo.innerHTML = `
    <aside class="lateral pista-config">
      <div class="painel ativo">
        <div class="campo">
          <span class="rotulo">Distância</span>
          <div class="alternador" id="dist">
            <button type="button" data-d="201">201 m</button>
            <button type="button" data-d="402">402 m</button>
          </div>
        </div>
        <div class="campo">
          <div class="campo-topo"><label for="rpm-largada">Rotação de largada</label><span class="mono" id="v-largada"></span></div>
          <input type="range" id="rpm-largada" min="2000" step="100">
        </div>
        <div class="campo">
          <div class="campo-topo"><label for="rpm-troca">Rotação de troca</label><span class="mono" id="v-troca"></span></div>
          <input type="range" id="rpm-troca" min="4000" step="100">
        </div>
        <p class="dica-mapa">Tração dianteira: muita potência na largada vira patinada. Largar mais baixo, pneu melhor ou turbo que enche mais tarde podem andar mais.</p>
        <p class="dica-mapa">Aperte <b>Espaço</b> (ou o botão) quando a luz ficar verde. Antes disso, queima a largada.</p>
      </div>
    </aside>
    <div class="pista-centro">
      <div class="cartao pista-cena">
        <div class="arvore" aria-label="Árvore de largada">
          <span class="luz" data-l="1"></span><span class="luz" data-l="2"></span><span class="luz" data-l="3"></span>
          <span class="luz" data-l="v"></span><span class="luz" data-l="r"></span>
        </div>
        <svg viewBox="0 0 1000 280" width="100%" height="280" preserveAspectRatio="xMidYMid meet" id="cena"></svg>
      </div>
      <div class="pista-hud" id="pista-hud"></div>
      <button type="button" class="botao-largar alinhar" id="largar">ALINHAR NA PISTA</button>
      <div class="mensagem-pista" id="msg-pista" role="status"></div>
      <div class="cartao">
        <div class="cartao-topo"><h2>Resultado</h2></div>
        <table class="tabela-resultado" id="tabela-pista"></table>
      </div>
    </div>`;

  $('#dist').addEventListener('click', (e) => {
    const b = (e.target as HTMLElement).closest('button');
    if (!b || fase === 'contagem' || fase === 'correndo') return;
    estado.pista.distancia = b.dataset.d === '402' ? 402 : 201;
    resultado = null;
    fase = 'parado';
    notificar();
  });
  const largada = $<HTMLInputElement>('#rpm-largada');
  const troca = $<HTMLInputElement>('#rpm-troca');
  largada.addEventListener('input', () => ($('#v-largada').textContent = `${Number(largada.value).toLocaleString('pt-BR')} rpm`));
  troca.addEventListener('input', () => ($('#v-troca').textContent = `${Number(troca.value).toLocaleString('pt-BR')} rpm`));
  largada.addEventListener('change', () => { estado.pista.rpmLargada = Number(largada.value); notificar(); });
  troca.addEventListener('change', () => { estado.pista.rpmTroca = Number(troca.value); notificar(); });

  $('#largar').addEventListener('click', acaoBotao);
  document.addEventListener('keydown', (e) => {
    if (e.code !== 'Space') return;
    const tela = document.getElementById('tela-pista');
    if (!tela?.classList.contains('ativa')) return;
    if ((e.target as HTMLElement).closest('input, select, textarea')) return;
    e.preventDefault();
    acaoBotao();
  });
}

function luzes(acesas: string[]): void {
  document.querySelectorAll<HTMLElement>('.arvore .luz').forEach((l) => {
    const id = l.dataset.l ?? '';
    l.className = 'luz';
    if (acesas.includes(id)) l.classList.add(id === 'v' ? 'verde' : id === 'r' ? 'vermelha' : 'ambar');
  });
}

function limparTimers(): void {
  timers.forEach((t) => clearTimeout(t));
  timers = [];
}

function acaoBotao(): void {
  if (fase === 'parado' || fase === 'fim') alinhar();
  else if (fase === 'contagem') largar();
}

function alinhar(): void {
  if (estado.vida <= 0) {
    mensagem('Motor quebrado. Retifique no dinamômetro antes de correr.', 'perigo');
    return;
  }
  fase = 'contagem';
  queimou = false;
  reacao = null;
  resultado = simularPista(estado.setup, estado.cond, estado.pista);
  luzes([]);
  mensagem('Segurando na rotação de largada…');
  atualizarBotao();
  desenharCena(0, null);
  ligarMotor();
  atualizarMotor(Math.min(estado.pista.rpmLargada, corteEfetivo(estado.setup)), 0.4);
  limparTimers();
  [1, 2, 3].forEach((n) => timers.push(window.setTimeout(() => { luzes(['1', '2', '3'].slice(0, n)); bip(660); }, 600 + n * 500)));
  timers.push(window.setTimeout(() => {
    luzes(['v']);
    bip(990, 0.25);
    tempoVerde = performance.now();
    timers.push(window.setTimeout(() => { if (fase === 'contagem') largar(); }, 1500));
  }, 2600));
  const inicioContagem = performance.now();
  tempoVerde = inicioContagem + 2600;
}

function largar(): void {
  const agora = performance.now();
  if (agora < tempoVerde) {
    queimou = true;
    limparTimers();
    luzes(['r']);
    mensagem('QUEIMOU A LARGADA! Saiu antes da luz verde.', 'perigo');
    reacao = (agora - tempoVerde) / 1000;
  } else {
    reacao = (agora - tempoVerde) / 1000;
    mensagem(reacao < 0.1 ? 'Largada cravada!' : reacao > 0.5 ? 'Dormiu na largada…' : 'Largou!', reacao < 0.1 ? 'ok' : '');
  }
  limparTimers();
  correr();
}

function correr(): void {
  if (!resultado) return;
  fase = 'correndo';
  atualizarBotao();
  const r = resultado;
  const inicio = performance.now();
  const quadro = (agora: number) => {
    const t = (agora - inicio) / 1000;
    let i = 0;
    while (i < r.amostras.length - 1 && r.amostras[i + 1].t <= t) i++;
    const a = r.amostras[i];
    desenharCena(a.x, a);
    atualizarHud(a.t, a.v * 3.6, a.rpm, a.marcha, a.pressao, a.patinando);
    atualizarMotor(a.rpm, Math.min(1, a.pressao / 2 + 0.4));
    if (i < r.amostras.length - 1) requestAnimationFrame(quadro);
    else terminar();
  };
  requestAnimationFrame(quadro);
}

function terminar(): void {
  fase = 'fim';
  pararMotor();
  if (resultado && !queimou) {
    const d = estado.pista.distancia;
    const et = d === 201 ? resultado.tempo201 : resultado.tempo402;
    const anterior = estado.melhores[d];
    if (et !== null && (anterior === null || et < anterior)) {
      estado.melhores[d] = et;
      mensagem(`Novo recorde pessoal nos ${d} m: ${fmt(et)} s!`, 'ok');
    }
  }
  notificar();
}

function mensagem(txt: string, tipo: '' | 'perigo' | 'ok' = ''): void {
  const m = $('#msg-pista');
  m.textContent = txt;
  m.className = `mensagem-pista ${tipo}`;
}

function atualizarBotao(): void {
  const b = $<HTMLButtonElement>('#largar');
  if (fase === 'contagem') {
    b.textContent = 'LARGAR!';
    b.className = 'botao-largar';
    b.disabled = false;
  } else if (fase === 'correndo') {
    b.textContent = 'CORRENDO…';
    b.className = 'botao-largar alinhar';
    b.disabled = true;
  } else {
    b.textContent = fase === 'fim' ? 'CORRER DE NOVO' : 'ALINHAR NA PISTA';
    b.className = 'botao-largar alinhar';
    b.disabled = false;
  }
}

function atualizarHud(t: number, kmh: number, rpm: number, marcha: number, pressao: number, patinando: boolean): void {
  const itens: [string, string, string?][] = [
    ['Tempo', `${fmt(t)} s`],
    ['km/h', kmh.toFixed(0)],
    ['RPM', Math.round(rpm).toLocaleString('pt-BR')],
    ['Marcha', String(marcha)],
    ['Pressão', `${fmt(pressao, 2)} bar`],
    ['Tração', patinando ? 'PATINANDO' : 'ok', patinando ? 'alerta' : ''],
  ];
  $('#pista-hud').innerHTML = itens
    .map(([r, v, c]) => `<div class="mostrador ${c ?? ''}"><span class="valor">${v}</span><span class="rotulo">${r}</span></div>`)
    .join('');
}

function desenharCena(x: number, a: { patinando: boolean } | null): void {
  const d = estado.pista.distancia;
  const px = (m: number) => X0 + (m / d) * (X1 - X0);
  const marcas: [number, string][] = [[0, 'LARGADA'], [18.288, '60 PÉS'], [d === 402 ? 201 : 100, d === 402 ? '201 m' : '100 m'], [d, 'CHEGADA']];
  const carroX = px(x) - 118;
  const fumaca = a?.patinando
    ? `<g fill="#9fbcd8" opacity="0.35"><circle cx="${carroX + 75}" cy="236" r="12"/><circle cx="${carroX + 60}" cy="230" r="9"/><circle cx="${carroX + 48}" cy="238" r="7"/></g>`
    : '';
  $('#cena').innerHTML = `
    <g stroke="#2d5a86" stroke-width="1">
      ${Array.from({ length: 20 }, (_, i) => `<line x1="${i * 50}" y1="40" x2="${i * 50}" y2="250" opacity="0.4"/>`).join('')}
    </g>
    <line x1="0" y1="250" x2="1000" y2="250" stroke="#e8f1fa" stroke-width="2"/>
    <line x1="0" y1="262" x2="1000" y2="262" stroke="#2d5a86" stroke-width="1" stroke-dasharray="10 8"/>
    ${marcas.map(([m, t]) => `
      <line x1="${px(m)}" y1="150" x2="${px(m)}" y2="262" stroke="${t === 'CHEGADA' || t === 'LARGADA' ? '#ffc24b' : '#9fbcd8'}" stroke-width="${t === 'CHEGADA' || t === 'LARGADA' ? 2 : 1}" stroke-dasharray="${t === 'CHEGADA' || t === 'LARGADA' ? '' : '4 4'}"/>
      <text x="${px(m)}" y="142" text-anchor="middle" fill="#9fbcd8" font-family="IBM Plex Mono, monospace" font-size="12">${t}</text>`).join('')}
    ${fumaca}
    <g transform="translate(${carroX}, 172) scale(0.33)">${svgCarro(400).replace(/<svg[^>]*>|<\/svg>/g, '')}</g>
  `;
}

function renderTabela(): void {
  const tab = $('#tabela-pista');
  const d = estado.pista.distancia;
  const r = fase === 'fim' ? resultado : null;
  const et = r ? (d === 201 ? r.tempo201 : r.tempo402) : null;
  const linhas: [string, string, string][] = [
    ['Reação', r ? (queimou ? 'QUEIMOU' : `${fmt(reacao)} s`) : '—', ''],
    ['60 pés', r ? `${fmt(r.tempo60pes)} s` : '—', ''],
  ];
  if (d === 402) linhas.push(['201 m', r ? `${fmt(r.tempo201)} s` : '—', '']);
  linhas.push([`${d} m (tempo)`, r ? `${fmt(et)} s` : '—', estado.melhores[d] !== null ? `${fmt(estado.melhores[d])} s` : '—']);
  linhas.push(['Tempo + reação', r && !queimou && et !== null && reacao !== null ? `${fmt(et + reacao)} s` : '—', '']);
  linhas.push(['Velocidade final', r ? `${r.velFinalKmh.toFixed(0)} km/h` : '—', '']);
  linhas.push(['Tempo patinando', r ? `${fmt(r.tempoPatinando, 2)} s` : '—', '']);
  tab.innerHTML = `<thead><tr><th scope="col">Parcial</th><th scope="col">Esta corrida</th><th scope="col">Melhor</th></tr></thead><tbody>${linhas
    .map(([a, b, c]) => `<tr><td>${a}</td><td>${b}</td><td>${c}</td></tr>`)
    .join('')}</tbody>`;
}

export function atualizarTelaPista(): void {
  const corte = corteEfetivo(estado.setup);
  const largada = $<HTMLInputElement>('#rpm-largada');
  const troca = $<HTMLInputElement>('#rpm-troca');
  if (!largada) return;
  largada.max = String(corte - 200);
  troca.max = String(corte);
  estado.pista.rpmLargada = Math.min(estado.pista.rpmLargada, corte - 200);
  estado.pista.rpmTroca = Math.min(estado.pista.rpmTroca, corte);
  largada.value = String(estado.pista.rpmLargada);
  troca.value = String(estado.pista.rpmTroca);
  $('#v-largada').textContent = `${estado.pista.rpmLargada.toLocaleString('pt-BR')} rpm`;
  $('#v-troca').textContent = `${estado.pista.rpmTroca.toLocaleString('pt-BR')} rpm`;
  document.querySelectorAll<HTMLButtonElement>('#dist button').forEach((b) => {
    const ativo = Number(b.dataset.d) === estado.pista.distancia;
    b.classList.toggle('ativo', ativo);
    b.setAttribute('aria-pressed', String(ativo));
  });
  if (fase === 'parado') {
    desenharCena(0, null);
    atualizarHud(0, 0, 0, 1, 0, false);
  }
  if (fase !== 'correndo' && fase !== 'contagem') atualizarBotao();
  renderTabela();
}
