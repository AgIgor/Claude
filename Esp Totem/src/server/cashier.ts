import { one, rows, type Db, type Queryable } from './db.ts';
import { AppError } from './errors.ts';
import { listBillOrders, type OrderView } from './orders.ts';
import { loadTotals, type BillTotals } from './totals.ts';

export type PaymentMethod = 'cash' | 'pix' | 'debit' | 'credit';

export interface PaymentView {
  id: string;
  method: PaymentMethod;
  amountCents: number;
  receivedCents: number | null;
  changeCents: number;
  createdBy: string;
  createdAt: string;
  voidedAt: string | null;
}

export interface BillSummary extends BillTotals {
  id: string;
  kind: 'table' | 'counter';
  label: string;
  status: 'open' | 'closed';
  serviceApplied: boolean;
  openedAt: string;
  activeOrders: number;
}

export interface BillDetail extends BillSummary {
  orders: OrderView[];
  payments: PaymentView[];
}

async function billRow(db: Queryable, storeId: string, billId: string) {
  return one<Omit<BillSummary, keyof BillTotals | 'activeOrders'>>(
    db,
    `SELECT id, kind, label, status, service_applied AS "serviceApplied", opened_at AS "openedAt"
     FROM bills WHERE id = $1 AND store_id = $2`,
    [billId, storeId],
  );
}

const countActive = async (db: Queryable, billId: string) =>
  (await one<{ count: number }>(
    db,
    `SELECT count(*)::int AS count FROM orders WHERE bill_id = $1 AND status NOT IN ('delivered', 'cancelled')`,
    [billId],
  ))!.count;

export async function listOpenBills(db: Queryable, storeId: string): Promise<BillSummary[]> {
  const bills = await rows<{ id: string }>(
    db,
    `SELECT id FROM bills WHERE store_id = $1 AND status = 'open' ORDER BY opened_at`,
    [storeId],
  );
  const summaries: BillSummary[] = [];
  for (const { id } of bills) {
    summaries.push({
      ...(await billRow(db, storeId, id))!,
      ...(await loadTotals(db, id)),
      activeOrders: await countActive(db, id),
    });
  }
  return summaries;
}

export async function getBill(db: Queryable, storeId: string, billId: string): Promise<BillDetail> {
  const bill = await billRow(db, storeId, billId);
  if (!bill) throw new AppError('Conta não encontrada.', 404);
  return {
    ...bill,
    ...(await loadTotals(db, billId)),
    activeOrders: await countActive(db, billId),
    orders: await listBillOrders(db, billId),
    payments: await rows<PaymentView>(
      db,
      `SELECT id, method, amount_cents AS "amountCents", received_cents AS "receivedCents",
              change_cents AS "changeCents", created_by AS "createdBy", created_at AS "createdAt",
              voided_at AS "voidedAt"
       FROM payments WHERE bill_id = $1 ORDER BY created_at`,
      [billId],
    ),
  };
}

async function lockOpenBill(tx: Queryable, storeId: string, billId: string) {
  const bill = await one<{ status: string; kind: string }>(
    tx,
    'SELECT status, kind FROM bills WHERE id = $1 AND store_id = $2 FOR UPDATE',
    [billId, storeId],
  );
  if (!bill) throw new AppError('Conta não encontrada.', 404);
  if (bill.status !== 'open') throw new AppError('Esta conta já foi fechada.', 409);
  return bill;
}

const openCash = (tx: Queryable, storeId: string) =>
  one<{ id: string; openingCents: number; openedBy: string; openedAt: string }>(
    tx,
    `SELECT id, opening_cents AS "openingCents", opened_by AS "openedBy", opened_at AS "openedAt"
     FROM cash_sessions WHERE store_id = $1 AND status = 'open' FOR UPDATE`,
    [storeId],
  );

export function adjustBill(
  db: Db,
  storeId: string,
  billId: string,
  changes: { serviceApplied: boolean; discountCents: number },
) {
  return db.transaction(async (tx) => {
    await lockOpenBill(tx, storeId, billId);
    await tx.query('UPDATE bills SET service_applied = $1, discount_cents = $2 WHERE id = $3', [
      changes.serviceApplied,
      changes.discountCents,
      billId,
    ]);
    const totals = await loadTotals(tx, billId);
    if (changes.discountCents > totals.subtotalCents + totals.serviceCents) {
      throw new AppError('O desconto não pode ser maior que a conta.');
    }
    if (totals.remainingCents < 0) throw new AppError('O total ficaria menor que o valor já pago.', 409);
    return getBill(tx, storeId, billId);
  });
}

