// Ferramenta de calibração: roda puxadas com alguns setups e imprime os resultados.
// Uso: node scripts/calibrar.ts
import { puxar, diagnosticar } from '../src/sim/dino.ts';
import { setupPadrao } from '../src/sim/motor.ts';
import type { Setup } from '../src/sim/motor.ts';

const cond = { temperaturaC: 25, altitudeM: 760 };

function teste(nome: string, ajuste: (s: Setup) => void) {
  const s = setupPadrao();
  ajuste(s);
  const px = puxar(1, s, cond, 100);
  console.log(
    `${nome.padEnd(34)} ${px.picoCvRoda.toFixed(0).padStart(4)} cv roda @${px.rpmPicoCv}  ` +
      `${(px.picoTorqueNm * 0.102).toFixed(1)} kgfm @${px.rpmPicoTorque}  ` +
      `pressão ${px.pressaoMax.toFixed(2)}  desgaste ${px.desgaste.toFixed(1)}${px.quebra ? '  QUEBROU ' + px.quebra.texto : ''}`,
  );
  for (const o of diagnosticar(px, s)) console.log(`    [${o.gravidade}] ${o.texto}`);
}

teste('Original etanol', () => {});
teste('Original gasolina', (s) => (s.combustivel = 'gasolina'));
teste('Turbo médio, ECU original', (s) => (s.pecas.turbo = 'medio'));
teste('Turbo médio 1.0 bar, ECU prog', (s) => {
  s.pecas.turbo = 'medio'; s.pecas.ecu = 'programavel'; s.mapa.pressaoAlvo = 1.0;
});
teste('Turbo médio 1.0, bicos/bomba/IC', (s) => {
  s.pecas.turbo = 'medio'; s.pecas.ecu = 'programavel'; s.mapa.pressaoAlvo = 1.0;
  s.pecas.bicos = 'b650'; s.pecas.bomba = 'externa'; s.pecas.intercooler = 'frontal';
});
teste('Turbo médio 1.0 + ign ajustada', (s) => {
  s.pecas.turbo = 'medio'; s.pecas.ecu = 'programavel'; s.mapa.pressaoAlvo = 1.0;
  s.pecas.bicos = 'b650'; s.pecas.bomba = 'externa'; s.pecas.intercooler = 'frontal';
  s.mapa.ignicao = [24, 26, 22, 22, 23, 24, 25, 25];
});
teste('Forjado 8.5, 2.0 bar, completo', (s) => {
  Object.assign(s.pecas, { turbo: 'medio', ecu: 'programavel', bicos: 'b1000', bomba: 'dupla', intercooler: 'frontal', pistoes: 'forjado85', comando: 'c272', cabecote: 'trabalhado', escape: 'e3' });
  s.mapa.pressaoAlvo = 2.0; s.mapa.corte = 7800;
  s.mapa.ignicao = [20, 20, 16, 16, 17, 18, 19, 19];
});
teste('Turbo grande 2.8 metanol', (s) => {
  Object.assign(s.pecas, { turbo: 'grande', ecu: 'programavel', bicos: 'b1000', bomba: 'dupla', intercooler: 'frontal', pistoes: 'forjado85', comando: 'c290', cabecote: 'trabalhado', escape: 'e3' });
  s.combustivel = 'metanol';
  s.mapa.pressaoAlvo = 2.8; s.mapa.corte = 8500;
  s.mapa.ignicao = [22, 22, 18, 16, 17, 18, 19, 19];
  s.mapa.lambda = [0.8, 0.8, 0.78, 0.78, 0.78, 0.78, 0.8, 0.8];
});
teste('Aspirado bravo 12:1 etanol', (s) => {
  Object.assign(s.pecas, { ecu: 'programavel', pistoes: 'forjado12', comando: 'c290', cabecote: 'trabalhado', escape: 'e3', bicos: 'b440' });
  s.mapa.corte = 8200;
  s.mapa.ignicao = [26, 28, 30, 31, 32, 33, 34, 34];
});
