// Simulação do motor em plena carga, ponto a ponto por rotação.
// Modelo simplificado: fluxo de ar (VE × densidade), turbo, mistura, ignição e detonação.

import { CARRO, CATALOGO, COMBUSTIVEIS, RPM_MAPA } from './dados.ts';
import type { Categoria, CombustivelId } from './dados.ts';

export type Pecas = Record<Categoria, string>;

export interface Mapa {
  ignicao: number[]; // graus, um por coluna de RPM_MAPA
  lambda: number[]; // alvo, um por coluna
  pressaoAlvo: number; // bar
  corte: number; // rpm
}

export interface Setup {
  pecas: Pecas;
  mapa: Mapa;
  combustivel: CombustivelId;
}

export interface Condicoes {
  temperaturaC: number;
  altitudeM: number;
}

export interface Ponto {
  rpm: number;
  torqueNm: number;
  potenciaCv: number; // no motor
  potenciaRodaCv: number;
  pressao: number; // bar relativo
  pressaoAlvo: number;
  lambda: number;
  lambdaAlvo: number;
  ignicao: number;
  mbt: number; // avanço de melhor torque (oculto do jogador)
  limiteDetonacao: number; // oculto do jogador
  detonacao: number; // graus acima do limite
  tempAdmissaoC: number;
  egtC: number;
  dutyBicos: number; // 0..1+
  faltaCombustivel: boolean;
  turboNoLimite: boolean;
  flutuacao: boolean;
}

export const CV_POR_KW = 1.35962;
export const KGFM_POR_NM = 0.101972;

export function peca<C extends Categoria>(cat: C, id: string): (typeof CATALOGO)[C][number] {
  const lista = CATALOGO[cat] as (typeof CATALOGO)[C][number][];
  return lista.find((o) => o.id === id) ?? lista[0];
}

export function interpMapa(valores: number[], rpm: number): number {
  if (rpm <= RPM_MAPA[0]) return valores[0];
  for (let i = 1; i < RPM_MAPA.length; i++) {
    if (rpm <= RPM_MAPA[i]) {
      const t = (rpm - RPM_MAPA[i - 1]) / (RPM_MAPA[i] - RPM_MAPA[i - 1]);
      return valores[i - 1] + (valores[i] - valores[i - 1]) * t;
    }
  }
  return valores[valores.length - 1];
}

function suave(a: number, b: number, x: number): number {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
}

export function pressaoAmbienteKpa(altitudeM: number): number {
  return 101.325 * Math.pow(1 - 2.25577e-5 * altitudeM, 5.25588);
}

export function corteEfetivo(setup: Setup): number {
  return peca('ecu', setup.pecas.ecu).programavel ? setup.mapa.corte : 6500;
}

export function pressaoAlvoEfetiva(setup: Setup): number {
  const turbo = peca('turbo', setup.pecas.turbo);
  if (turbo.id === 'nenhum') return 0;
  const ecu = peca('ecu', setup.pecas.ecu);
  // Sem injeção programável, só a mola da wastegate (0,5 bar).
  const alvo = ecu.programavel ? setup.mapa.pressaoAlvo : 0.5;
  return Math.min(alvo, turbo.pressaoMax);
}

/**
 * Calcula um ponto de plena carga.
 * `pressaoForcada` permite à simulação de pista usar a pressão real (com atraso do turbo).
 */
