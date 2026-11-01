import type { FastifyInstance, FastifyRequest } from 'fastify';
import { hashPassword, randomQrToken } from './auth.ts';
import { one, rows, type Db, type Queryable } from './db.ts';
import { AppError } from './errors.ts';

export interface StaffUser {
  storeId: string;
  userId: string;
  name: string;
  role: 'admin' | 'staff';
}

interface Deps {
  db: Db;
  requireAdmin: (request: FastifyRequest) => Promise<StaffUser>;
  notify: (storeId: string) => void;
}

const uuid = { type: 'string', format: 'uuid' } as const;
const idParams = { type: 'object', required: ['id'], properties: { id: uuid } } as const;
const name = { type: 'string', minLength: 1, maxLength: 80 } as const;
const cents = { type: 'integer', minimum: 0, maximum: 10_000_000 } as const;

const categoryBody = {
  type: 'object',
  required: ['name'],
  properties: {
    name,
    startMinute: { type: ['integer', 'null'], minimum: 0, maximum: 1440 },
    endMinute: { type: ['integer', 'null'], minimum: 0, maximum: 1440 },
  },
} as const;

const productBody = {
  type: 'object',
  required: ['categoryId', 'kind', 'name'],
  properties: {
    categoryId: uuid,
    kind: { enum: ['simple', 'pizza'] },
    name,
    description: { type: 'string', maxLength: 300 },
    priceCents: { ...cents, type: ['integer', 'null'] },
    pricesBySize: { type: 'object', additionalProperties: cents },
    optionGroupIds: { type: 'array', maxItems: 30, items: uuid },
    available: { type: 'boolean' },
  },
} as const;

const sizeBody = {
  type: 'object',
  required: ['name', 'slices', 'maxFlavors'],
  properties: {
    name,
    slices: { type: 'integer', minimum: 1, maximum: 64 },
    maxFlavors: { type: 'integer', minimum: 1, maximum: 4 },
  },
} as const;

const groupBody = {
  type: 'object',
  required: ['name', 'minSelect', 'maxSelect', 'options'],
  properties: {
    name,
    minSelect: { type: 'integer', minimum: 0, maximum: 30 },
    maxSelect: { type: 'integer', minimum: 1, maximum: 30 },
    options: {
      type: 'array',
      maxItems: 60,
      items: {
        type: 'object',
        required: ['name', 'priceCents'],
        properties: { id: uuid, name, priceCents: cents, available: { type: 'boolean' } },
      },
    },
  },
} as const;

interface CategoryBody {
  name: string;
  startMinute?: number | null;
  endMinute?: number | null;
}

interface ProductBody {
  categoryId: string;
  kind: 'simple' | 'pizza';
  name: string;
  description?: string;
  priceCents?: number | null;
  pricesBySize?: Record<string, number>;
  optionGroupIds?: string[];
  available?: boolean;
}

interface SizeBody {
  name: string;
  slices: number;
  maxFlavors: number;
}

interface GroupBody {
  name: string;
  minSelect: number;
  maxSelect: number;
  options: { id?: string; name: string; priceCents: number; available?: boolean }[];
}

function checkWindow({ startMinute = null, endMinute = null }: CategoryBody) {
  if ((startMinute === null) !== (endMinute === null)) {
    throw new AppError('Informe o início e o fim do horário, ou deixe os dois vazios.');
  }
  if (startMinute !== null && endMinute !== null && startMinute >= endMinute) {
    throw new AppError('O início do horário deve ser antes do fim.');
  }
  return [startMinute, endMinute];
}

async function assertAllOwned(db: Queryable, table: 'pizza_sizes' | 'option_groups', ids: string[], storeId: string) {
  if (ids.length === 0) return;
  const found = await one<{ count: number }>(
    db,
    `SELECT count(*)::int AS count FROM ${table} WHERE id = ANY($1::uuid[]) AND store_id = $2`,
    [ids, storeId],
  );
  if (found!.count !== new Set(ids).size) throw new AppError('Referência inválida no cadastro.');
}

