import { hashPassword, randomQrToken } from './auth.ts';
import { one, type Db, type Queryable } from './db.ts';

/** Dados inteiramente ficticios, usados para demonstracao e testes. */
export const DEMO_SLUG = 'forno-demo';

const reais = (value: number) => Math.round(value * 100);

const SIZES = [
  { name: 'Pequena', slices: 4, maxFlavors: 1 },
  { name: 'Média', slices: 6, maxFlavors: 2 },
  { name: 'Grande', slices: 8, maxFlavors: 2 },
  { name: 'Família', slices: 12, maxFlavors: 3 },
];

// Preco por tamanho, na ordem de SIZES.
const TRADICIONAL = [39.9, 54.9, 69.9, 89.9];
const ESPECIAL = [46.9, 62.9, 79.9, 99.9];
const DOCE = [42.9, 56.9, 72.9, 92.9];

const PIZZA_CATEGORIES = [
  {
    name: 'Pizzas tradicionais',
    flavors: [
      ['Margherita', 'Molho de tomate, muçarela, manjericão fresco e azeite.', TRADICIONAL],
      ['Calabresa', 'Muçarela, calabresa fatiada, cebola e orégano.', TRADICIONAL],
      ['Muçarela', 'Dupla camada de muçarela, tomate e orégano.', TRADICIONAL],
      ['Napolitana', 'Muçarela, tomate, parmesão e alho.', TRADICIONAL],
    ],
  },
  {
    name: 'Pizzas especiais',
    flavors: [
      ['Portuguesa', 'Muçarela, presunto, ovo, cebola, ervilha e azeitona.', ESPECIAL],
      ['Frango com Requeijão', 'Frango desfiado temperado, requeijão cremoso e milho.', ESPECIAL],
      ['Quatro Queijos', 'Muçarela, provolone, parmesão e gorgonzola.', ESPECIAL],
      ['Pepperoni', 'Muçarela, pepperoni fatiado e pimenta calabresa.', ESPECIAL],
    ],
  },
  {
    name: 'Pizzas doces',
    flavors: [
      ['Chocolate com Morango', 'Creme de chocolate ao leite e morangos frescos.', DOCE],
      ['Banana com Canela', 'Banana caramelizada, açúcar e canela.', DOCE],
      ['Romeu e Julieta', 'Muçarela e goiabada cremosa.', DOCE],
    ],
  },
] as const;

const SIMPLE_CATEGORIES = [
  {
    name: 'Promoção de almoço',
    startMinute: 11 * 60,
    endMinute: 15 * 60,
    products: [
      ['Mini Pizza + Refrigerante', 'Mini pizza de um sabor do dia e refrigerante lata.', 26.9],
    ],
  },
  {
    name: 'Combos',
    products: [
      ['Combo Casal', 'Pizza média de um sabor tradicional e refrigerante 2 L.', 69.9, 'combo'],
      ['Combo Família', 'Pizza família de um sabor tradicional e refrigerante 2 L.', 104.9, 'combo'],
    ],
  },
  {
    name: 'Entradas',
    products: [
      ['Pão de Alho', 'Seis fatias gratinadas com muçarela.', 22.9],
      ['Bolinho de Queijo', 'Porção com dez unidades.', 29.9],
      ['Batata Frita', 'Porção de 400 g com molho da casa.', 27.9],
    ],
  },
  {
    name: 'Bebidas',
    products: [
      ['Refrigerante Cola 2 L', 'Garrafa de 2 litros.', 15.9],
      ['Guaraná 2 L', 'Garrafa de 2 litros.', 14.9],
      ['Refrigerante Lata', 'Lata de 350 ml.', 7.9],
      ['Suco de Laranja', 'Natural, 500 ml.', 12.9],
      ['Água Mineral', 'Garrafa de 500 ml, com ou sem gás.', 5.9],
    ],
  },
  {
    name: 'Sobremesas',
    products: [
      ['Pudim de Leite', 'Fatia individual.', 13.9],
      ['Petit Gâteau', 'Bolo quente de chocolate com sorvete de creme.', 21.9],
    ],
  },
] as const;

interface GroupSeed {
  name: string;
  minSelect: number;
  maxSelect: number;
  options: (readonly [string, number])[];
}

const GROUPS: Record<'borda' | 'adicionais' | 'saborCombo' | 'refriCombo', GroupSeed> = {
  borda: {
    name: 'Borda recheada',
    minSelect: 0,
    maxSelect: 1,
    options: [['Requeijão', 9], ['Cheddar', 9], ['Chocolate', 11]],
  },
  adicionais: {
    name: 'Adicionais',
    minSelect: 0,
    maxSelect: 3,
    options: [['Bacon', 6], ['Ovo', 3], ['Azeitona', 3], ['Cebola caramelizada', 5]],
  },
  saborCombo: {
    name: 'Sabor da pizza',
    minSelect: 1,
    maxSelect: 1,
    options: [['Margherita', 0], ['Calabresa', 0], ['Muçarela', 0], ['Napolitana', 0]],
  },
  refriCombo: {
    name: 'Refrigerante',
    minSelect: 1,
    maxSelect: 1,
    options: [['Cola 2 L', 0], ['Guaraná 2 L', 0]],
  },
};