export function pontoMotor(rpm: number, setup: Setup, cond: Condicoes, pressaoForcada?: number): Ponto {
  const { pecas, mapa } = setup;
  const comb = COMBUSTIVEIS[setup.combustivel];
  const ecu = peca('ecu', pecas.ecu);
  const turbo = peca('turbo', pecas.turbo);
  const comando = peca('comando', pecas.comando);
  const cabecote = peca('cabecote', pecas.cabecote);
  const pistoes = peca('pistoes', pecas.pistoes);
  const ic = peca('intercooler', pecas.intercooler);
  const bicos = peca('bicos', pecas.bicos);
  const bomba = peca('bomba', pecas.bomba);
  const escape = peca('escape', pecas.escape);

  const k = rpm / 1000;
  const pAmb = pressaoAmbienteKpa(cond.altitudeM);
  const tAmb = cond.temperaturaC + 273.15;
  const vd = CARRO.cilindradaL / 1000; // m³

  // Eficiência volumétrica
  let ve = comando.veMax - comando.curvatura * Math.pow((rpm - comando.rpmPico) / 1000, 2);
  ve = Math.max(0.42, ve) * cabecote.multVe;
  ve *= 1 + escape.ganhoAlto * suave(4000, 7000, rpm);
  const flutuacao = rpm > cabecote.rpmFlutuacao;
  if (flutuacao) ve *= Math.max(0.5, 1 - 0.28 * ((rpm - cabecote.rpmFlutuacao) / 1000));

  // Pressão do turbo
  const alvo = pressaoAlvoEfetiva(setup);
  let pressao = 0;
  if (turbo.id !== 'nenhum') {
    pressao = pressaoForcada ?? alvo * suave(turbo.enchimentoInicio, turbo.enchimentoCheio, rpm);
    pressao = Math.min(pressao, alvo);
  }
  let pressaoUtil = Math.max(0, pressao - ic.perdaCarga * (pressao / 1.5));

  // Temperatura de admissão
  const calcTemp = (p: number) => {
    const pr = (pAmb + p * 100) / pAmb;
    const tComp = tAmb * (1 + (Math.pow(pr, 0.2857) - 1) / 0.7);
    const efIc = turbo.id === 'nenhum' ? 1 : ic.eficiencia;
    return tComp - efIc * (tComp - tAmb) + 8 - comb.resfriamentoK;
  };
  let tAdm = calcTemp(pressao);
  let pMan = pAmb + pressaoUtil * 100;
  let massaAr = ve * ((pMan * 1000) / (287 * tAdm)) * (vd / 2) * (rpm / 60);

  // Turbo estrangulando: não consegue entregar mais vazão
  let turboNoLimite = false;
  if (turbo.id !== 'nenhum' && massaAr > turbo.vazaoMax && pressao > 0) {
    turboNoLimite = true;
    const escala = turbo.vazaoMax / massaAr;
    pMan = Math.max(pAmb, pMan * escala);
    pressaoUtil = (pMan - pAmb) / 100;
    pressao = pressaoUtil;
    tAdm = calcTemp(pressao) + 12; // compressor fora da faixa esquenta mais
    massaAr = ve * ((pMan * 1000) / (287 * tAdm)) * (vd / 2) * (rpm / 60);
  }

  // Mistura
  const lambdaAlvo = ecu.programavel ? interpMapa(mapa.lambda, rpm) : 0.86;
  const combNecessario = massaAr / (comb.afr * lambdaAlvo); // kg/s
  const capBicos = ((bicos.ccMin * 4 * 0.85) / 60) * 1e-3 * comb.densidade; // kg/s
  const capBomba = (bomba.litrosMin / 60) * comb.densidade * Math.max(0.3, 1 - 0.12 * pressaoUtil);
  const cap = Math.min(capBicos, capBomba);
  const combReal = Math.min(combNecessario, cap);
  const faltaCombustivel = combNecessario > cap * 1.0001;
  const lambda = massaAr / (comb.afr * combReal);
  const dutyBicos = combNecessario / capBicos;

  // Ignição
  const mbt = 21 + 1.6 * k - 3.2 * pressaoUtil;
  const limiteDetonacao =
    comb.baseDetonacao + 1.0 * k - 8 * pressaoUtil - 0.18 * (tAdm - 313) - 3.2 * (pistoes.taxa - 10) + 22 * (0.88 - lambda);
  const ignicao = ecu.programavel ? interpMapa(mapa.ignicao, rpm) : Math.min(mbt - 4, limiteDetonacao - 4);
  const detonacao = Math.max(0, ignicao - limiteDetonacao);

  // Potência
  const fIgn = Math.max(0.3, 1 - 0.0005 * Math.pow(ignicao - mbt, 2));
  const fDet = Math.max(0.6, 1 - 0.02 * detonacao);
  const fLam = lambda < 0.88 ? Math.max(0.4, 1 - 2.0 * Math.pow(lambda - 0.88, 2)) : Math.max(0.4, 1 - 0.5 * (lambda - 0.88));
  const queimado = Math.min(1, 1 / lambda);
  const eta = 0.37 * ((1 - Math.pow(pistoes.taxa, -0.28)) / (1 - Math.pow(10, -0.28)));
  const pInd = massaAr * comb.energiaMJporKgAr * 1e6 * queimado * eta * fIgn * fDet * fLam;
  const fmep = 0.5 + 0.1 * k + 0.03 * k * k + (pistoes.id === 'original' ? 0 : 0.04);
  const pAtrito = fmep * 1e5 * vd * (rpm / 120);
  const pFreio = Math.max(0, pInd - pAtrito);
  const omega = (2 * Math.PI * rpm) / 60;
  const torqueNm = pFreio / omega;
  const potenciaCv = (pFreio / 1000) * CV_POR_KW;

  const egtC =
    780 + 14 * Math.max(0, mbt - ignicao) - 5 * Math.max(0, ignicao - mbt) + 900 * Math.max(0, lambda - 0.88) + 45 * pressaoUtil + 0.012 * (rpm - 3000);

  return {
    rpm,
    torqueNm,
    potenciaCv,
    potenciaRodaCv: potenciaCv * (1 - CARRO.perdaTransmissao),
    pressao: pressaoUtil,
    pressaoAlvo: alvo,
    lambda,
    lambdaAlvo,
    ignicao,
    mbt,
    limiteDetonacao,
    detonacao,
    tempAdmissaoC: tAdm - 273.15,
    egtC,
    dutyBicos,
    faltaCombustivel,
    turboNoLimite,
    flutuacao,
  };
}