async function saveProductLinks(tx: Queryable, storeId: string, productId: string, body: ProductBody) {
  const prices = Object.entries(body.pricesBySize ?? {});
  const groupIds = [...new Set(body.optionGroupIds ?? [])];
  await assertAllOwned(tx, 'pizza_sizes', prices.map(([sizeId]) => sizeId), storeId);
  await assertAllOwned(tx, 'option_groups', groupIds, storeId);

  await tx.query('DELETE FROM pizza_prices WHERE product_id = $1', [productId]);
  await tx.query('DELETE FROM product_option_groups WHERE product_id = $1', [productId]);
  if (body.kind === 'pizza') {
    for (const [sizeId, price] of prices) {
      await tx.query('INSERT INTO pizza_prices (product_id, size_id, price_cents) VALUES ($1, $2, $3)', [
        productId,
        sizeId,
        price,
      ]);
    }
  }
  for (const groupId of groupIds) {
    await tx.query('INSERT INTO product_option_groups (product_id, group_id) VALUES ($1, $2)', [productId, groupId]);
  }
}

function checkProduct(body: ProductBody) {
  if (body.kind === 'simple' && body.priceCents == null) throw new AppError('Informe o preço do produto.');
  if (body.kind === 'pizza' && Object.keys(body.pricesBySize ?? {}).length === 0) {
    throw new AppError('Informe o preço em ao menos um tamanho.');
  }
}

async function assertCategory(tx: Queryable, storeId: string, categoryId: string) {
  const found = await one(tx, 'SELECT 1 FROM categories WHERE id = $1 AND store_id = $2', [categoryId, storeId]);
  if (!found) throw new AppError('Categoria não encontrada.', 404);
}

function checkGroup(body: GroupBody) {
  if (body.options.length === 0) throw new AppError('Cadastre ao menos uma opção.');
  if (body.minSelect > body.maxSelect) throw new AppError('O mínimo não pode ser maior que o máximo.');
  if (body.minSelect > body.options.length) throw new AppError('O mínimo é maior que o número de opções.');
}

async function saveOptions(tx: Queryable, groupId: string, options: GroupBody['options']) {
  const kept = options.flatMap((option) => (option.id ? [option.id] : []));
  await tx.query('DELETE FROM options WHERE group_id = $1 AND NOT (id = ANY($2::uuid[]))', [groupId, kept]);
  for (const [sort, option] of options.entries()) {
    if (option.id) {
      const updated = await one(
        tx,
        `UPDATE options SET name = $1, price_cents = $2, available = COALESCE($3, available), sort = $4
         WHERE id = $5 AND group_id = $6 RETURNING id`,
        [option.name, option.priceCents, option.available ?? null, sort, option.id, groupId],
      );
      if (!updated) throw new AppError('Opção não encontrada.', 404);
    } else {
      await tx.query('INSERT INTO options (group_id, name, price_cents, sort) VALUES ($1, $2, $3, $4)', [
        groupId,
        option.name,
        option.priceCents,
        sort,
      ]);
    }
  }
}

const nextSort = async (db: Queryable, table: string, storeId: string) =>
  (await one<{ sort: number }>(db, `SELECT COALESCE(MAX(sort), -1)::int + 1 AS sort FROM ${table} WHERE store_id = $1`, [
    storeId,
  ]))!.sort;

const REORDERABLE = { categories: 'categories', products: 'products', sizes: 'pizza_sizes', groups: 'option_groups' };

