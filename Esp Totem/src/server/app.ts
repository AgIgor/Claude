import { EventEmitter } from 'node:events';
import { existsSync } from 'node:fs';
import { resolve } from 'node:path';
import rateLimit from '@fastify/rate-limit';
import fastifyStatic from '@fastify/static';
import Fastify, { type FastifyReply, type FastifyRequest } from 'fastify';
import { registerAdminRoutes, type StaffUser } from './admin.ts';
import { createToken, readToken, verifyPassword } from './auth.ts';
import { loadCatalog, menuAt, minuteOfDay } from './catalog.ts';
import {
  addCashMovement,
  addPayment,
  adjustBill,
  closeBill,
  closeCashSession,
  currentCash,
  getBill,
  listCashHistory,
  listOpenBills,
  openCashSession,
  salesReport,
  voidPayment,
  type PaymentMethod,
} from './cashier.ts';
import { one, rows, type Db } from './db.ts';
import { AppError } from './errors.ts';
import {
  changeOrderStatus,
  createCustomerOrder,
  createStaffOrder,
  listBillOrders,
  listRecentOrders,
  openTableBill,
  type OrderStatus,
  type StaffOrderTarget,
} from './orders.ts';
import type { CartItem } from './pricing.ts';
import { loadTotals } from './totals.ts';

const STAFF_TOKEN_MS = 12 * 60 * 60_000;
const TABLE_TOKEN_MS = 6 * 60 * 60_000;

export interface AppOptions {
  db: Db;
  secret: string;
  hub?: EventEmitter;
  staticDir?: string;
  /** Tentativas de PIN por minuto, por IP e mesa. */
  pinAttemptsPerMinute?: number;
}

const uuid = { type: 'string', format: 'uuid' } as const;
const idParams = { type: 'object', required: ['id'], properties: { id: uuid } } as const;
const cents = { type: 'integer', minimum: 0, maximum: 100_000_000 } as const;

const cartItemSchema = {
  type: 'object',
  required: ['type', 'quantity'],
  additionalProperties: false,
  properties: {
    type: { enum: ['simple', 'pizza'] },
    productId: uuid,
    sizeId: uuid,
    flavorIds: { type: 'array', maxItems: 4, items: uuid },
    optionIds: { type: 'array', maxItems: 20, items: uuid },
    quantity: { type: 'integer', minimum: 1, maximum: 50 },
    notes: { type: 'string', maxLength: 200 },
  },
} as const;

const cartProperties = {
  items: { type: 'array', minItems: 1, maxItems: 30, items: cartItemSchema },
  idempotencyKey: { type: 'string', minLength: 8, maxLength: 64 },
} as const;

const unauthorized = () => new AppError('Acesso não autorizado.', 401);

function bearer(request: FastifyRequest): string | undefined {
  const header = request.headers.authorization;
  if (header?.startsWith('Bearer ')) return header.slice(7);
  // EventSource nao envia cabecalhos; o token vai na URL apenas nos fluxos SSE.
  return (request.query as { token?: string } | undefined)?.token;
}