export function addPayment(
  db: Db,
  storeId: string,
  billId: string,
  operator: string,
  payment: { method: PaymentMethod; amountCents: number; receivedCents?: number },
) {
  return db.transaction(async (tx) => {
    const cash = await openCash(tx, storeId);
    if (!cash) throw new AppError('Abra o caixa antes de receber pagamentos.', 409);
    await lockOpenBill(tx, storeId, billId);

    const { remainingCents } = await loadTotals(tx, billId);
    if (payment.amountCents > remainingCents) {
      throw new AppError('O valor é maior que o saldo da conta.');
    }

    const isCash = payment.method === 'cash';
    const receivedCents = isCash ? (payment.receivedCents ?? payment.amountCents) : null;
    if (receivedCents !== null && receivedCents < payment.amountCents) {
      throw new AppError('O valor recebido é menor que o valor a pagar.');
    }

    await tx.query(
      `INSERT INTO payments
         (store_id, bill_id, cash_session_id, method, amount_cents, received_cents, change_cents, created_by)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8)`,
      [
        storeId,
        billId,
        cash.id,
        payment.method,
        payment.amountCents,
        receivedCents,
        receivedCents === null ? 0 : receivedCents - payment.amountCents,
        operator,
      ],
    );
    return getBill(tx, storeId, billId);
  });
}

export function voidPayment(db: Db, storeId: string, paymentId: string, operator: string) {
  return db.transaction(async (tx) => {
    const payment = await one<{ billId: string; voidedAt: string | null; cashStatus: string }>(
      tx,
      `SELECT p.bill_id AS "billId", p.voided_at AS "voidedAt", c.status AS "cashStatus"
       FROM payments p JOIN cash_sessions c ON c.id = p.cash_session_id
       WHERE p.id = $1 AND p.store_id = $2 FOR UPDATE OF p`,
      [paymentId, storeId],
    );
    if (!payment) throw new AppError('Pagamento não encontrado.', 404);
    if (payment.voidedAt) throw new AppError('Este pagamento já foi estornado.', 409);
    if (payment.cashStatus !== 'open') throw new AppError('O caixa deste pagamento já foi fechado.', 409);
    await lockOpenBill(tx, storeId, payment.billId);

    await tx.query('UPDATE payments SET voided_at = now(), voided_by = $1 WHERE id = $2', [operator, paymentId]);
    return getBill(tx, storeId, payment.billId);
  });
}

export function closeBill(db: Db, storeId: string, billId: string, operator: string) {
  return db.transaction(async (tx) => {
    const bill = await lockOpenBill(tx, storeId, billId);
    const { remainingCents } = await loadTotals(tx, billId);
    if (remainingCents > 0) throw new AppError('Ainda há saldo a pagar nesta conta.', 409);
    // No balcao o cliente costuma pagar antes de a pizza ficar pronta.
    if (bill.kind === 'table' && (await countActive(tx, billId)) > 0) {
      throw new AppError('Conclua ou cancele os pedidos da mesa antes de fechar.', 409);
    }
    await tx.query(`UPDATE bills SET status = 'closed', closed_at = now(), closed_by = $1 WHERE id = $2`, [
      operator,
      billId,
    ]);
  });
}

/* ---------- Caixa ---------- */

export interface CashSummary {
  id: string;
  openingCents: number;
  openedBy: string;
  openedAt: string;
  byMethod: Record<PaymentMethod, number>;
  depositsCents: number;
  withdrawalsCents: number;
  expectedCashCents: number;
  movements: { id: string; type: 'withdrawal' | 'deposit'; amountCents: number; reason: string; createdBy: string; createdAt: string }[];
}

async function summarize(tx: Queryable, cash: NonNullable<Awaited<ReturnType<typeof openCash>>>): Promise<CashSummary> {
  const payments = await rows<{ method: PaymentMethod; cents: number }>(
    tx,
    `SELECT method, SUM(amount_cents)::int AS cents FROM payments
     WHERE cash_session_id = $1 AND voided_at IS NULL GROUP BY method`,
    [cash.id],
  );
  const movements = await rows<CashSummary['movements'][number]>(
    tx,
    `SELECT id, type, amount_cents AS "amountCents", reason, created_by AS "createdBy", created_at AS "createdAt"
     FROM cash_movements WHERE cash_session_id = $1 ORDER BY created_at`,
    [cash.id],
  );

  const byMethod: Record<PaymentMethod, number> = { cash: 0, pix: 0, debit: 0, credit: 0 };
  for (const payment of payments) byMethod[payment.method] = payment.cents;
  const sumOf = (type: string) =>
    movements.filter((movement) => movement.type === type).reduce((sum, movement) => sum + movement.amountCents, 0);
  const depositsCents = sumOf('deposit');
  const withdrawalsCents = sumOf('withdrawal');

  return {
    ...cash,
    byMethod,
    depositsCents,
    withdrawalsCents,
    expectedCashCents: cash.openingCents + byMethod.cash + depositsCents - withdrawalsCents,
    movements,
  };
}