export function registerAdminRoutes(app: FastifyInstance, { db, requireAdmin, notify }: Deps) {
  /** Toda alteracao avisa os paineis abertos. */
  const changed = (storeId: string) => {
    notify(storeId);
    return { ok: true };
  };

  async function removeOwned(table: string, id: string, storeId: string, missing: string) {
    const deleted = await one(db, `DELETE FROM ${table} WHERE id = $1 AND store_id = $2 RETURNING id`, [id, storeId]);
    if (!deleted) throw new AppError(missing, 404);
    return changed(storeId);
  }

  app.put<{ Body: { name: string; halfPizzaRule: string; servicePercent: number } }>(
    '/api/admin/store',
    {
      schema: {
        body: {
          type: 'object',
          required: ['name', 'halfPizzaRule', 'servicePercent'],
          properties: {
            name,
            halfPizzaRule: { enum: ['highest', 'average'] },
            servicePercent: { type: 'integer', minimum: 0, maximum: 30 },
          },
        },
      },
    },
    async (request) => {
      const { storeId } = await requireAdmin(request);
      const { name: storeName, halfPizzaRule, servicePercent } = request.body;
      await db.query('UPDATE stores SET name = $1, half_pizza_rule = $2, service_percent = $3 WHERE id = $4', [
        storeName,
        halfPizzaRule,
        servicePercent,
        storeId,
      ]);
      return changed(storeId);
    },
  );

  app.post<{ Body: CategoryBody }>('/api/admin/categories', { schema: { body: categoryBody } }, async (request) => {
    const { storeId } = await requireAdmin(request);
    const [start, end] = checkWindow(request.body);
    await db.query(
      'INSERT INTO categories (store_id, name, sort, start_minute, end_minute) VALUES ($1, $2, $3, $4, $5)',
      [storeId, request.body.name, await nextSort(db, 'categories', storeId), start, end],
    );
    return changed(storeId);
  });

  app.put<{ Params: { id: string }; Body: CategoryBody }>(
    '/api/admin/categories/:id',
    { schema: { params: idParams, body: categoryBody } },
    async (request) => {
      const { storeId } = await requireAdmin(request);
      const [start, end] = checkWindow(request.body);
      const updated = await one(
        db,
        `UPDATE categories SET name = $1, start_minute = $2, end_minute = $3
         WHERE id = $4 AND store_id = $5 RETURNING id`,
        [request.body.name, start, end, request.params.id, storeId],
      );
      if (!updated) throw new AppError('Categoria não encontrada.', 404);
      return changed(storeId);
    },
  );

  app.delete<{ Params: { id: string } }>(
    '/api/admin/categories/:id',
    { schema: { params: idParams } },
    async (request) => {
      const { storeId } = await requireAdmin(request);
      const used = await one(db, 'SELECT 1 FROM products WHERE category_id = $1 LIMIT 1', [request.params.id]);
      if (used) throw new AppError('Remova os produtos desta categoria antes de excluí-la.', 409);
      return removeOwned('categories', request.params.id, storeId, 'Categoria não encontrada.');
    },
  );

  app.post<{ Body: ProductBody }>('/api/admin/products', { schema: { body: productBody } }, async (request) => {
    const { storeId } = await requireAdmin(request);
    const { body } = request;
    checkProduct(body);
    await db.transaction(async (tx) => {
      await assertCategory(tx, storeId, body.categoryId);
      const product = await one<{ id: string }>(
        tx,
        `INSERT INTO products (store_id, category_id, kind, name, description, price_cents, sort)
         VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id`,
        [
          storeId,
          body.categoryId,
          body.kind,
          body.name,
          body.description ?? '',
          body.kind === 'simple' ? body.priceCents : null,
          await nextSort(tx, 'products', storeId),
        ],
      );
      await saveProductLinks(tx, storeId, product!.id, body);
    });
    return changed(storeId);
  });

  app.put<{ Params: { id: string }; Body: ProductBody }>(
    '/api/admin/products/:id',
    { schema: { params: idParams, body: productBody } },
    async (request) => {
      const { storeId } = await requireAdmin(request);
      const { body } = request;
      checkProduct(body);
      await db.transaction(async (tx) => {
        await assertCategory(tx, storeId, body.categoryId);
        const updated = await one(
          tx,
          `UPDATE products
           SET category_id = $1, name = $2, description = $3, price_cents = $4,
               available = COALESCE($5, available)
           WHERE id = $6 AND store_id = $7 AND kind = $8 RETURNING id`,
          [
            body.categoryId,
            body.name,
            body.description ?? '',
            body.kind === 'simple' ? body.priceCents : null,
            body.available ?? null,
            request.params.id,
            storeId,
            body.kind,
          ],
        );
        if (!updated) throw new AppError('Produto não encontrado.', 404);
        await saveProductLinks(tx, storeId, request.params.id, body);
      });
      return changed(storeId);
    },
  );

  // Pedidos guardam copia do nome e do preco, entao excluir nao altera o historico.
  app.delete<{ Params: { id: string } }>(
    '/api/admin/products/:id',
    { schema: { params: idParams } },
    async (request) =>
      removeOwned('products', request.params.id, (await requireAdmin(request)).storeId, 'Produto não encontrado.'),
  );

  app.post<{ Body: SizeBody }>('/api/admin/sizes', { schema: { body: sizeBody } }, async (request) => {
    const { storeId } = await requireAdmin(request);
    const { name: sizeName, slices, maxFlavors } = request.body;
    await db.query('INSERT INTO pizza_sizes (store_id, name, slices, max_flavors, sort) VALUES ($1, $2, $3, $4, $5)', [
      storeId,
      sizeName,
      slices,
      maxFlavors,
      await nextSort(db, 'pizza_sizes', storeId),
    ]);
    return changed(storeId);
  });

  app.put<{ Params: { id: string }; Body: SizeBody }>(
    '/api/admin/sizes/:id',
    { schema: { params: idParams, body: sizeBody } },
    async (request) => {
      const { storeId } = await requireAdmin(request);
      const { name: sizeName, slices, maxFlavors } = request.body;
      const updated = await one(
        db,
        'UPDATE pizza_sizes SET name = $1, slices = $2, max_flavors = $3 WHERE id = $4 AND store_id = $5 RETURNING id',
        [sizeName, slices, maxFlavors, request.params.id, storeId],
      );
      if (!updated) throw new AppError('Tamanho não encontrado.', 404);
      return changed(storeId);
    },
  );

  app.delete<{ Params: { id: string } }>('/api/admin/sizes/:id', { schema: { params: idParams } }, async (request) =>
    removeOwned('pizza_sizes', request.params.id, (await requireAdmin(request)).storeId, 'Tamanho não encontrado.'),
  );

  app.post<{ Body: GroupBody }>('/api/admin/option-groups', { schema: { body: groupBody } }, async (request) => {
    const { storeId } = await requireAdmin(request);
    const { body } = request;
    checkGroup(body);
    await db.transaction(async (tx) => {
      const group = await one<{ id: string }>(
        tx,
        `INSERT INTO option_groups (store_id, name, min_select, max_select, sort)
         VALUES ($1, $2, $3, $4, $5) RETURNING id`,
        [storeId, body.name, body.minSelect, body.maxSelect, await nextSort(tx, 'option_groups', storeId)],
      );
      await saveOptions(tx, group!.id, body.options);
    });
    return changed(storeId);
  });

  app.put<{ Params: { id: string }; Body: GroupBody }>(
    '/api/admin/option-groups/:id',
    { schema: { params: idParams, body: groupBody } },
    async (request) => {
      const { storeId } = await requireAdmin(request);
      const { body } = request;
      checkGroup(body);
      await db.transaction(async (tx) => {
        const updated = await one(
          tx,
          `UPDATE option_groups SET name = $1, min_select = $2, max_select = $3
           WHERE id = $4 AND store_id = $5 RETURNING id`,
          [body.name, body.minSelect, body.maxSelect, request.params.id, storeId],
        );
        if (!updated) throw new AppError('Grupo não encontrado.', 404);
        await saveOptions(tx, request.params.id, body.options);
      });
      return changed(storeId);
    },
  );

  app.delete<{ Params: { id: string } }>(
    '/api/admin/option-groups/:id',
    { schema: { params: idParams } },
    async (request) =>
      removeOwned('option_groups', request.params.id, (await requireAdmin(request)).storeId, 'Grupo não encontrado.'),
  );

  app.post<{ Body: { type: keyof typeof REORDERABLE; ids: string[] } }>(
    '/api/admin/reorder',
    {
      schema: {
        body: {
          type: 'object',
          required: ['type', 'ids'],
          properties: {
            type: { enum: Object.keys(REORDERABLE) },
            ids: { type: 'array', maxItems: 500, items: uuid },
          },
        },
      },
    },
    async (request) => {
      const { storeId } = await requireAdmin(request);
      const table = REORDERABLE[request.body.type];
      await db.transaction(async (tx) => {
        for (const [sort, id] of request.body.ids.entries()) {
          await tx.query(`UPDATE ${table} SET sort = $1 WHERE id = $2 AND store_id = $3`, [sort, id, storeId]);
        }
      });
      return changed(storeId);
    },
  );

  app.post('/api/admin/tables', async (request) => {
    const { storeId } = await requireAdmin(request);
    await db.query(
      `INSERT INTO dining_tables (store_id, number, qr_token)
       VALUES ($1, (SELECT COALESCE(MAX(number), 0) + 1 FROM dining_tables WHERE store_id = $1), $2)`,
      [storeId, randomQrToken()],
    );
    return changed(storeId);
  });

  app.delete<{ Params: { id: string } }>('/api/admin/tables/:id', { schema: { params: idParams } }, async (request) => {
    const { storeId } = await requireAdmin(request);
    const used = await one(db, 'SELECT 1 FROM bills WHERE table_id = $1 LIMIT 1', [request.params.id]);
    if (used) throw new AppError('Esta mesa já tem histórico de contas e não pode ser excluída.', 409);
    return removeOwned('dining_tables', request.params.id, storeId, 'Mesa não encontrada.');
  });

  app.get('/api/admin/users', async (request) =>
    rows(
      db,
      `SELECT id, name, login, role, active FROM users WHERE store_id = $1 ORDER BY name`,
      [(await requireAdmin(request)).storeId],
    ),
  );

  const password = { type: 'string', minLength: 6, maxLength: 200 } as const;
  const role = { enum: ['admin', 'staff'] } as const;

  app.post<{ Body: { name: string; login: string; password: string; role: string } }>(
    '/api/admin/users',
    {
      schema: {
        body: {
          type: 'object',
          required: ['name', 'login', 'password', 'role'],
          properties: {
            name,
            login: { type: 'string', pattern: '^[a-z0-9._-]{3,30}$' },
            password,
            role,
          },
        },
      },
    },
    async (request) => {
      const { storeId } = await requireAdmin(request);
      const { body } = request;
      const created = await one(
        db,
        `INSERT INTO users (store_id, name, login, password_hash, role) VALUES ($1, $2, $3, $4, $5)
         ON CONFLICT (store_id, login) DO NOTHING RETURNING id`,
        [storeId, body.name, body.login, hashPassword(body.password), body.role],
      );
      if (!created) throw new AppError('Este login já está em uso.', 409);
      return { ok: true };
    },
  );

  app.patch<{
    Params: { id: string };
    Body: { name: string; role: string; active: boolean; password?: string };
  }>(
    '/api/admin/users/:id',
    {
      schema: {
        params: idParams,
        body: {
          type: 'object',
          required: ['name', 'role', 'active'],
          properties: { name, role, active: { type: 'boolean' }, password },
        },
      },
    },
    async (request) => {
      const admin = await requireAdmin(request);
      const { body } = request;
      // Evita que a loja fique sem nenhum administrador.
      if (request.params.id === admin.userId && (!body.active || body.role !== 'admin')) {
        throw new AppError('Você não pode desativar nem rebaixar o próprio usuário.', 409);
      }
      const updated = await one(
        db,
        `UPDATE users
         SET name = $1, role = $2, active = $3, password_hash = COALESCE($4, password_hash)
         WHERE id = $5 AND store_id = $6 RETURNING id`,
        [
          body.name,
          body.role,
          body.active,
          body.password ? hashPassword(body.password) : null,
          request.params.id,
          admin.storeId,
        ],
      );
      if (!updated) throw new AppError('Usuário não encontrado.', 404);
      return { ok: true };
    },
  );
}
