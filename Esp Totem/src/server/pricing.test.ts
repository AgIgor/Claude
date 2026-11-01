import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { isCategoryOpen, minuteOfDay, type Catalog } from './catalog.ts';
import { AppError } from './errors.ts';
import { pizzaBasePrice, priceCart } from './pricing.ts';

const SIZE = '00000000-0000-4000-8000-000000000001';
const CALABRESA = '00000000-0000-4000-8000-000000000002';
const PORTUGUESA = '00000000-0000-4000-8000-000000000003';
const REFRI = '00000000-0000-4000-8000-000000000004';
const BORDA = '00000000-0000-4000-8000-000000000005';
const CHEDDAR = '00000000-0000-4000-8000-000000000006';
const REQUEIJAO = '00000000-0000-4000-8000-000000000007';
const GROUP = '00000000-0000-4000-8000-000000000008';
const CATEGORY = '00000000-0000-4000-8000-000000000009';

function catalog(): Catalog {
  const flavor = (id: string, name: string, price: number) => ({
    id,
    categoryId: CATEGORY,
    kind: 'pizza' as const,
    name,
    description: '',
    priceCents: null,
    available: true,
    pricesBySize: { [SIZE]: price },
    optionGroupIds: [GROUP],
  });
  return {
    categories: [{ id: CATEGORY, name: 'Pizzas', startMinute: null, endMinute: null }],
    sizes: [{ id: SIZE, name: 'Grande', slices: 8, maxFlavors: 2 }],
    optionGroups: [
      {
        id: GROUP,
        name: 'Borda',
        minSelect: 0,
        maxSelect: 1,
        options: [
          { id: CHEDDAR, name: 'Cheddar', priceCents: 900, available: true },
          { id: REQUEIJAO, name: 'Requeijão', priceCents: 900, available: false },
        ],
      },
    ],
    products: [
      flavor(CALABRESA, 'Calabresa', 6990),
      flavor(PORTUGUESA, 'Portuguesa', 7995),
      {
        id: REFRI,
        categoryId: CATEGORY,
        kind: 'simple',
        name: 'Refrigerante',
        description: '',
        priceCents: 790,
        available: true,
        pricesBySize: {},
        optionGroupIds: [],
      },
    ],
  };
}

const half = { type: 'pizza' as const, sizeId: SIZE, flavorIds: [CALABRESA, PORTUGUESA], quantity: 1 };

describe('preço de pizza', () => {
  it('meio a meio cobra o sabor mais caro na regra "highest"', () => {
    assert.equal(pizzaBasePrice([6990, 7995], 'highest'), 7995);
  });

  it('meio a meio arredonda a média para cima na regra "average"', () => {
    assert.equal(pizzaBasePrice([6990, 7995], 'average'), 7493);
  });

  it('soma borda, multiplica pela quantidade e nomeia os dois sabores', () => {
    const [item] = priceCart([{ ...half, quantity: 2, optionIds: [CHEDDAR] }], catalog(), 'highest', 720);
    assert.equal(item.unitPriceCents, 8895);
    assert.equal(item.totalCents, 17790);
    assert.equal(item.name, 'Pizza Grande: 1/2 Calabresa + 1/2 Portuguesa');
    assert.deepEqual(item.details.flavors, ['Calabresa', 'Portuguesa']);
  });
});

describe('validação do carrinho', () => {
  const rejects = (run: () => unknown, pattern: RegExp) =>
    assert.throws(run, (error: unknown) => error instanceof AppError && pattern.test(error.message));

  it('recusa mais sabores do que o tamanho permite', () => {
    const data = catalog();
    data.sizes[0].maxFlavors = 1;
    rejects(() => priceCart([half], data, 'highest', 720), /até 1 sabor/);
  });

  it('recusa pizza sem sabor e sabor repetido', () => {
    rejects(() => priceCart([{ ...half, flavorIds: [] }], catalog(), 'highest', 720), /ao menos um sabor/);
    rejects(() => priceCart([{ ...half, flavorIds: [CALABRESA, CALABRESA] }], catalog(), 'highest', 720), /repetido/);
  });

  it('recusa sabor e opção esgotados', () => {
    const data = catalog();
    data.products[0].available = false;
    rejects(() => priceCart([half], data, 'highest', 720), /Calabresa está esgotado/);
    rejects(() => priceCart([{ ...half, optionIds: [REQUEIJAO] }], catalog(), 'highest', 720), /Requeijão está esgotado/);
  });

  it('recusa acima do máximo do grupo e abaixo do mínimo', () => {
    const data = catalog();
    data.optionGroups[0].options[1].available = true;
    rejects(() => priceCart([{ ...half, optionIds: [CHEDDAR, REQUEIJAO] }], data, 'highest', 720), /Máximo de 1/);
    data.optionGroups[0].minSelect = 1;
    rejects(() => priceCart([half], data, 'highest', 720), /Escolha uma opção/);
  });

  it('recusa opção que não pertence ao produto', () => {
    const item = { type: 'simple' as const, productId: REFRI, quantity: 1, optionIds: [CHEDDAR] };
    rejects(() => priceCart([item], catalog(), 'highest', 720), /inválido/);
  });

  it('recusa categoria fora do horário', () => {
    const data = catalog();
    Object.assign(data.categories[0], { startMinute: 660, endMinute: 900 });
    const item = { type: 'simple' as const, productId: REFRI, quantity: 1 };
    rejects(() => priceCart([item], data, 'highest', 959), /neste horário/);
    assert.equal(priceCart([item], data, 'highest', 660)[0].totalCents, 790);
  });
});

describe('horário da loja', () => {
  it('converte para o fuso da loja', () => {
    assert.equal(minuteOfDay(new Date('2026-10-30T15:30:00Z'), 'America/Sao_Paulo'), 12 * 60 + 30);
  });

  it('a janela inclui o início e exclui o fim', () => {
    const category = { id: 'c', name: 'c', startMinute: 660, endMinute: 900 };
    assert.equal(isCategoryOpen(category, 660), true);
    assert.equal(isCategoryOpen(category, 900), false);
  });
});