async function insertId(db: Queryable, sql: string, params: unknown[]): Promise<string> {
  const row = await one<{ id: string }>(db, `${sql} RETURNING id`, params);
  return row!.id;
}

export async function seedDemoStore(db: Db, password: string): Promise<string> {
  const existing = await one<{ id: string }>(db, 'SELECT id FROM stores WHERE slug = $1', [DEMO_SLUG]);
  if (existing) return existing.id;

  return db.transaction(async (tx) => {
    let sort = 0;

    const storeId = await insertId(
      tx,
      `INSERT INTO stores (slug, name, half_pizza_rule, service_percent) VALUES ($1, $2, 'highest', 10)`,
      [DEMO_SLUG, 'Pizzaria Forno Demo'],
    );

    for (const [login, name, role] of [
      ['admin', 'Administrador', 'admin'],
      ['caixa', 'Operador de caixa', 'staff'],
    ]) {
      await tx.query('INSERT INTO users (store_id, name, login, password_hash, role) VALUES ($1, $2, $3, $4, $5)', [
        storeId,
        name,
        login,
        hashPassword(password),
        role,
      ]);
    }

    const sizeIds: string[] = [];
    for (const [index, size] of SIZES.entries()) {
      sizeIds.push(
        await insertId(
          tx,
          'INSERT INTO pizza_sizes (store_id, name, slices, max_flavors, sort) VALUES ($1, $2, $3, $4, $5)',
          [storeId, size.name, size.slices, size.maxFlavors, index],
        ),
      );
    }

    const groupIds = {} as Record<keyof typeof GROUPS, string>;
    for (const [index, [key, group]] of Object.entries(GROUPS).entries()) {
      const groupId = await insertId(
        tx,
        'INSERT INTO option_groups (store_id, name, min_select, max_select, sort) VALUES ($1, $2, $3, $4, $5)',
        [storeId, group.name, group.minSelect, group.maxSelect, index],
      );
      groupIds[key as keyof typeof GROUPS] = groupId;
      for (const [optionIndex, [name, price]] of group.options.entries()) {
        await tx.query(
          'INSERT INTO options (group_id, name, price_cents, sort) VALUES ($1, $2, $3, $4)',
          [groupId, name, reais(price), optionIndex],
        );
      }
    }

    const insertCategory = (name: string, startMinute: number | null = null, endMinute: number | null = null) =>
      insertId(
        tx,
        'INSERT INTO categories (store_id, name, sort, start_minute, end_minute) VALUES ($1, $2, $3, $4, $5)',
        [storeId, name, sort++, startMinute, endMinute],
      );

    for (const category of PIZZA_CATEGORIES) {
      const categoryId = await insertCategory(category.name);
      const isSweet = category.name === 'Pizzas doces';
      for (const [name, description, prices] of category.flavors) {
        const productId = await insertId(
          tx,
          `INSERT INTO products (store_id, category_id, kind, name, description, sort)
           VALUES ($1, $2, 'pizza', $3, $4, $5)`,
          [storeId, categoryId, name, description, sort++],
        );
        for (const [index, price] of prices.entries()) {
          await tx.query('INSERT INTO pizza_prices (product_id, size_id, price_cents) VALUES ($1, $2, $3)', [
            productId,
            sizeIds[index],
            reais(price),
          ]);
        }
        const groups = isSweet ? [groupIds.borda] : [groupIds.borda, groupIds.adicionais];
        for (const groupId of groups) {
          await tx.query('INSERT INTO product_option_groups (product_id, group_id) VALUES ($1, $2)', [
            productId,
            groupId,
          ]);
        }
      }
    }

    for (const category of SIMPLE_CATEGORIES) {
      const window = category as { startMinute?: number; endMinute?: number };
      const categoryId = await insertCategory(category.name, window.startMinute, window.endMinute);
      for (const [name, description, price, type] of category.products as readonly (readonly [
        string,
        string,
        number,
        string?,
      ])[]) {
        const productId = await insertId(
          tx,
          `INSERT INTO products (store_id, category_id, kind, name, description, price_cents, sort)
           VALUES ($1, $2, 'simple', $3, $4, $5, $6)`,
          [storeId, categoryId, name, description, reais(price), sort++],
        );
        if (type === 'combo') {
          for (const groupId of [groupIds.saborCombo, groupIds.refriCombo]) {
            await tx.query('INSERT INTO product_option_groups (product_id, group_id) VALUES ($1, $2)', [
              productId,
              groupId,
            ]);
          }
        }
      }
    }

    for (let number = 1; number <= 10; number++) {
      await tx.query('INSERT INTO dining_tables (store_id, number, qr_token) VALUES ($1, $2, $3)', [
        storeId,
        number,
        randomQrToken(),
      ]);
    }

    return storeId;
  });
}
