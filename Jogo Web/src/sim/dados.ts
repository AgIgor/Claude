// Catálogo do protótipo: carro, combustíveis e peças.
// Os números são calibrados para "parecer certo" no jogo, não são dados de fábrica.

export interface Carro {
  nome: string;
  motor: string;
  cilindradaL: number;
  massaKg: number;
  pesoDianteiro: number; // fração do peso no eixo dianteiro
  entreEixosM: number;
  alturaCgM: number;
  raioRodaM: number;
  marchas: number[];
  cdA: number;
  perdaTransmissao: number; // fração perdida entre motor e roda
}

export const CARRO: Carro = {
  nome: 'Golzinho Quadrado',
  motor: 'AP 1.8 8v',
  cilindradaL: 1.781,
  massaKg: 940,
  pesoDianteiro: 0.62,
  entreEixosM: 2.36,
  alturaCgM: 0.52,
  raioRodaM: 0.29,
  marchas: [3.45, 1.94, 1.29, 0.97, 0.8],
  cdA: 0.78,
  perdaTransmissao: 0.15,
};

export type CombustivelId = 'gasolina' | 'etanol' | 'metanol';

export interface Combustivel {
  id: CombustivelId;
  nome: string;
  afr: number; // relação ar/combustível estequiométrica
  energiaMJporKgAr: number; // energia liberada por kg de ar na estequiometria
  baseDetonacao: number; // graus de avanço suportados antes de detonar (referência)
  resfriamentoK: number; // quanto a evaporação esfria a admissão
  densidade: number; // kg/L
  preco: number;
}

export const COMBUSTIVEIS: Record<CombustivelId, Combustivel> = {
  gasolina: { id: 'gasolina', nome: 'Gasolina comum', afr: 13.2, energiaMJporKgAr: 2.92, baseDetonacao: 25, resfriamentoK: 5, densidade: 0.75, preco: 0 },
  etanol: { id: 'etanol', nome: 'Etanol', afr: 8.4, energiaMJporKgAr: 2.98, baseDetonacao: 34, resfriamentoK: 16, densidade: 0.81, preco: 0 },
  metanol: { id: 'metanol', nome: 'Metanol', afr: 6.4, energiaMJporKgAr: 3.09, baseDetonacao: 38, resfriamentoK: 26, densidade: 0.79, preco: 900 },
};

export interface Opcao {
  id: string;
  nome: string;
  desc: string;
  preco: number;
}

export interface Turbo extends Opcao {
  pressaoMax: number; // bar
  enchimentoInicio: number; // rpm
  enchimentoCheio: number; // rpm
  vazaoMax: number; // kg/s de ar
}
export interface Comando extends Opcao { veMax: number; rpmPico: number; curvatura: number }
export interface Cabecote extends Opcao { multVe: number; rpmFlutuacao: number }
export interface Pistoes extends Opcao { taxa: number; limiteTorqueNm: number; toleranciaDetonacao: number }
export interface Intercooler extends Opcao { eficiencia: number; perdaCarga: number }
export interface Bicos extends Opcao { ccMin: number }
export interface Bomba extends Opcao { litrosMin: number }
export interface Ecu extends Opcao { programavel: boolean }
export interface Escape extends Opcao { ganhoAlto: number }
export interface Pneu extends Opcao { atrito: number }
export interface Diferencial extends Opcao { relacao: number }

export interface Catalogo {
  turbo: Turbo[];
  comando: Comando[];
  cabecote: Cabecote[];
  pistoes: Pistoes[];
  intercooler: Intercooler[];
  bicos: Bicos[];
  bomba: Bomba[];
  ecu: Ecu[];
  escape: Escape[];
  pneu: Pneu[];
  diferencial: Diferencial[];
}

export type Categoria = keyof Catalogo;

