import { randomPin } from './auth.ts';
import { loadCatalog, minuteOfDay } from './catalog.ts';
import { one, rows, type Db, type Queryable } from './db.ts';
import { AppError } from './errors.ts';
import { priceCart, type CartItem, type HalfPizzaRule, type PricedItem } from './pricing.ts';
import { loadTotals } from './totals.ts';

export type OrderStatus = 'new' | 'accepted' | 'preparing' | 'ready' | 'delivered' | 'cancelled';

export interface OrderView {
  id: string;
  billId: string;
  number: number;
  channel: 'qr' | 'staff';
  label: string;
  status: OrderStatus;
  totalCents: number;
  createdAt: string;
  items: {
    name: string;
    quantity: number;
    unitPriceCents: number;
    totalCents: number;
    notes: string;
    details: PricedItem['details'];
  }[];
}

const TRANSITIONS: Record<OrderStatus, OrderStatus[]> = {
  new: ['accepted', 'cancelled'],
  accepted: ['preparing', 'cancelled'],
  preparing: ['ready', 'cancelled'],
  ready: ['delivered'],
  delivered: [],
  cancelled: [],
};

const ORDER_COLUMNS = `
  o.id, o.bill_id AS "billId", o.number, o.channel, o.label, o.status,
  o.total_cents AS "totalCents", o.created_at AS "createdAt"`;

async function withItems(db: Queryable, orders: Omit<OrderView, 'items'>[]): Promise<OrderView[]> {
  if (orders.length === 0) return [];
  const items = await rows<OrderView['items'][number] & { orderId: string }>(
    db,
    `SELECT order_id AS "orderId", name, quantity, unit_price_cents AS "unitPriceCents",
            total_cents AS "totalCents", notes, details
     FROM order_items WHERE order_id = ANY($1::uuid[]) ORDER BY position`,
    [orders.map((order) => order.id)],
  );
  return orders.map((order) => ({
    ...order,
    items: items.filter((item) => item.orderId === order.id).map(({ orderId: _, ...item }) => item),
  }));
}

export async function getOrder(db: Queryable, orderId: string): Promise<OrderView | undefined> {
  const order = await one<Omit<OrderView, 'items'>>(db, `SELECT ${ORDER_COLUMNS} FROM orders o WHERE o.id = $1`, [
    orderId,
  ]);
  return order && (await withItems(db, [order]))[0];
}

export async function listRecentOrders(db: Queryable, storeId: string): Promise<OrderView[]> {
  const orders = await rows<Omit<OrderView, 'items'>>(
    db,
    `SELECT ${ORDER_COLUMNS} FROM orders o
     WHERE o.store_id = $1 AND o.created_at > now() - interval '12 hours'
     ORDER BY o.created_at`,
    [storeId],
  );
  return withItems(db, orders);
}

export async function listBillOrders(db: Queryable, billId: string): Promise<OrderView[]> {
  const orders = await rows<Omit<OrderView, 'items'>>(
    db,
    `SELECT ${ORDER_COLUMNS} FROM orders o WHERE o.bill_id = $1 ORDER BY o.created_at`,
    [billId],
  );
  return withItems(db, orders);
}

interface OrderInput {
  storeId: string;
  billId: string;
  label: string;
  channel: OrderView['channel'];
  status: 'new' | 'accepted';
  items: CartItem[];
  idempotencyKey: string;
}

const findByKey = (db: Queryable, storeId: string, key: string) =>
  one<{ id: string }>(db, 'SELECT id FROM orders WHERE store_id = $1 AND idempotency_key = $2', [storeId, key]);

async function insertOrder(tx: Queryable, input: OrderInput, now: Date): Promise<OrderView> {
  const bill = await one<{ status: string }>(tx, 'SELECT status FROM bills WHERE id = $1 FOR UPDATE', [input.billId]);
  if (bill?.status !== 'open') throw new AppError('Esta conta já foi fechada.', 409);

  const store = await one<{ rule: HalfPizzaRule; timezone: string }>(
    tx,
    'SELECT half_pizza_rule AS rule, timezone FROM stores WHERE id = $1',
    [input.storeId],
  );
  const catalog = await loadCatalog(tx, input.storeId);
  const priced = priceCart(input.items, catalog, store!.rule, minuteOfDay(now, store!.timezone));
  const totalCents = priced.reduce((sum, item) => sum + item.totalCents, 0);

  const counter = await one<{ number: number }>(
    tx,
    `INSERT INTO order_counters (store_id, day, last_number)
     VALUES ($1, ($2::timestamptz AT TIME ZONE $3)::date, 1)
     ON CONFLICT (store_id, day) DO UPDATE SET last_number = order_counters.last_number + 1
     RETURNING last_number AS number`,
    [input.storeId, now.toISOString(), store!.timezone],
  );

  const order = await one<{ id: string }>(
    tx,
    `INSERT INTO orders (store_id, number, channel, bill_id, label, status, total_cents, idempotency_key)
     VALUES ($1, $2, $3, $4, $5, $6, $7, $8) RETURNING id`,
    [
      input.storeId,
      counter!.number,
      input.channel,
      input.billId,
      input.label,
      input.status,
      totalCents,
      input.idempotencyKey,
    ],
  );

  for (const [position, item] of priced.entries()) {
    await tx.query(
      `INSERT INTO order_items
         (order_id, position, name, quantity, unit_price_cents, total_cents, notes, details)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8)`,
      [
        order!.id,
        position,
        item.name,
        item.quantity,
        item.unitPriceCents,
        item.totalCents,
        item.notes,
        JSON.stringify(item.details),
      ],
    );
  }
  return (await getOrder(tx, order!.id))!;
}