// ---------- Desgaste e quebra ----------

export type CausaQuebra = 'detonacao' | 'torque' | 'egt' | 'pobre' | 'flutuacao';

export const TEXTO_QUEBRA: Record<CausaQuebra, string> = {
  detonacao: 'Pistão furou por detonação',
  torque: 'Biela dobrou: torque além do que as peças aguentam',
  egt: 'Válvula de escape queimou: temperatura de escape alta demais',
  pobre: 'Pistão derreteu: mistura pobre sob pressão',
  flutuacao: 'Válvula bateu no pistão: giro além do que o cabeçote aguenta',
};

export interface Desgaste {
  total: number;
  porCausa: Record<CausaQuebra, number>;
}

export function desgasteNoPonto(p: Ponto, setup: Setup, dt: number): Desgaste {
  const pistoes = peca('pistoes', setup.pecas.pistoes);
  const porCausa: Record<CausaQuebra, number> = { detonacao: 0, torque: 0, egt: 0, pobre: 0, flutuacao: 0 };
  if (p.detonacao > 0) porCausa.detonacao = (Math.pow(p.detonacao, 1.8) * (1 + p.pressao) * 0.9 * dt) / pistoes.toleranciaDetonacao;
  if (p.torqueNm > pistoes.limiteTorqueNm) porCausa.torque = (p.torqueNm / pistoes.limiteTorqueNm - 1) * 90 * dt;
  if (p.egtC > 950) porCausa.egt = (p.egtC - 950) * 0.08 * dt;
  if (p.lambda > 0.95 && p.pressao > 0.2) porCausa.pobre = (p.lambda - 0.95) * 140 * p.pressao * dt;
  if (p.flutuacao) porCausa.flutuacao = 4 * dt;
  const total = Object.values(porCausa).reduce((a, b) => a + b, 0);
  return { total, porCausa };
}

// ---------- Mapa e setup padrão ----------

export function mapaPadrao(): Mapa {
  return {
    ignicao: [22, 24, 24, 25, 26, 27, 28, 28],
    lambda: [0.86, 0.86, 0.85, 0.85, 0.85, 0.86, 0.86, 0.86],
    pressaoAlvo: 0.5,
    corte: 6500,
  };
}

export function setupPadrao(): Setup {
  const pecas = {} as Pecas;
  (Object.keys(CATALOGO) as Categoria[]).forEach((c) => (pecas[c] = CATALOGO[c][0].id));
  return { pecas, mapa: mapaPadrao(), combustivel: 'etanol' };
}

export function custoPreparacao(setup: Setup): number {
  let total = COMBUSTIVEIS[setup.combustivel].preco;
  (Object.keys(setup.pecas) as Categoria[]).forEach((c) => (total += peca(c, setup.pecas[c]).preco));
  return total;
}
