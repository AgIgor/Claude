# Arrancada 402 — protótipo

Jogo web de preparação de carros nacionais. O **dinamômetro** é a atração principal; a arrancada usa a curva do motor que você montou.

## Rodar

```
npm install
npm run dev
```

Abra http://localhost:5173.

## Estrutura

- `src/sim/dados.ts`: carro, combustíveis e catálogo de peças (números de jogo, não de fábrica).
- `src/sim/motor.ts`: simulação do motor em plena carga (fluxo de ar, turbo, mistura, ignição, detonação, desgaste).
- `src/sim/dino.ts`: puxada no dinamômetro e diagnóstico do preparador.
- `src/sim/pista.ts`: simulação da arrancada (tração dianteira, transferência de peso, trocas, atraso do turbo).
- `src/ui/`: telas (dinamômetro, pista), gráficos em canvas e som sintetizado.
- `scripts/calibrar.ts` e `scripts/pista-teste.ts`: rodam setups de teste no terminal (`node scripts/calibrar.ts`).
