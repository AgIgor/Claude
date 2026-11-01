import { useEffect, useState } from 'react';
import type { BillDetail, BillSummary, CashSummary, PaymentMethod } from '../../server/cashier.ts';
import { formatMoney, formatTime, moneyInput, orderNumber, parseMoney, PAYMENT_LABEL } from '../lib.ts';
import { billReceiptHtml, printHtml } from '../print.ts';
import type { Staff } from './context.ts';

interface ClosedCash {
  expectedCashCents: number;
  countedCents: number;
  differenceCents: number;
}

export function CashTab({ staff }: { staff: Staff }) {
  const [cash, setCash] = useState<CashSummary | null | undefined>(undefined);
  const [bills, setBills] = useState<BillSummary[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [closed, setClosed] = useState<ClosedCash | null>(null);

  useEffect(() => {
    let current = true;
    void Promise.all([
      staff.call<{ current: CashSummary | null }>('/api/staff/cash'),
      staff.call<BillSummary[]>('/api/staff/bills'),
    ])
      .then(([cashState, openBills]) => {
        if (!current) return;
        setCash(cashState.current);
        setBills(openBills);
      })
      .catch(() => undefined);
    return () => {
      current = false;
    };
  }, [staff.version, staff.call]);

  if (cash === undefined) return <main className="panel-page">Carregando…</main>;

  if (cash === null) {
    return (
      <main className="panel-page narrow">
        {closed && (
          <div className={closed.differenceCents === 0 ? 'success' : 'hint'}>
            <strong>Caixa fechado.</strong> Esperado {formatMoney(closed.expectedCashCents)}, contado{' '}
            {formatMoney(closed.countedCents)}. Diferença: {formatMoney(closed.differenceCents)}.
          </div>
        )}
        <AmountForm
          title="Abrir caixa"
          label="Dinheiro inicial na gaveta"
          action="Abrir caixa"
          allowZero
          onSubmit={(openingCents) => {
            setClosed(null);
            return staff.run(() => staff.call('/api/staff/cash/open', 'POST', { openingCents }));
          }}
        />
      </main>
    );
  }

  return (
    <main className="cashier">
      <section aria-label="Contas abertas" className="bill-list">
        <h2>Contas abertas</h2>
        {bills.length === 0 && <p className="muted empty">Nenhuma conta aberta</p>}
        {bills.map((bill) => (
          <button
            key={bill.id}
            className={`bill-tile ${selectedId === bill.id ? 'selected' : ''}`}
            onClick={() => setSelectedId(bill.id)}
          >
            <strong>{bill.label}</strong>
            <span>{formatMoney(bill.remainingCents)}</span>
            <small>
              {bill.remainingCents === 0 && bill.totalCents > 0 ? 'Pago · ' : ''}
              {bill.activeOrders} em andamento
            </small>
          </button>
        ))}
      </section>

      <section aria-label="Conta selecionada" className="bill-detail">
        {selectedId && bills.some((bill) => bill.id === selectedId) ? (
          <BillPanel key={selectedId} billId={selectedId} staff={staff} onClosed={() => setSelectedId(null)} />
        ) : (
          <p className="muted empty">Selecione uma conta para receber.</p>
        )}
      </section>

      <CashPanel cash={cash} staff={staff} onClosed={setClosed} />
    </main>
  );
}

function BillPanel({ billId, staff, onClosed }: { billId: string; staff: Staff; onClosed: () => void }) {
  const [bill, setBill] = useState<BillDetail | null>(null);
  const [method, setMethod] = useState<PaymentMethod>('cash');
  const [amount, setAmount] = useState('');
  const [received, setReceived] = useState('');
  const [discount, setDiscount] = useState('');

  useEffect(() => {
    let current = true;
    void staff
      .call<BillDetail>(`/api/staff/bills/${billId}`)
      .then((next) => {
        if (!current) return;
        setBill(next);
        setAmount(next.remainingCents > 0 ? moneyInput(next.remainingCents) : '');
        setReceived('');
        setDiscount(next.discountCents ? moneyInput(next.discountCents) : '');
      })
      .catch(() => undefined);
    return () => {
      current = false;
    };
  }, [billId, staff.version, staff.call]);

  if (!bill) return <p className="muted">Carregando…</p>;

  const amountCents = parseMoney(amount);
  const receivedCents = parseMoney(received);
  const changeCents = amountCents !== null && receivedCents !== null ? receivedCents - amountCents : null;
  const canPay =
    amountCents !== null &&
    amountCents > 0 &&
    amountCents <= bill.remainingCents &&
    (method !== 'cash' || received.trim() === '' || (changeCents !== null && changeCents >= 0));

  const adjust = (serviceApplied: boolean, discountCents: number) =>
    staff.run(() => staff.call(`/api/staff/bills/${billId}/adjust`, 'POST', { serviceApplied, discountCents }));

  function applyDiscount() {
    const cents = discount.trim() === '' ? 0 : parseMoney(discount);
    if (cents !== null) void adjust(bill!.serviceApplied, cents);
  }

  const payment = () =>
    staff.run(() =>
      staff.call(`/api/staff/bills/${billId}/payments`, 'POST', {
        method,
        amountCents,
        ...(method === 'cash' && receivedCents !== null ? { receivedCents } : {}),
      }),
    );

  async function close() {
    if (await staff.run(() => staff.call(`/api/staff/bills/${billId}/close`, 'POST'))) onClosed();
  }

  const orders = bill.orders.filter((order) => order.status !== 'cancelled');

  return (
    <>
      <header className="bill-header">
        <h2>{bill.label}</h2>
        <button className="button" onClick={() => printHtml(billReceiptHtml(bill, staff.me.store.name))}>
          Imprimir conta
        </button>
      </header>

      {orders.length === 0 && <p className="muted">Nenhum pedido nesta conta.</p>}
      {orders.map((order) => (
        <div key={order.id} className="bill-order-lines">
          <p className="muted">Pedido {orderNumber(order.number)}</p>
          {order.items.map((item, index) => (
            <div key={index} className="line-main">
              <span>
                {item.quantity}× {item.name}
              </span>
              <span>{formatMoney(item.totalCents)}</span>
            </div>
          ))}
        </div>
      ))}

      <div className="totals">
        <div className="line-main">
          <span>Subtotal</span>
          <span>{formatMoney(bill.subtotalCents)}</span>
        </div>
        {staff.me.store.servicePercent > 0 && (
          <label className="line-main check-line">
            <span>
              <input
                type="checkbox"
                checked={bill.serviceApplied}
                onChange={(event) => void adjust(event.target.checked, bill.discountCents)}
              />{' '}
              Serviço ({staff.me.store.servicePercent}%)
            </span>
            <span>{formatMoney(bill.serviceCents)}</span>
          </label>
        )}
        <div className="line-main">
          <label className="inline-field">
            <span>Desconto (R$)</span>
            <input
              inputMode="decimal"
              value={discount}
              onChange={(event) => setDiscount(event.target.value)}
              onBlur={applyDiscount}
            />
          </label>
          <span>- {formatMoney(bill.discountCents)}</span>
        </div>
        <div className="line-main bill-total">
          <strong>Total</strong>
          <strong>{formatMoney(bill.totalCents)}</strong>
        </div>
        {bill.payments.map((item) => (
          <div key={item.id} className={`line-main payment-line ${item.voidedAt ? 'voided' : ''}`}>
            <span>
              {PAYMENT_LABEL[item.method]} · {formatTime(item.createdAt)}
              {item.changeCents > 0 && ` · troco ${formatMoney(item.changeCents)}`}
              {item.voidedAt && ' · estornado'}
            </span>
            <span className="payment-actions">
              {formatMoney(item.amountCents)}
              {!item.voidedAt && staff.me.user.role === 'admin' && (
                <button
                  className="text-button"
                  onClick={() => staff.run(() => staff.call(`/api/staff/payments/${item.id}/void`, 'POST'))}
                >
                  Estornar
                </button>
              )}
            </span>
          </div>
        ))}
        <div className="line-main bill-total">
          <strong>Falta pagar</strong>
          <strong>{formatMoney(bill.remainingCents)}</strong>
        </div>
      </div>

      {bill.remainingCents > 0 ? (
        <div className="payment-form">
          <div className="segmented" role="group" aria-label="Forma de pagamento">
            {(Object.keys(PAYMENT_LABEL) as PaymentMethod[]).map((key) => (
              <button key={key} className={method === key ? 'active' : ''} onClick={() => setMethod(key)}>
                {PAYMENT_LABEL[key]}
              </button>
            ))}
          </div>
          <div className="form-row">
            <label className="field">
              <span>Valor a pagar</span>
              <input inputMode="decimal" value={amount} onChange={(event) => setAmount(event.target.value)} />
            </label>
            {method === 'cash' && (
              <label className="field">
                <span>Recebido</span>
                <input inputMode="decimal" value={received} onChange={(event) => setReceived(event.target.value)} />
              </label>
            )}
          </div>
          {method === 'cash' && changeCents !== null && (
            <p className={changeCents < 0 ? 'error' : 'success'}>
              {changeCents < 0 ? `Faltam ${formatMoney(-changeCents)}` : `Troco: ${formatMoney(changeCents)}`}
            </p>
          )}
          <button className="button primary" disabled={!canPay} onClick={() => void payment()}>
            Registrar pagamento
          </button>
        </div>
      ) : (
        <button className="button primary" onClick={() => void close()}>
          Fechar conta
        </button>
      )}
    </>
  );
}

function CashPanel({
  cash,
  staff,
  onClosed,
}: {
  cash: CashSummary;
  staff: Staff;
  onClosed: (result: ClosedCash) => void;
}) {
  const [movement, setMovement] = useState<'withdrawal' | 'deposit'>('withdrawal');
  const [amount, setAmount] = useState('');
  const [reason, setReason] = useState('');
  const [closing, setClosing] = useState(false);

  const amountCents = parseMoney(amount);

  async function addMovement() {
    const ok = await staff.run(() =>
      staff.call('/api/staff/cash/movements', 'POST', { type: movement, amountCents, reason }),
    );
    if (ok) {
      setAmount('');
      setReason('');
    }
  }

  return (
    <section aria-label="Resumo do caixa" className="cash-panel">
      <h2>Caixa aberto</h2>
      <p className="muted">
        {cash.openedBy} às {formatTime(cash.openedAt)}
      </p>
      <div className="line-main">
        <span>Dinheiro inicial</span>
        <span>{formatMoney(cash.openingCents)}</span>
      </div>
      {(Object.keys(PAYMENT_LABEL) as PaymentMethod[]).map((key) => (
        <div key={key} className="line-main">
          <span>{PAYMENT_LABEL[key]}</span>
          <span>{formatMoney(cash.byMethod[key])}</span>
        </div>
      ))}
      <div className="line-main">
        <span>Suprimentos</span>
        <span>{formatMoney(cash.depositsCents)}</span>
      </div>
      <div className="line-main">
        <span>Sangrias</span>
        <span>- {formatMoney(cash.withdrawalsCents)}</span>
      </div>
      <div className="line-main bill-total">
        <strong>Dinheiro na gaveta</strong>
        <strong>{formatMoney(cash.expectedCashCents)}</strong>
      </div>

      <h3>Sangria ou suprimento</h3>
      <div className="segmented" role="group" aria-label="Tipo de movimento">
        <button className={movement === 'withdrawal' ? 'active' : ''} onClick={() => setMovement('withdrawal')}>
          Sangria
        </button>
        <button className={movement === 'deposit' ? 'active' : ''} onClick={() => setMovement('deposit')}>
          Suprimento
        </button>
      </div>
      <label className="field">
        <span>Valor</span>
        <input inputMode="decimal" value={amount} onChange={(event) => setAmount(event.target.value)} />
      </label>
      <label className="field">
        <span>Motivo</span>
        <input maxLength={120} value={reason} onChange={(event) => setReason(event.target.value)} />
      </label>
      <button
        className="button"
        disabled={!amountCents || reason.trim() === ''}
        onClick={() => void addMovement()}
      >
        Registrar
      </button>

      {closing ? (
        <AmountForm
          title="Fechar caixa"
          label="Dinheiro contado na gaveta"
          action="Confirmar fechamento"
          allowZero
          onSubmit={(countedCents) =>
            staff.run(async () => {
              onClosed(await staff.call<ClosedCash>('/api/staff/cash/close', 'POST', { countedCents }));
            })
          }
        />
      ) : (
        <button className="button danger-outline" onClick={() => setClosing(true)}>
          Fechar caixa
        </button>
      )}
    </section>
  );
}

function AmountForm({
  title,
  label,
  action,
  allowZero,
  onSubmit,
}: {
  title: string;
  label: string;
  action: string;
  allowZero?: boolean;
  onSubmit: (cents: number) => unknown;
}) {
  const [value, setValue] = useState('');
  const cents = parseMoney(value);
  const valid = cents !== null && (allowZero || cents > 0);

  return (
    <form
      className="amount-form"
      onSubmit={(event) => {
        event.preventDefault();
        if (valid) void onSubmit(cents);
      }}
    >
      <h2>{title}</h2>
      <label className="field">
        <span>{label}</span>
        <input autoFocus inputMode="decimal" value={value} onChange={(event) => setValue(event.target.value)} />
      </label>
      <button className="button primary" disabled={!valid}>
        {action}
      </button>
    </form>
  );
}
