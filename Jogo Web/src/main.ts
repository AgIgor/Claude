import './style.css';
import { aoMudar, estado } from './estado.ts';
import { CARRO } from './sim/dados.ts';
import { custoPreparacao } from './sim/motor.ts';
import { renderClima, renderMapa, renderPecas } from './ui/paineis.ts';
import { aoClicarPuxar, atualizarTelaDino, estaAnimando, montarCanais, montarMostradores, redesenharGraficos } from './ui/telaDino.ts';
import { atualizarTelaPista, montarTelaPista } from './ui/telaPista.ts';
import { alternarSom, somLigado } from './ui/som.ts';

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const ICONE_SOM_LIGADO =
  '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M11 5 6 9H2v6h4l5 4V5z"/><path d="M15.5 8.5a5 5 0 0 1 0 7"/><path d="M19 5a10 10 0 0 1 0 14"/></svg>';
const ICONE_SOM_DESLIGADO =
  '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M11 5 6 9H2v6h4l5 4V5z"/><path d="m22 9-6 6"/><path d="m16 9 6 6"/></svg>';

function atualizarTopo(): void {
  $('carro-nome').textContent = CARRO.nome;
  $('carro-motor').textContent = `${CARRO.motor} · tração dianteira · ${CARRO.massaKg} kg`;
  const vida = Math.max(0, estado.vida);
  const barra = $('vida-preenche');
  barra.style.width = `${vida}%`;
  barra.style.background = vida > 60 ? 'var(--verde)' : vida > 25 ? 'var(--ambar)' : 'var(--vermelho)';
  $('vida-texto').textContent = vida <= 0 ? 'QUEBRADO' : `${vida.toFixed(0)}%`;
  $('custo-texto').textContent = `R$ ${custoPreparacao(estado.setup).toLocaleString('pt-BR')}`;
  const som = $('som');
  som.innerHTML = somLigado() ? ICONE_SOM_LIGADO : ICONE_SOM_DESLIGADO;
  som.setAttribute('aria-pressed', String(somLigado()));
}

function renderTudo(): void {
  atualizarTopo();
  renderPecas($('painel-pecas'));
  renderMapa($('painel-mapa'));
  renderClima($('painel-clima'));
  atualizarTelaDino();
  atualizarTelaPista();
}

// Abas principais
document.querySelectorAll<HTMLButtonElement>('.aba').forEach((b) => {
  b.addEventListener('click', () => {
    if (estaAnimando()) return;
    document.querySelectorAll('.aba').forEach((x) => x.classList.toggle('ativa', x === b));
    document.querySelectorAll('.tela').forEach((t) => t.classList.toggle('ativa', t.id === `tela-${b.dataset.tela}`));
    if (b.dataset.tela === 'dino') requestAnimationFrame(redesenharGraficos);
  });
});

// Abas da lateral
document.querySelectorAll<HTMLButtonElement>('.sub-aba').forEach((b) => {
  b.addEventListener('click', () => {
    document.querySelectorAll('.sub-aba').forEach((x) => x.classList.toggle('ativa', x === b));
    document.querySelectorAll('#tela-dino .painel').forEach((p) => p.classList.toggle('ativo', p.id === `painel-${b.dataset.painel}`));
  });
});

$('puxar').addEventListener('click', aoClicarPuxar);
$('som').addEventListener('click', () => {
  alternarSom();
  atualizarTopo();
});

let redimensionar = 0;
window.addEventListener('resize', () => {
  cancelAnimationFrame(redimensionar);
  redimensionar = requestAnimationFrame(redesenharGraficos);
});

montarMostradores();
montarCanais();
montarTelaPista($('tela-pista'));
aoMudar(renderTudo);
renderTudo();
