// Estado do jogo e persistência local (só conveniência do protótipo).

import { CATALOGO, RPM_MAPA } from './sim/dados.ts';
import type { Categoria } from './sim/dados.ts';
import { setupPadrao } from './sim/motor.ts';
import type { Condicoes, Setup } from './sim/motor.ts';
import type { Puxada } from './sim/dino.ts';
import type { ConfigPista } from './sim/pista.ts';

export type Canal = 'lambda' | 'ignicao' | 'pressao' | 'detonacao' | 'temperaturas' | 'bicos';

export interface Estado {
  setup: Setup;
  cond: Condicoes;
  vida: number;
  quebras: number;
  puxadas: Puxada[];
  proxId: number;
  selecionada: number | null;
  canal: Canal;
  pista: ConfigPista;
  melhores: { 201: number | null; 402: number | null };
}

const CHAVE = 'arrancada402.v1';
export const MAX_PUXADAS = 6;
export const CORES_PUXADA = ['#ffc24b', '#6fd3ff', '#c4a2ff', '#6ee7a1', '#ff9dbb', '#ffffff'];

export function corDaPuxada(id: number): string {
  return CORES_PUXADA[(id - 1) % CORES_PUXADA.length];
}

function padrao(): Estado {
  return {
    setup: setupPadrao(),
    cond: { temperaturaC: 25, altitudeM: 760 },
    vida: 100,
    quebras: 0,
    puxadas: [],
    proxId: 1,
    selecionada: null,
    canal: 'lambda',
    pista: { distancia: 201, rpmLargada: 4000, rpmTroca: 6300 },
    melhores: { 201: null, 402: null },
  };
}

function carregar(): Estado {
  const base = padrao();
  try {
    const bruto = localStorage.getItem(CHAVE);
    if (!bruto) return base;
    const salvo = JSON.parse(bruto) as Partial<Estado>;
    if (salvo.setup) {
      for (const c of Object.keys(CATALOGO) as Categoria[]) {
        const id = salvo.setup.pecas?.[c];
        if (id && CATALOGO[c].some((o) => o.id === id)) base.setup.pecas[c] = id;
      }
      const m = salvo.setup.mapa;
      if (m && m.ignicao?.length === RPM_MAPA.length && m.lambda?.length === RPM_MAPA.length) base.setup.mapa = m;
      if (salvo.setup.combustivel) base.setup.combustivel = salvo.setup.combustivel;
    }
    if (salvo.cond) base.cond = salvo.cond;
    if (typeof salvo.vida === 'number') base.vida = salvo.vida;
    if (typeof salvo.quebras === 'number') base.quebras = salvo.quebras;
    if (salvo.pista) base.pista = { ...base.pista, ...salvo.pista };
    if (salvo.melhores) base.melhores = { ...base.melhores, ...salvo.melhores };
  } catch {
    /* sem armazenamento: começa do zero */
  }
  return base;
}

export const estado: Estado = carregar();

export function salvar(): void {
  try {
    const { setup, cond, vida, quebras, pista, melhores } = estado;
    localStorage.setItem(CHAVE, JSON.stringify({ setup, cond, vida, quebras, pista, melhores }));
  } catch {
    /* ignora */
  }
}

const ouvintes: (() => void)[] = [];
export function aoMudar(fn: () => void): void {
  ouvintes.push(fn);
}
export function notificar(): void {
  salvar();
  ouvintes.forEach((fn) => fn());
}

export function resetarTudo(): void {
  Object.assign(estado, padrao());
  notificar();
}
