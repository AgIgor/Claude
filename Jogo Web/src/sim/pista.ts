// Simulação da arrancada usando o mesmo modelo de motor do dinamômetro.

import { CARRO } from './dados.ts';
import { corteEfetivo, peca, pontoMotor, pressaoAlvoEfetiva } from './motor.ts';
import type { Condicoes, Setup } from './motor.ts';
import { pressaoAmbienteKpa } from './motor.ts';

export interface ConfigPista {
  distancia: 201 | 402;
  rpmLargada: number;
  rpmTroca: number;
}

export interface Amostra {
  t: number;
  x: number;
  v: number; // m/s
  rpm: number;
  marcha: number;
  pressao: number;
  patinando: boolean;
}

export interface ResultadoPista {
  amostras: Amostra[];
  tempo60pes: number | null;
  tempo201: number | null;
  tempo402: number | null;
  tempoFinal: number;
  velFinalKmh: number;
  tempoPatinando: number;
}

const G = 9.81;
const PILOTO_KG = 75;
const TEMPO_TROCA = 0.22;
const TAU_TURBO = 0.35;

function suave(a: number, b: number, x: number): number {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
}

export function simularPista(setup: Setup, cond: Condicoes, cfg: ConfigPista): ResultadoPista {
  const turbo = peca('turbo', setup.pecas.turbo);
  const pneu = peca('pneu', setup.pecas.pneu);
  const dif = peca('diferencial', setup.pecas.diferencial);
  const corte = corteEfetivo(setup);
  const alvo = pressaoAlvoEfetiva(setup);
  const rpmTroca = Math.min(cfg.rpmTroca, corte - 50);
  const rpmLargada = Math.min(cfg.rpmLargada, corte - 200);

  const m = CARRO.massaKg + PILOTO_KG;
  const rho = (pressaoAmbienteKpa(cond.altitudeM) * 1000) / (287 * (cond.temperaturaC + 273.15));
  const r = CARRO.raioRodaM;
  const ultima = CARRO.marchas.length - 1;

  const pressaoEm = (rpm: number) => (turbo.id === 'nenhum' ? 0 : alvo * suave(turbo.enchimentoInicio, turbo.enchimentoCheio, rpm));

  const dt = 0.002;
  let t = 0, x = 0, v = 0, a = 0;
  let marcha = 0;
  let trocando = 0;
  let embreagem = true;
  let escorregamento = 0;
  let pressao = pressaoEm(rpmLargada) * 0.5; // um pouco de pressão segurando no freio
  let rpm = rpmLargada;
  let tempoPatinando = 0;
  let t60: number | null = null, t201: number | null = null, t402: number | null = null;
  const amostras: Amostra[] = [];
  let proxAmostra = 0;

  while (x < cfg.distancia && t < 40) {
    const relacao = CARRO.marchas[marcha] * dif.relacao;
    const rpmRoda = ((v / r) * 60 * relacao) / (2 * Math.PI);

    if (embreagem) {
      const rpmSoltando = Math.max(rpmLargada - 1800 * t, 1500);
      rpm = Math.max(rpmRoda * (1 + escorregamento), rpmSoltando);
      if (rpmRoda >= rpmSoltando) embreagem = false;
    } else {
      rpm = rpmRoda * (1 + escorregamento);
    }
    rpm = Math.min(Math.max(rpm, 1000), corte);

    const alvoAgora = trocando > 0 ? pressaoEm(rpm) * 0.6 : pressaoEm(rpm);
    pressao += ((alvoAgora - pressao) * dt) / TAU_TURBO;

    const p = pontoMotor(rpm, setup, cond, pressao);
    let torque = rpm >= corte - 1 ? 0 : p.torqueNm;
    if (trocando > 0) {
      torque = 0;
      trocando -= dt;
      if (trocando <= 0) marcha++;
    }

    const forca = (torque * relacao * (1 - CARRO.perdaTransmissao)) / r;
    const cargaDianteira = m * G * CARRO.pesoDianteiro - (m * a * CARRO.alturaCgM) / CARRO.entreEixosM;
    const forcaMax = pneu.atrito * Math.max(0, cargaDianteira);
    let forcaUtil = forca;
    let patinando = false;
    if (forca > forcaMax) {
      patinando = true;
      forcaUtil = forcaMax * 0.92;
      escorregamento = Math.min(0.6, escorregamento + ((forca - forcaMax) / Math.max(forcaMax, 1)) * dt * 4);
      tempoPatinando += dt;
    } else {
      escorregamento = Math.max(0, escorregamento - dt * 2.5);
    }

    const resistencia = 0.5 * rho * CARRO.cdA * v * v + 0.015 * m * G;
    a = (forcaUtil - resistencia) / m;
    v = Math.max(0, v + a * dt);
    x += v * dt;
    t += dt;

    if (trocando <= 0 && marcha < ultima && rpm >= rpmTroca && !embreagem) trocando = TEMPO_TROCA;

    if (t60 === null && x >= 18.288) t60 = t;
    if (t201 === null && x >= 201) t201 = t;
    if (t402 === null && x >= 402) t402 = t;

    if (t >= proxAmostra) {
      amostras.push({ t, x, v, rpm, marcha: marcha + 1, pressao, patinando });
      proxAmostra += 1 / 30;
    }
  }
  amostras.push({ t, x: Math.min(x, cfg.distancia), v, rpm, marcha: marcha + 1, pressao, patinando: false });

  return {
    amostras,
    tempo60pes: t60,
    tempo201: t201,
    tempo402: t402,
    tempoFinal: t,
    velFinalKmh: v * 3.6,
    tempoPatinando,
  };
}
