import { one, type Queryable } from './db.ts';

export interface BillTotals {
  subtotalCents: number;
  serviceCents: number;
  discountCents: number;
  totalCents: number;
  paidCents: number;
  remainingCents: number;
}

export function computeTotals(input: {
  subtotalCents: number;
  servicePercent: number;
  serviceApplied: boolean;
  discountCents: number;
  paidCents: number;
}): BillTotals {
  const { subtotalCents, servicePercent, serviceApplied, discountCents, paidCents } = input;
  const serviceCents = serviceApplied ? Math.round((subtotalCents * servicePercent) / 100) : 0;
  const totalCents = Math.max(0, subtotalCents + serviceCents - discountCents);
  return {
    subtotalCents,
    serviceCents,
    discountCents,
    totalCents,
    paidCents,
    remainingCents: totalCents - paidCents,
  };
}

export async function loadTotals(db: Queryable, billId: string): Promise<BillTotals> {
  const row = await one<{
    subtotalCents: number;
    servicePercent: number;
    serviceApplied: boolean;
    discountCents: number;
    paidCents: number;
  }>(
    db,
    `SELECT b.service_applied AS "serviceApplied", b.discount_cents AS "discountCents",
            s.service_percent AS "servicePercent",
            (SELECT COALESCE(SUM(o.total_cents), 0)::int FROM orders o
             WHERE o.bill_id = b.id AND o.status <> 'cancelled') AS "subtotalCents",
            (SELECT COALESCE(SUM(p.amount_cents), 0)::int FROM payments p
             WHERE p.bill_id = b.id AND p.voided_at IS NULL) AS "paidCents"
     FROM bills b JOIN stores s ON s.id = b.store_id WHERE b.id = $1`,
    [billId],
  );
  return computeTotals(row!);
}