export const CATALOGO: Catalogo = {
  ecu: [
    { id: 'original', nome: 'Injeção original', desc: 'Mapa fixo de fábrica, conservador. Não deixa mexer em nada.', preco: 0, programavel: false },
    { id: 'programavel', nome: 'Injeção programável', desc: 'Libera o mapa de ignição e mistura, pressão do turbo e corte de giro.', preco: 3800, programavel: true },
  ],
  turbo: [
    { id: 'nenhum', nome: 'Aspirado', desc: 'Sem turbo.', preco: 0, pressaoMax: 0, enchimentoInicio: 0, enchimentoCheio: 0, vazaoMax: 0 },
    { id: 'pequeno', nome: 'Turbo .42/.48', desc: 'Enche cedo, mas estrangula em alta rotação.', preco: 2900, pressaoMax: 1.6, enchimentoInicio: 2300, enchimentoCheio: 3700, vazaoMax: 0.17 },
    { id: 'medio', nome: 'Turbo .50/.63', desc: 'Equilibrado. O mais usado em AP de rua.', preco: 4600, pressaoMax: 2.3, enchimentoInicio: 3100, enchimentoCheio: 4700, vazaoMax: 0.26 },
    { id: 'grande', nome: 'Turbo .70/.82', desc: 'Muito fôlego em cima, mas demora para encher.', preco: 7400, pressaoMax: 3.0, enchimentoInicio: 4000, enchimentoCheio: 5900, vazaoMax: 0.36 },
  ],
  comando: [
    { id: 'original', nome: 'Comando original', desc: 'Bom torque embaixo, cansa cedo.', preco: 0, veMax: 0.88, rpmPico: 4200, curvatura: 0.018 },
    { id: 'c272', nome: 'Comando 272°', desc: 'Mais fôlego no meio e em cima.', preco: 1300, veMax: 0.95, rpmPico: 5600, curvatura: 0.021 },
    { id: 'c290', nome: 'Comando 290°', desc: 'Bravo. Fraco embaixo, forte acima de 6.000 rpm.', preco: 1900, veMax: 1.0, rpmPico: 6800, curvatura: 0.024 },
  ],
  cabecote: [
    { id: 'original', nome: 'Cabeçote original', desc: 'Válvulas flutuam acima de 7.000 rpm.', preco: 0, multVe: 1.0, rpmFlutuacao: 7000 },
    { id: 'trabalhado', nome: 'Cabeçote trabalhado + molas', desc: 'Dutos polidos e molas duplas. Gira até 8.600 rpm.', preco: 3500, multVe: 1.07, rpmFlutuacao: 8600 },
  ],
  pistoes: [
    { id: 'original', nome: 'Pistões e bielas originais (10:1)', desc: 'Aguentam pouco torque e pouca detonação.', preco: 0, taxa: 10, limiteTorqueNm: 240, toleranciaDetonacao: 1 },
    { id: 'forjado85', nome: 'Forjados taxa 8,5:1', desc: 'Para turbo. Taxa baixa aceita mais pressão.', preco: 5200, taxa: 8.5, limiteTorqueNm: 560, toleranciaDetonacao: 2.2 },
    { id: 'forjado12', nome: 'Forjados taxa 12:1', desc: 'Para aspirado no etanol ou metanol.', preco: 5200, taxa: 12, limiteTorqueNm: 520, toleranciaDetonacao: 2.2 },
  ],
  intercooler: [
    { id: 'nenhum', nome: 'Sem intercooler', desc: 'Ar quente na admissão.', preco: 0, eficiencia: 0.15, perdaCarga: 0 },
    { id: 'pequeno', nome: 'Intercooler ar-ar pequeno', desc: 'Esfria bem até uns 300 cv.', preco: 1100, eficiencia: 0.62, perdaCarga: 0.04 },
    { id: 'frontal', nome: 'Intercooler frontal 600 mm', desc: 'Esfria muito, perde um pouco de pressão.', preco: 2300, eficiencia: 0.82, perdaCarga: 0.07 },
  ],
  bicos: [
    { id: 'original', nome: 'Bicos originais 260 cc', desc: 'Suficientes para o motor de fábrica.', preco: 0, ccMin: 260 },
    { id: 'b440', nome: 'Bicos 440 cc', desc: '', preco: 900, ccMin: 440 },
    { id: 'b650', nome: 'Bicos 650 cc', desc: '', preco: 1400, ccMin: 650 },
    { id: 'b1000', nome: 'Bicos 1.000 cc', desc: 'Para muita potência no etanol ou metanol.', preco: 2100, ccMin: 1000 },
  ],
  bomba: [
    { id: 'original', nome: 'Bomba original', desc: '1,6 L/min. Perde vazão com pressão.', preco: 0, litrosMin: 1.6 },
    { id: 'externa', nome: 'Bomba externa', desc: '3,0 L/min.', preco: 800, litrosMin: 3.0 },
    { id: 'dupla', nome: 'Bomba dupla', desc: '5,0 L/min.', preco: 1700, litrosMin: 5.0 },
  ],
  escape: [
    { id: 'original', nome: 'Escape original', desc: '', preco: 0, ganhoAlto: 0 },
    { id: 'e25', nome: 'Escape 2,5"', desc: 'Respira melhor em alta.', preco: 700, ganhoAlto: 0.03 },
    { id: 'e3', nome: 'Escape 3" direto', desc: 'Respira muito melhor em alta.', preco: 1300, ganhoAlto: 0.055 },
  ],
  pneu: [
    { id: 'original', nome: 'Pneu de rua', desc: '', preco: 0, atrito: 1.1 },
    { id: 'dragradial', nome: 'Drag radial', desc: 'Muito mais tração na largada.', preco: 1600, atrito: 1.4 },
    { id: 'slick', nome: 'Slick dianteiro', desc: 'Máxima tração.', preco: 2600, atrito: 1.7 },
  ],
  diferencial: [
    { id: 'original', nome: 'Diferencial 4,11', desc: 'Relação final original.', preco: 0, relacao: 4.11 },
    { id: 'curto', nome: 'Diferencial 4,50 (curto)', desc: 'Acelera mais, chega antes no corte.', preco: 1200, relacao: 4.5 },
    { id: 'longo', nome: 'Diferencial 3,67 (longo)', desc: 'Para muita potência e 402 m.', preco: 1200, relacao: 3.67 },
  ],
};

export const NOMES_CATEGORIA: Record<Categoria, string> = {
  ecu: 'Injeção',
  turbo: 'Turbo',
  comando: 'Comando',
  cabecote: 'Cabeçote',
  pistoes: 'Pistões e bielas',
  intercooler: 'Intercooler',
  bicos: 'Bicos injetores',
  bomba: 'Bomba de combustível',
  escape: 'Escape',
  pneu: 'Pneus',
  diferencial: 'Diferencial',
};

export const ORDEM_CATEGORIAS: Categoria[] = [
  'ecu', 'turbo', 'intercooler', 'comando', 'cabecote', 'pistoes', 'bicos', 'bomba', 'escape', 'pneu', 'diferencial',
];

// Colunas do mapa de plena carga (rpm)
export const RPM_MAPA = [2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000];

export const ALTITUDES = [
  { id: 'mar', nome: 'Nível do mar (Santos)', metros: 0 },
  { id: 'sp', nome: 'São Paulo (760 m)', metros: 760 },
  { id: 'cwb', nome: 'Curitiba (930 m)', metros: 930 },
  { id: 'bsb', nome: 'Brasília (1.170 m)', metros: 1170 },
];
