// Painéis laterais: peças, mapa da injeção e condições do dia.

import { estado, notificar } from '../estado.ts';
import { ALTITUDES, CATALOGO, COMBUSTIVEIS, NOMES_CATEGORIA, ORDEM_CATEGORIAS, RPM_MAPA } from '../sim/dados.ts';
import type { CombustivelId } from '../sim/dados.ts';
import { peca, pressaoAmbienteKpa } from '../sim/motor.ts';

const moeda = (v: number) => `R$ ${v.toLocaleString('pt-BR')}`;

function el<K extends keyof HTMLElementTagNameMap>(tag: K, attrs: Record<string, string> = {}, texto?: string): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (texto !== undefined) e.textContent = texto;
  return e;
}

// ---------- Peças ----------

export function renderPecas(alvo: HTMLElement): void {
  alvo.replaceChildren();

  const blocoComb = el('div', { class: 'peca' });
  blocoComb.append(el('label', { for: 'sel-combustivel' }, 'Combustível'));
  const selComb = el('select', { id: 'sel-combustivel' });
  for (const c of Object.values(COMBUSTIVEIS)) {
    const o = el('option', { value: c.id }, `${c.nome}${c.preco ? ` · ${moeda(c.preco)}` : ''}`);
    if (c.id === estado.setup.combustivel) o.selected = true;
    selComb.append(o);
  }
  selComb.addEventListener('change', () => {
    estado.setup.combustivel = selComb.value as CombustivelId;
    notificar();
  });
  blocoComb.append(selComb, el('span', { class: 'desc' }, descCombustivel(estado.setup.combustivel)));
  alvo.append(blocoComb);

  for (const cat of ORDEM_CATEGORIAS) {
    const bloco = el('div', { class: 'peca' });
    const id = `sel-${cat}`;
    bloco.append(el('label', { for: id }, NOMES_CATEGORIA[cat]));
    const sel = el('select', { id });
    for (const op of CATALOGO[cat]) {
      const o = el('option', { value: op.id }, op.preco ? `${op.nome} · ${moeda(op.preco)}` : op.nome);
      if (op.id === estado.setup.pecas[cat]) o.selected = true;
      sel.append(o);
    }
    sel.addEventListener('change', () => {
      estado.setup.pecas[cat] = sel.value;
      if (cat === 'turbo') {
        const t = peca('turbo', sel.value);
        if (t.pressaoMax > 0) estado.setup.mapa.pressaoAlvo = Math.min(estado.setup.mapa.pressaoAlvo, t.pressaoMax);
      }
      notificar();
    });
    bloco.append(sel);
    const desc = peca(cat, estado.setup.pecas[cat]).desc;
    if (desc) bloco.append(el('span', { class: 'desc' }, desc));
    alvo.append(bloco);
  }
}

function descCombustivel(id: CombustivelId): string {
  switch (id) {
    case 'gasolina': return 'Precisa de menos vazão, mas detona fácil sob pressão.';
    case 'etanol': return 'Aguenta bem mais avanço e pressão. Precisa de ~55% mais vazão de combustível.';
    case 'metanol': return 'O mais resistente à detonação e esfria muito a admissão. Precisa do dobro de vazão.';
  }
}

// ---------- Mapa ----------

export function renderMapa(alvo: HTMLElement): void {
  alvo.replaceChildren();
  const { mapa, pecas } = estado.setup;
  const ecu = peca('ecu', pecas.ecu);
  const turbo = peca('turbo', pecas.turbo);
  const travado = !ecu.programavel;

  if (travado) {
    alvo.append(el('div', { class: 'bloqueado' }, 'Injeção original: o mapa é fixo. Instale uma injeção programável na aba Peças para mexer aqui.'));
  }

  // Pressão do turbo
  if (turbo.id !== 'nenhum') {
    alvo.append(
      campoFaixa('Pressão do turbo', 'faixa-pressao', 0.3, turbo.pressaoMax, 0.1, travado ? 0.5 : mapa.pressaoAlvo, (v) => `${v.toFixed(1).replace('.', ',')} bar`, travado, (v) => {
        mapa.pressaoAlvo = v;
      }),
    );
  }
  alvo.append(
    campoFaixa('Corte de giro', 'faixa-corte', 5000, 9000, 100, travado ? 6500 : mapa.corte, (v) => `${v.toLocaleString('pt-BR')} rpm`, travado, (v) => {
      mapa.corte = v;
    }),
  );

  alvo.append(
    tabelaMapa('Ignição (graus de avanço)', mapa.ignicao, 0.5, 0, 45, 1, travado, [
      ['−2°', -2], ['−1°', -1], ['+1°', 1],
    ]),
  );
  alvo.append(
    tabelaMapa('Lambda alvo (menor = mais rico)', mapa.lambda, 0.01, 0.65, 1.1, 2, travado, [
      ['−0,02', -0.02], ['+0,02', 0.02],
    ]),
  );

  alvo.append(
    el(
      'p',
      { class: 'dica-mapa' },
      'Mais avanço dá mais torque até um ponto ideal escondido. Passou do limite, o motor detona. Com pressão, o limite cai bastante. Lambda perto de 0,85 rende bem; mais pobre esquenta e detona.',
    ),
  );
}

