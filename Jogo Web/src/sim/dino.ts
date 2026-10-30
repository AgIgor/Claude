// Puxada no dinamômetro: varre de 2.000 rpm até o corte, acumula desgaste e gera diagnóstico.

import { corteEfetivo, desgasteNoPonto, peca, pontoMotor, TEXTO_QUEBRA } from './motor.ts';
import type { CausaQuebra, Condicoes, Ponto, Setup } from './motor.ts';

export const RPM_INICIO = 2000;
export const PASSO_RPM = 50;
export const RPM_POR_SEGUNDO = 1100; // velocidade da varredura

export interface Puxada {
  id: number;
  pontos: Ponto[];
  quebra: { rpm: number; causa: CausaQuebra; texto: string } | null;
  desgaste: number; // pontos de vida consumidos
  picoCvRoda: number;
  rpmPicoCv: number;
  picoTorqueNm: number;
  rpmPicoTorque: number;
  pressaoMax: number;
  setupResumo: string;
  setup: Setup;
}

export function puxar(id: number, setup: Setup, cond: Condicoes, vidaMotor: number): Puxada {
  const corte = corteEfetivo(setup);
  const dt = PASSO_RPM / RPM_POR_SEGUNDO;
  const pontos: Ponto[] = [];
  const acumulado: Record<CausaQuebra, number> = { detonacao: 0, torque: 0, egt: 0, pobre: 0, flutuacao: 0 };
  let vida = vidaMotor;
  let desgaste = 0;
  let quebra: Puxada['quebra'] = null;

  for (let rpm = RPM_INICIO; rpm <= corte; rpm += PASSO_RPM) {
    const p = pontoMotor(rpm, setup, cond);
    pontos.push(p);
    const d = desgasteNoPonto(p, setup, dt);
    (Object.keys(d.porCausa) as CausaQuebra[]).forEach((c) => (acumulado[c] += d.porCausa[c]));
    desgaste += d.total;
    vida -= d.total;
    // Detonação muito forte sob pressão quebra na hora
    const catastrofe = p.detonacao > 9 && p.pressao > 0.6;
    if (vida <= 0 || catastrofe) {
      const causa = catastrofe
        ? 'detonacao'
        : (Object.keys(acumulado) as CausaQuebra[]).reduce((a, b) => (acumulado[a] >= acumulado[b] ? a : b));
      quebra = { rpm, causa, texto: TEXTO_QUEBRA[causa] };
      desgaste = vidaMotor;
      break;
    }
  }

  let picoCvRoda = 0, rpmPicoCv = 0, picoTorqueNm = 0, rpmPicoTorque = 0, pressaoMax = 0;
  for (const p of pontos) {
    if (p.potenciaRodaCv > picoCvRoda) { picoCvRoda = p.potenciaRodaCv; rpmPicoCv = p.rpm; }
    if (p.torqueNm > picoTorqueNm) { picoTorqueNm = p.torqueNm; rpmPicoTorque = p.rpm; }
    pressaoMax = Math.max(pressaoMax, p.pressao);
  }

  const turbo = peca('turbo', setup.pecas.turbo);
  const setupResumo = `${turbo.id === 'nenhum' ? 'Aspirado' : turbo.nome} · ${setup.combustivel}`;

  return { id, pontos, quebra, desgaste, picoCvRoda, rpmPicoCv, picoTorqueNm, rpmPicoTorque, pressaoMax, setupResumo, setup: structuredClone(setup) };
}

// ---------- Diagnóstico do preparador ----------

export type Gravidade = 'perigo' | 'atencao' | 'dica' | 'ok';
export interface Observacao { gravidade: Gravidade; texto: string }

function faixa(pontos: Ponto[], cond: (p: Ponto) => boolean): [number, number] | null {
  const sel = pontos.filter(cond);
  if (sel.length === 0) return null;
  return [sel[0].rpm, sel[sel.length - 1].rpm];
}

const fmtRpm = (r: number) => r.toLocaleString('pt-BR');
const fmtFaixa = (f: [number, number]) => (f[0] === f[1] ? `${fmtRpm(f[0])} rpm` : `${fmtRpm(f[0])}–${fmtRpm(f[1])} rpm`);