const isUniqueViolation = (error: unknown) => (error as { code?: string }).code === '23505';

/** Reenvios com a mesma chave devolvem o pedido ja criado, mesmo em corrida. */
async function idempotent(
  db: Db,
  storeId: string,
  key: string,
  create: (tx: Queryable) => Promise<OrderView>,
): Promise<OrderView> {
  try {
    return await db.transaction(async (tx) => {
      const existing = await findByKey(tx, storeId, key);
      return existing ? (await getOrder(tx, existing.id))! : create(tx);
    });
  } catch (error) {
    if (!isUniqueViolation(error)) throw error;
    const existing = await findByKey(db, storeId, key);
    if (!existing) throw error;
    return (await getOrder(db, existing.id))!;
  }
}

export function createCustomerOrder(
  db: Db,
  input: { storeId: string; billId: string; label: string; items: CartItem[]; idempotencyKey: string },
  now = new Date(),
): Promise<OrderView> {
  return idempotent(db, input.storeId, input.idempotencyKey, (tx) =>
    insertOrder(tx, { ...input, channel: 'qr', status: 'new' }, now),
  );
}

export type StaffOrderTarget = { billId: string } | { tableId: string } | { label: string };

async function resolveBill(tx: Queryable, storeId: string, target: StaffOrderTarget) {
  if ('billId' in target) {
    const bill = await one<{ id: string; label: string }>(
      tx,
      'SELECT id, label FROM bills WHERE id = $1 AND store_id = $2',
      [target.billId, storeId],
    );
    if (!bill) throw new AppError('Conta não encontrada.', 404);
    return bill;
  }
  if ('tableId' in target) return openTableBill(tx, storeId, target.tableId);

  const label = target.label.trim();
  if (!label) throw new AppError('Informe o nome do cliente.');
  return (await one<{ id: string; label: string }>(
    tx,
    `INSERT INTO bills (store_id, kind, label) VALUES ($1, 'counter', $2) RETURNING id, label`,
    [storeId, label],
  ))!;
}

export async function openTableBill(tx: Queryable, storeId: string, tableId: string) {
  const table = await one<{ number: number; servicePercent: number }>(
    tx,
    `SELECT t.number, s.service_percent AS "servicePercent"
     FROM dining_tables t JOIN stores s ON s.id = t.store_id WHERE t.id = $1 AND t.store_id = $2`,
    [tableId, storeId],
  );
  if (!table) throw new AppError('Mesa não encontrada.', 404);

  const label = `Mesa ${table.number}`;
  await tx.query(
    `INSERT INTO bills (store_id, kind, table_id, label, pin, service_applied)
     VALUES ($1, 'table', $2, $3, $4, $5)
     ON CONFLICT (table_id) WHERE status = 'open' DO NOTHING`,
    [storeId, tableId, label, randomPin(), table.servicePercent > 0],
  );
  return (await one<{ id: string; label: string }>(
    tx,
    `SELECT id, label FROM bills WHERE table_id = $1 AND status = 'open'`,
    [tableId],
  ))!;
}

/** Pedido lancado pela equipe ja nasce aceito. Se falhar, a conta criada aqui tambem e desfeita. */
export function createStaffOrder(
  db: Db,
  input: { storeId: string; target: StaffOrderTarget; items: CartItem[]; idempotencyKey: string },
  now = new Date(),
): Promise<OrderView> {
  return idempotent(db, input.storeId, input.idempotencyKey, async (tx) => {
    const bill = await resolveBill(tx, input.storeId, input.target);
    return insertOrder(
      tx,
      {
        storeId: input.storeId,
        billId: bill.id,
        label: bill.label,
        channel: 'staff',
        status: 'accepted',
        items: input.items,
        idempotencyKey: input.idempotencyKey,
      },
      now,
    );
  });
}

export async function changeOrderStatus(
  db: Db,
  storeId: string,
  orderId: string,
  next: OrderStatus,
): Promise<OrderView> {
  return db.transaction(async (tx) => {
    const current = await one<{ status: OrderStatus; billId: string; totalCents: number; billStatus: string }>(
      tx,
      `SELECT o.status, o.bill_id AS "billId", o.total_cents AS "totalCents", b.status AS "billStatus"
       FROM orders o JOIN bills b ON b.id = o.bill_id
       WHERE o.id = $1 AND o.store_id = $2 FOR UPDATE OF o, b`,
      [orderId, storeId],
    );
    if (!current) throw new AppError('Pedido não encontrado.', 404);
    if (!TRANSITIONS[current.status].includes(next)) {
      throw new AppError('Este pedido não pode mudar para esse status.', 409);
    }

    if (next === 'cancelled') {
      if (current.billStatus === 'closed') {
        throw new AppError('A conta deste pedido já foi fechada.', 409);
      }
      await tx.query(`UPDATE orders SET status = 'cancelled', updated_at = now() WHERE id = $1`, [orderId]);
      const totals = await loadTotals(tx, current.billId);
      if (totals.remainingCents < 0) {
        throw new AppError('Estorne o pagamento antes de cancelar este pedido.', 409);
      }
    } else {
      await tx.query('UPDATE orders SET status = $1, updated_at = now() WHERE id = $2', [next, orderId]);
    }
    return (await getOrder(tx, orderId))!;
  });
}