function campoFaixa(
  rotulo: string, id: string, min: number, max: number, passo: number, valor: number,
  fmt: (v: number) => string, travado: boolean, aoMudarValor: (v: number) => void,
): HTMLElement {
  const campo = el('div', { class: 'campo' });
  const topo = el('div', { class: 'campo-topo' });
  const saida = el('span', { class: 'mono' }, fmt(valor));
  topo.append(el('label', { for: id }, rotulo), saida);
  const input = el('input', { type: 'range', id, min: String(min), max: String(max), step: String(passo), value: String(valor) });
  input.disabled = travado;
  input.addEventListener('input', () => (saida.textContent = fmt(parseFloat(input.value))));
  input.addEventListener('change', () => {
    aoMudarValor(parseFloat(input.value));
    notificar();
  });
  campo.append(topo, input);
  return campo;
}

function tabelaMapa(
  titulo: string, valores: number[], passo: number, min: number, max: number, casas: number,
  travado: boolean, ajustes: [string, number][],
): HTMLElement {
  const bloco = el('div', { class: 'mapa-bloco' });
  const topo = el('div', { class: 'mapa-bloco-topo' });
  topo.append(el('h2', {}, titulo));
  const botoes = el('div', { class: 'mini-botoes' });
  for (const [txt, delta] of ajustes) {
    const b = el('button', { type: 'button', 'aria-label': `${txt} em todas as colunas de ${titulo}` }, txt);
    b.disabled = travado;
    b.addEventListener('click', () => {
      for (let i = 0; i < valores.length; i++) valores[i] = limitar(valores[i] + delta, min, max, casas);
      notificar();
    });
    botoes.append(b);
  }
  topo.append(botoes);
  bloco.append(topo);

  const tabela = el('table', { class: 'tabela-mapa' });
  const cab = el('tr');
  cab.append(el('th', { class: 'lin', scope: 'col' }, 'rpm'));
  const lin = el('tr');
  lin.append(el('th', { class: 'lin', scope: 'row' }, ''));
  RPM_MAPA.forEach((rpm, i) => {
    cab.append(el('th', { scope: 'col' }, `${rpm / 1000}k`));
    const td = el('td');
    const inp = el('input', {
      type: 'number', step: String(passo), min: String(min), max: String(max),
      value: valores[i].toFixed(casas), 'aria-label': `${titulo} a ${rpm} rpm`,
    });
    inp.disabled = travado;
    inp.addEventListener('change', () => {
      const v = parseFloat(inp.value.replace(',', '.'));
      if (!Number.isNaN(v)) valores[i] = limitar(v, min, max, casas);
      notificar();
    });
    td.append(inp);
    lin.append(td);
  });
  // primeira célula de cada linha vazia para alinhar com o cabeçalho "rpm"
  tabela.append(cab, lin);
  bloco.append(tabela);
  return bloco;
}

function limitar(v: number, min: number, max: number, casas: number): number {
  const f = Math.pow(10, casas);
  return Math.round(Math.min(max, Math.max(min, v)) * f) / f;
}

// ---------- Condições ----------

export function renderClima(alvo: HTMLElement): void {
  alvo.replaceChildren();
  const { cond } = estado;

  alvo.append(
    campoFaixa('Temperatura do ar', 'faixa-temp', 5, 42, 1, cond.temperaturaC, (v) => `${v} °C`, false, (v) => {
      cond.temperaturaC = v;
    }),
  );

  const campoAlt = el('div', { class: 'campo' });
  campoAlt.append(el('label', { for: 'sel-altitude' }, 'Local da oficina'));
  const sel = el('select', { id: 'sel-altitude' });
  for (const a of ALTITUDES) {
    const o = el('option', { value: String(a.metros) }, a.nome);
    if (a.metros === cond.altitudeM) o.selected = true;
    sel.append(o);
  }
  sel.addEventListener('change', () => {
    cond.altitudeM = parseInt(sel.value, 10);
    notificar();
  });
  campoAlt.append(sel);
  alvo.append(campoAlt);

  const rho = (pressaoAmbienteKpa(cond.altitudeM) * 1000) / (287 * (cond.temperaturaC + 273.15));
  const rel = (rho / 1.184) * 100;
  const info = el('div', { class: 'campo' });
  const topo = el('div', { class: 'campo-topo' });
  topo.append(el('span', {}, 'Densidade do ar'), el('span', { class: 'mono' }, `${rel.toFixed(1).replace('.', ',')}%`));
  info.append(
    topo,
    el('p', { class: 'dica-mapa' }, 'Ar frio e baixa altitude têm mais oxigênio: o motor rende mais, mas também detona mais fácil. O acerto de um dia pode não servir no outro.'),
  );
  alvo.append(info);

  const sortear = el('button', { type: 'button', class: 'botao-sec' }, 'Sortear clima do dia');
  sortear.addEventListener('click', () => {
    cond.temperaturaC = Math.round(10 + Math.random() * 28);
    cond.altitudeM = ALTITUDES[Math.floor(Math.random() * ALTITUDES.length)].metros;
    notificar();
  });
  alvo.append(sortear);
}