export async function currentCash(db: Db, storeId: string): Promise<CashSummary | null> {
  return db.transaction(async (tx) => {
    const cash = await openCash(tx, storeId);
    return cash ? summarize(tx, cash) : null;
  });
}

const isUniqueViolation = (error: unknown) => (error as { code?: string }).code === '23505';

export async function openCashSession(db: Db, storeId: string, operator: string, openingCents: number) {
  try {
    await db.query('INSERT INTO cash_sessions (store_id, opening_cents, opened_by) VALUES ($1, $2, $3)', [
      storeId,
      openingCents,
      operator,
    ]);
  } catch (error) {
    if (isUniqueViolation(error)) throw new AppError('O caixa já está aberto.', 409);
    throw error;
  }
}

export function addCashMovement(
  db: Db,
  storeId: string,
  operator: string,
  movement: { type: 'withdrawal' | 'deposit'; amountCents: number; reason: string },
) {
  return db.transaction(async (tx) => {
    const cash = await openCash(tx, storeId);
    if (!cash) throw new AppError('O caixa está fechado.', 409);
    if (movement.type === 'withdrawal') {
      const { expectedCashCents } = await summarize(tx, cash);
      if (movement.amountCents > expectedCashCents) {
        throw new AppError('A sangria é maior que o dinheiro no caixa.');
      }
    }
    await tx.query(
      `INSERT INTO cash_movements (cash_session_id, type, amount_cents, reason, created_by)
       VALUES ($1, $2, $3, $4, $5)`,
      [cash.id, movement.type, movement.amountCents, movement.reason.trim(), operator],
    );
  });
}

export function closeCashSession(db: Db, storeId: string, operator: string, countedCents: number) {
  return db.transaction(async (tx) => {
    const cash = await openCash(tx, storeId);
    if (!cash) throw new AppError('O caixa está fechado.', 409);
    const summary = await summarize(tx, cash);
    await tx.query(
      `UPDATE cash_sessions
       SET status = 'closed', expected_cents = $1, counted_cents = $2, closed_by = $3, closed_at = now()
       WHERE id = $4`,
      [summary.expectedCashCents, countedCents, operator, cash.id],
    );
    return { ...summary, countedCents, differenceCents: countedCents - summary.expectedCashCents };
  });
}

export function listCashHistory(db: Queryable, storeId: string) {
  return rows(
    db,
    `SELECT id, opening_cents AS "openingCents", opened_by AS "openedBy", opened_at AS "openedAt",
            expected_cents AS "expectedCents", counted_cents AS "countedCents",
            closed_by AS "closedBy", closed_at AS "closedAt"
     FROM cash_sessions WHERE store_id = $1 AND status = 'closed' ORDER BY closed_at DESC LIMIT 30`,
    [storeId],
  );
}

/* ---------- Relatorio ---------- */

export async function salesReport(db: Queryable, storeId: string, from: string, to: string) {
  const store = await one<{ timezone: string }>(db, 'SELECT timezone FROM stores WHERE id = $1', [storeId]);
  const range = [storeId, from, to, store!.timezone];

  const byMethod = await rows<{ method: PaymentMethod; cents: number }>(
    db,
    `SELECT method, SUM(amount_cents)::int AS cents FROM payments
     WHERE store_id = $1 AND voided_at IS NULL
       AND (created_at AT TIME ZONE $4)::date BETWEEN $2::date AND $3::date
     GROUP BY method ORDER BY cents DESC`,
    range,
  );
  const bills = await one<{ count: number }>(
    db,
    `SELECT count(*)::int AS count FROM bills
     WHERE store_id = $1 AND status = 'closed'
       AND (closed_at AT TIME ZONE $4)::date BETWEEN $2::date AND $3::date`,
    range,
  );
  const topItems = await rows<{ name: string; quantity: number; cents: number }>(
    db,
    `SELECT i.name, SUM(i.quantity)::int AS quantity, SUM(i.total_cents)::int AS cents
     FROM order_items i JOIN orders o ON o.id = i.order_id
     WHERE o.store_id = $1 AND o.status <> 'cancelled'
       AND (o.created_at AT TIME ZONE $4)::date BETWEEN $2::date AND $3::date
     GROUP BY i.name ORDER BY quantity DESC, i.name LIMIT 10`,
    range,
  );

  const totalCents = byMethod.reduce((sum, row) => sum + row.cents, 0);
  return {
    totalCents,
    billCount: bills!.count,
    averageTicketCents: bills!.count ? Math.round(totalCents / bills!.count) : 0,
    byMethod,
    topItems,
  };
}