export async function buildApp({
  db,
  secret,
  hub = new EventEmitter(),
  staticDir,
  pinAttemptsPerMinute = 10,
}: AppOptions) {
  hub.setMaxListeners(0);
  const app = Fastify({ logger: false, forceCloseConnections: true });
  await app.register(rateLimit, { global: false });

  const notifyStore = (storeId: string) => hub.emit(`store:${storeId}`);
  const notifyBill = (billId: string) => hub.emit(`bill:${billId}`);

  app.setErrorHandler((error: Error & { statusCode?: number; validation?: unknown }, _request, reply) => {
    if (error.validation) return reply.status(400).send({ error: 'Dados inválidos.' });
    const statusCode = error.statusCode ?? 500;
    if (statusCode >= 500) console.error(error);
    return reply.status(statusCode).send({ error: statusCode >= 500 ? 'Erro interno.' : error.message });
  });

  /** Consulta o usuario a cada requisicao: desativar alguem vale na hora. */
  async function requireStaff(request: FastifyRequest): Promise<StaffUser> {
    const token = readToken(bearer(request), secret);
    if (token?.kind !== 'staff') throw unauthorized();
    const user = await one<{ name: string; role: StaffUser['role'] }>(
      db,
      'SELECT name, role FROM users WHERE id = $1 AND store_id = $2 AND active',
      [token.userId, token.storeId],
    );
    if (!user) throw unauthorized();
    return { storeId: token.storeId, userId: token.userId, ...user };
  }

  async function requireAdmin(request: FastifyRequest): Promise<StaffUser> {
    const user = await requireStaff(request);
    if (user.role !== 'admin') throw new AppError('Disponível apenas para administradores.', 403);
    return user;
  }

  async function requireTable(request: FastifyRequest) {
    const token = readToken(bearer(request), secret);
    if (token?.kind !== 'table') throw unauthorized();
    const bill = await one<{ label: string }>(db, `SELECT label FROM bills WHERE id = $1 AND status = 'open'`, [
      token.billId,
    ]);
    if (!bill) throw new AppError('Esta mesa já foi fechada.', 401);
    return { storeId: token.storeId, billId: token.billId, label: bill.label };
  }

  function stream(request: FastifyRequest, reply: FastifyReply, topic: string) {
    reply.hijack();
    reply.raw.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache',
      Connection: 'keep-alive',
      'X-Accel-Buffering': 'no',
    });
    reply.raw.write(': conectado\n\n');
    const send = () => reply.raw.write('data: changed\n\n');
    const heartbeat = setInterval(() => reply.raw.write(': ping\n\n'), 25_000);
    hub.on(topic, send);
    request.raw.on('close', () => {
      clearInterval(heartbeat);
      hub.off(topic, send);
    });
  }

  const storeMenu = async (storeId: string, timezone: string) =>
    menuAt(await loadCatalog(db, storeId), minuteOfDay(new Date(), timezone));

  /* ---------- Cliente na mesa ---------- */

  app.get<{ Params: { qrToken: string } }>('/api/public/tables/:qrToken', async (request) => {
    const table = await one<{
      id: string;
      number: number;
      storeId: string;
      storeName: string;
      halfPizzaRule: string;
      timezone: string;
    }>(
      db,
      `SELECT t.id, t.number, s.id AS "storeId", s.name AS "storeName",
              s.half_pizza_rule AS "halfPizzaRule", s.timezone
       FROM dining_tables t JOIN stores s ON s.id = t.store_id WHERE t.qr_token = $1`,
      [request.params.qrToken],
    );
    if (!table) throw new AppError('Mesa não encontrada.', 404);

    const bill = await one(db, `SELECT 1 FROM bills WHERE table_id = $1 AND status = 'open'`, [table.id]);
    return {
      store: { name: table.storeName, halfPizzaRule: table.halfPizzaRule },
      table: { number: table.number, open: Boolean(bill) },
      menu: await storeMenu(table.storeId, table.timezone),
    };
  });

  app.post<{ Params: { qrToken: string }; Body: { pin: string } }>(
    '/api/public/tables/:qrToken/join',
    {
      config: {
        rateLimit: {
          max: pinAttemptsPerMinute,
          timeWindow: '1 minute',
          // Clientes costumam dividir o Wi-Fi da loja: o limite nao pode ser so por IP.
          keyGenerator: (request: FastifyRequest) =>
            `${request.ip}:${(request.params as { qrToken: string }).qrToken}`,
        },
      },
      schema: {
        body: {
          type: 'object',
          required: ['pin'],
          properties: { pin: { type: 'string', pattern: '^[0-9]{4}$' } },
        },
      },
    },
    async (request) => {
      const bill = await one<{ id: string; storeId: string; pin: string }>(
        db,
        `SELECT b.id, b.store_id AS "storeId", b.pin
         FROM bills b JOIN dining_tables t ON t.id = b.table_id
         WHERE t.qr_token = $1 AND b.status = 'open'`,
        [request.params.qrToken],
      );
      if (!bill) throw new AppError('Peça ao atendente para abrir a mesa.', 409);
      if (bill.pin !== request.body.pin) throw new AppError('Código incorreto.', 403);
      return {
        token: createToken(
          { kind: 'table', storeId: bill.storeId, billId: bill.id, exp: Date.now() + TABLE_TOKEN_MS },
          secret,
        ),
      };
    },
  );

  app.post<{ Body: { items: CartItem[]; idempotencyKey: string } }>(
    '/api/public/orders',
    {
      schema: {
        body: { type: 'object', required: ['items', 'idempotencyKey'], properties: cartProperties },
      },
    },
    async (request, reply) => {
      const session = await requireTable(request);
      const order = await createCustomerOrder(db, { ...session, ...request.body });
      notifyStore(session.storeId);
      notifyBill(session.billId);
      return reply.status(201).send(order);
    },
  );

  app.get('/api/public/session/orders', async (request) => {
    const { billId, label } = await requireTable(request);
    const { paidCents: _paid, remainingCents: _remaining, ...totals } = await loadTotals(db, billId);
    return { label, orders: await listBillOrders(db, billId), ...totals };
  });

  app.get('/api/public/session/events', async (request, reply) => {
    const { billId } = await requireTable(request);
    stream(request, reply, `bill:${billId}`);
  });

  /* ---------- Equipe ---------- */

  app.post<{ Body: { slug: string; login: string; password: string } }>(
    '/api/staff/login',
    {
      config: { rateLimit: { max: 10, timeWindow: '1 minute' } },
      schema: {
        body: {
          type: 'object',
          required: ['slug', 'login', 'password'],
          properties: {
            slug: { type: 'string' },
            login: { type: 'string', maxLength: 30 },
            password: { type: 'string', maxLength: 200 },
          },
        },
      },
    },
    async (request) => {
      const { slug, login, password } = request.body;
      const user = await one<{ id: string; storeId: string; hash: string }>(
        db,
        `SELECT u.id, u.store_id AS "storeId", u.password_hash AS hash
         FROM users u JOIN stores s ON s.id = u.store_id
         WHERE s.slug = $1 AND u.login = $2 AND u.active`,
        [slug, login.trim().toLowerCase()],
      );
      if (!user || !verifyPassword(password, user.hash)) throw new AppError('Usuário ou senha incorreta.', 401);
      return {
        token: createToken(
          { kind: 'staff', storeId: user.storeId, userId: user.id, exp: Date.now() + STAFF_TOKEN_MS },
          secret,
        ),
      };
    },
  );

  app.get('/api/staff/me', async (request) => {
    const { storeId, ...user } = await requireStaff(request);
    const store = await one(
      db,
      `SELECT name, half_pizza_rule AS "halfPizzaRule", service_percent AS "servicePercent"
       FROM stores WHERE id = $1`,
      [storeId],
    );
    return { user, store };
  });

  app.get('/api/staff/events', async (request, reply) => {
    const { storeId } = await requireStaff(request);
    stream(request, reply, `store:${storeId}`);
  });

  app.get('/api/staff/orders', async (request) => listRecentOrders(db, (await requireStaff(request)).storeId));

  app.post<{ Body: StaffOrderTarget & { items: CartItem[]; idempotencyKey: string } }>(
    '/api/staff/orders',
    {
      schema: {
        body: {
          type: 'object',
          required: ['items', 'idempotencyKey'],
          properties: {
            ...cartProperties,
            billId: uuid,
            tableId: uuid,
            label: { type: 'string', maxLength: 60 },
          },
        },
      },
    },
    async (request, reply) => {
      const { storeId } = await requireStaff(request);
      const { items, idempotencyKey, ...target } = request.body;
      if (Object.keys(target).length !== 1) throw new AppError('Escolha a mesa ou o cliente do balcão.');
      const order = await createStaffOrder(db, { storeId, target, items, idempotencyKey });
      notifyStore(storeId);
      notifyBill(order.billId);
      return reply.status(201).send(order);
    },
  );

  app.post<{ Params: { id: string }; Body: { status: OrderStatus } }>(
    '/api/staff/orders/:id/status',
    {
      schema: {
        params: idParams,
        body: {
          type: 'object',
          required: ['status'],
          properties: { status: { enum: ['accepted', 'preparing', 'ready', 'delivered', 'cancelled'] } },
        },
      },
    },
    async (request) => {
      const { storeId } = await requireStaff(request);
      const order = await changeOrderStatus(db, storeId, request.params.id, request.body.status);
      notifyStore(storeId);
      notifyBill(order.billId);
      return order;
    },
  );

  app.get('/api/staff/tables', async (request) => {
    const { storeId } = await requireStaff(request);
    return rows(
      db,
      `SELECT t.id, t.number, t.qr_token AS "qrToken", b.id AS "billId", b.pin
       FROM dining_tables t
       LEFT JOIN bills b ON b.table_id = t.id AND b.status = 'open'
       WHERE t.store_id = $1 ORDER BY t.number`,
      [storeId],
    );
  });

  app.post<{ Params: { id: string } }>(
    '/api/staff/tables/:id/open',
    { schema: { params: idParams } },
    async (request) => {
      const { storeId } = await requireStaff(request);
      await db.transaction((tx) => openTableBill(tx, storeId, request.params.id));
      notifyStore(storeId);
      return { ok: true };
    },
  );

  app.get('/api/staff/menu', async (request) => {
    const { storeId } = await requireStaff(request);
    const store = await one<{ timezone: string }>(db, 'SELECT timezone FROM stores WHERE id = $1', [storeId]);
    return storeMenu(storeId, store!.timezone);
  });

  const availabilitySchema = {
    params: idParams,
    body: { type: 'object', required: ['available'], properties: { available: { type: 'boolean' } } },
  };

  app.patch<{ Params: { id: string }; Body: { available: boolean } }>(
    '/api/staff/products/:id/availability',
    { schema: availabilitySchema },
    async (request) => {
      const { storeId } = await requireStaff(request);
      const updated = await one(db, 'UPDATE products SET available = $1 WHERE id = $2 AND store_id = $3 RETURNING id', [
        request.body.available,
        request.params.id,
        storeId,
      ]);
      if (!updated) throw new AppError('Produto não encontrado.', 404);
      return { ok: true };
    },
  );

  app.patch<{ Params: { id: string }; Body: { available: boolean } }>(
    '/api/staff/options/:id/availability',
    { schema: availabilitySchema },
    async (request) => {
      const { storeId } = await requireStaff(request);
      const updated = await one(
        db,
        `UPDATE options o SET available = $1 FROM option_groups g
         WHERE o.id = $2 AND g.id = o.group_id AND g.store_id = $3 RETURNING o.id`,
        [request.body.available, request.params.id, storeId],
      );
      if (!updated) throw new AppError('Opção não encontrada.', 404);
      return { ok: true };
    },
  );

  /* ---------- Caixa ---------- */

  app.get('/api/staff/bills', async (request) => listOpenBills(db, (await requireStaff(request)).storeId));

  app.get<{ Params: { id: string } }>('/api/staff/bills/:id', { schema: { params: idParams } }, async (request) =>
    getBill(db, (await requireStaff(request)).storeId, request.params.id),
  );

  app.post<{ Params: { id: string }; Body: { serviceApplied: boolean; discountCents: number } }>(
    '/api/staff/bills/:id/adjust',
    {
      schema: {
        params: idParams,
        body: {
          type: 'object',
          required: ['serviceApplied', 'discountCents'],
          properties: { serviceApplied: { type: 'boolean' }, discountCents: cents },
        },
      },
    },
    async (request) => {
      const { storeId } = await requireStaff(request);
      const bill = await adjustBill(db, storeId, request.params.id, request.body);
      notifyStore(storeId);
      notifyBill(bill.id);
      return bill;
    },
  );

  app.post<{
    Params: { id: string };
    Body: { method: PaymentMethod; amountCents: number; receivedCents?: number };
  }>(
    '/api/staff/bills/:id/payments',
    {
      schema: {
        params: idParams,
        body: {
          type: 'object',
          required: ['method', 'amountCents'],
          properties: {
            method: { enum: ['cash', 'pix', 'debit', 'credit'] },
            amountCents: { ...cents, minimum: 1 },
            receivedCents: cents,
          },
        },
      },
    },
    async (request) => {
      const { storeId, name } = await requireStaff(request);
      const bill = await addPayment(db, storeId, request.params.id, name, request.body);
      notifyStore(storeId);
      return bill;
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/staff/payments/:id/void',
    { schema: { params: idParams } },
    async (request) => {
      const { storeId, name } = await requireAdmin(request);
      const bill = await voidPayment(db, storeId, request.params.id, name);
      notifyStore(storeId);
      return bill;
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/staff/bills/:id/close',
    { schema: { params: idParams } },
    async (request) => {
      const { storeId, name } = await requireStaff(request);
      await closeBill(db, storeId, request.params.id, name);
      notifyStore(storeId);
      notifyBill(request.params.id);
      return { ok: true };
    },
  );

  app.get('/api/staff/cash', async (request) => ({
    current: await currentCash(db, (await requireStaff(request)).storeId),
  }));

  app.post<{ Body: { openingCents: number } }>(
    '/api/staff/cash/open',
    {
      schema: {
        body: { type: 'object', required: ['openingCents'], properties: { openingCents: cents } },
      },
    },
    async (request) => {
      const { storeId, name } = await requireStaff(request);
      await openCashSession(db, storeId, name, request.body.openingCents);
      notifyStore(storeId);
      return { ok: true };
    },
  );

  app.post<{ Body: { type: 'withdrawal' | 'deposit'; amountCents: number; reason: string } }>(
    '/api/staff/cash/movements',
    {
      schema: {
        body: {
          type: 'object',
          required: ['type', 'amountCents', 'reason'],
          properties: {
            type: { enum: ['withdrawal', 'deposit'] },
            amountCents: { ...cents, minimum: 1 },
            reason: { type: 'string', minLength: 1, maxLength: 120, pattern: '\\S' },
          },
        },
      },
    },
    async (request) => {
      const { storeId, name } = await requireStaff(request);
      await addCashMovement(db, storeId, name, request.body);
      notifyStore(storeId);
      return { ok: true };
    },
  );

  app.post<{ Body: { countedCents: number } }>(
    '/api/staff/cash/close',
    {
      schema: {
        body: { type: 'object', required: ['countedCents'], properties: { countedCents: cents } },
      },
    },
    async (request) => {
      const { storeId, name } = await requireStaff(request);
      const result = await closeCashSession(db, storeId, name, request.body.countedCents);
      notifyStore(storeId);
      return result;
    },
  );

  /* ---------- Administracao ---------- */

  app.get('/api/admin/cash/history', async (request) =>
    listCashHistory(db, (await requireAdmin(request)).storeId),
  );

  app.get<{ Querystring: { from: string; to: string } }>(
    '/api/admin/reports/sales',
    {
      schema: {
        querystring: {
          type: 'object',
          required: ['from', 'to'],
          properties: { from: { type: 'string', format: 'date' }, to: { type: 'string', format: 'date' } },
        },
      },
    },
    async (request) =>
      salesReport(db, (await requireAdmin(request)).storeId, request.query.from, request.query.to),
  );

  registerAdminRoutes(app, { db, requireAdmin, notify: notifyStore });

  /* ---------- Frontend compilado ---------- */

  if (staticDir && existsSync(resolve(staticDir, 'index.html'))) {
    await app.register(fastifyStatic, { root: resolve(staticDir) });
    app.setNotFoundHandler((request, reply) => {
      if (request.url.startsWith('/api/')) return reply.status(404).send({ error: 'Rota não encontrada.' });
      return reply.sendFile('index.html');
    });
  }

  return app;
}
