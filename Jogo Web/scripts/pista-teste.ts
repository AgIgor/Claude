import { simularPista } from '../src/sim/pista.ts';
import { setupPadrao } from '../src/sim/motor.ts';
const cond = { temperaturaC: 25, altitudeM: 760 };
const s = setupPadrao();
const fmt = (n: number | null) => (n === null ? '-' : n.toFixed(3));
for (const [nome, aj] of [
  ['original', () => {}],
  ['forjado 2 bar rua', () => { Object.assign(s.pecas, { turbo: 'medio', ecu: 'programavel', bicos: 'b1000', bomba: 'dupla', intercooler: 'frontal', pistoes: 'forjado85', comando: 'c272', cabecote: 'trabalhado', escape: 'e3' }); s.mapa.pressaoAlvo = 2.0; s.mapa.corte = 7800; s.mapa.ignicao = [20,20,19,19,20,21,21,21]; }],
  ['+ slick', () => { s.pecas.pneu = 'slick'; }],
] as [string, () => void][]) {
  aj();
  const r = simularPista(s, cond, { distancia: 402, rpmLargada: 4500, rpmTroca: 7300 });
  console.log(nome.padEnd(20), '60p', fmt(r.tempo60pes), '201', fmt(r.tempo201), '402', fmt(r.tempo402), 'vf', r.velFinalKmh.toFixed(0), 'patina', r.tempoPatinando.toFixed(2));
}