export function diagnosticar(px: Puxada, setup: Setup): Observacao[] {
  const obs: Observacao[] = [];
  const { pontos } = px;
  const ecu = peca('ecu', setup.pecas.ecu);
  const turbo = peca('turbo', setup.pecas.turbo);
  const pistoes = peca('pistoes', setup.pecas.pistoes);

  if (px.quebra) obs.push({ gravidade: 'perigo', texto: `QUEBROU a ${fmtRpm(px.quebra.rpm)} rpm: ${px.quebra.texto}.` });

  const det = faixa(pontos, (p) => p.detonacao > 0.3);
  if (det) {
    const maxDet = Math.max(...pontos.map((p) => p.detonacao));
    obs.push({
      gravidade: maxDet > 3 ? 'perigo' : 'atencao',
      texto: `Detonação em ${fmtFaixa(det)} (até ${maxDet.toFixed(1)}°). Tire avanço nessa faixa, enriqueça a mistura ou baixe a pressão.`,
    });
  }

  const pobre = faixa(pontos, (p) => p.faltaCombustivel);
  if (pobre) {
    const maxDuty = Math.max(...pontos.map((p) => p.dutyBicos));
    const culpado = maxDuty > 1 ? `os bicos chegaram a ${(maxDuty * 100).toFixed(0)}% de abertura` : 'a bomba não deu conta da vazão';
    obs.push({ gravidade: 'perigo', texto: `Faltou combustível em ${fmtFaixa(pobre)}: ${culpado}. A mistura empobreceu.` });
  }

  const egt = faixa(pontos, (p) => p.egtC > 950);
  if (egt) obs.push({ gravidade: 'atencao', texto: `Temperatura de escape acima de 950 °C em ${fmtFaixa(egt)}. Ignição atrasada demais ou mistura pobre.` });

  const torqueMax = Math.max(...pontos.map((p) => p.torqueNm));
  if (torqueMax > pistoes.limiteTorqueNm * 0.92) {
    obs.push({
      gravidade: torqueMax > pistoes.limiteTorqueNm ? 'perigo' : 'atencao',
      texto: `Torque de ${(torqueMax * 0.101972).toFixed(1)} kgfm está ${torqueMax > pistoes.limiteTorqueNm ? 'acima' : 'perto'} do limite das bielas (${(pistoes.limiteTorqueNm * 0.101972).toFixed(0)} kgfm).`,
    });
  }

  const estrangulado = faixa(pontos, (p) => p.turboNoLimite);
  if (estrangulado) obs.push({ gravidade: 'dica', texto: `O turbo ficou sem fôlego a partir de ${fmtRpm(estrangulado[0])} rpm: a pressão cai. Um turbo maior resolveria.` });

  const flut = faixa(pontos, (p) => p.flutuacao);
  if (flut) obs.push({ gravidade: 'atencao', texto: `Válvulas flutuando acima de ${fmtRpm(flut[0])} rpm. Baixe o corte ou trabalhe o cabeçote.` });

  const iat = Math.max(...pontos.map((p) => p.tempAdmissaoC));
  if (iat > 70) obs.push({ gravidade: 'dica', texto: `Ar de admissão chegou a ${iat.toFixed(0)} °C. Ar quente rende menos e detona mais: um intercooler ajudaria.` });

  if (ecu.programavel) {
    // Dica de ignição: muito atrasado em relação ao melhor ponto, sem detonar
    const atrasado = faixa(pontos, (p) => p.mbt - p.ignicao > 5 && p.limiteDetonacao - p.ignicao > 3);
    if (atrasado) obs.push({ gravidade: 'dica', texto: `Ignição parece conservadora em ${fmtFaixa(atrasado)}: dá para adiantar com cuidado.` });
    const rico = faixa(pontos, (p) => p.lambda < 0.78);
    if (rico) obs.push({ gravidade: 'dica', texto: `Mistura muito rica em ${fmtFaixa(rico)}: está jogando potência fora.` });
  } else {
    obs.push({ gravidade: 'dica', texto: 'Injeção original: o mapa é fixo e conservador. Uma injeção programável libera o acerto.' });
    if (turbo.id !== 'nenhum') obs.push({ gravidade: 'dica', texto: 'Sem injeção programável, o turbo fica limitado a 0,5 bar pela mola da wastegate.' });
  }

  if (obs.length === 0 || obs.every((o) => o.gravidade === 'dica')) {
    if (!px.quebra && !det && !pobre) obs.unshift({ gravidade: 'ok', texto: 'Puxada limpa: sem detonação e mistura dentro do alvo.' });
  }
  return obs;
}
