import { useEffect, useState } from 'react';
import QRCode from 'qrcode';
import type { OrderStatus, OrderView } from '../../server/orders.ts';
import { formatMoney, formatTime, orderNumber } from '../lib.ts';
import { kitchenTicketHtml, printHtml } from '../print.ts';
import type { Staff, TableView } from './context.ts';

const COLUMNS: { status: OrderStatus; title: string; next: OrderStatus; action: string }[] = [
  { status: 'new', title: 'Novos', next: 'accepted', action: 'Aceitar' },
  { status: 'accepted', title: 'Aceitos', next: 'preparing', action: 'Iniciar preparo' },
  { status: 'preparing', title: 'Em preparo', next: 'ready', action: 'Marcar pronto' },
  { status: 'ready', title: 'Prontos', next: 'delivered', action: 'Entregue' },
];

export function OrdersTab({ orders, staff }: { orders: OrderView[]; staff: Staff }) {
  const move = (order: OrderView, status: OrderStatus) =>
    staff.run(() => staff.call(`/api/staff/orders/${order.id}/status`, 'POST', { status }));

  return (
    <main className="board">
      {COLUMNS.map((column) => {
        const cards = orders.filter((order) => order.status === column.status);
        return (
          <section key={column.status} className="board-column">
            <h2>
              {column.title} <span>{cards.length}</span>
            </h2>
            {cards.length === 0 && <p className="muted empty">Nenhum pedido</p>}
            {cards.map((order) => (
              <OrderCard key={order.id} order={order} column={column} onMove={move} />
            ))}
          </section>
        );
      })}
    </main>
  );
}

function OrderCard({
  order,
  column,
  onMove,
}: {
  order: OrderView;
  column: (typeof COLUMNS)[number];
  onMove: (order: OrderView, status: OrderStatus) => void;
}) {
  const [confirmingCancel, setConfirmingCancel] = useState(false);

  return (
    <article className={`order-card order-${order.status}`}>
      <header>
        <strong>{orderNumber(order.number)}</strong>
        <span>{order.label}</span>
        <time>{formatTime(order.createdAt)}</time>
      </header>
      <ul>
        {order.items.map((item, index) => (
          <li key={index}>
            <strong>
              {item.quantity}× {item.name}
            </strong>
            {item.details.options.length > 0 && (
              <span>{item.details.options.map((option) => option.name).join(', ')}</span>
            )}
            {item.notes && <span className="order-note">Obs.: {item.notes}</span>}
          </li>
        ))}
      </ul>
      <p className="order-total">{formatMoney(order.totalCents)}</p>
      {confirmingCancel ? (
        <div className="card-actions">
          <button className="button danger grow" onClick={() => onMove(order, 'cancelled')}>
            Confirmar cancelamento
          </button>
          <button className="button" onClick={() => setConfirmingCancel(false)}>
            Voltar
          </button>
        </div>
      ) : (
        <>
          <button className="button primary" onClick={() => onMove(order, column.next)}>
            {column.action}
          </button>
          <div className="card-links">
            <button className="text-button" onClick={() => printHtml(kitchenTicketHtml(order))}>
              Imprimir
            </button>
            {order.status !== 'ready' && (
              <button className="text-button" onClick={() => setConfirmingCancel(true)}>
                Cancelar
              </button>
            )}
          </div>
        </>
      )}
    </article>
  );
}

export function TablesTab({ staff, onOpenBill }: { staff: Staff; onOpenBill: () => void }) {
  const [qrTable, setQrTable] = useState<TableView | null>(null);

  return (
    <main className="tables-grid">
      {staff.tables.map((table) => (
        <article key={table.id} className={`table-card ${table.billId ? 'open' : ''}`}>
          <h2>Mesa {table.number}</h2>
          {table.billId ? (
            <>
              <p className="muted">Código para o cliente</p>
              <p className="table-pin" aria-label={`Código ${table.pin}`}>
                {table.pin}
              </p>
              <button className="button" onClick={onOpenBill}>
                Ir para o caixa
              </button>
            </>
          ) : (
            <>
              <p className="muted">Livre</p>
              <button
                className="button primary"
                onClick={() => staff.run(() => staff.call(`/api/staff/tables/${table.id}/open`, 'POST'))}
              >
                Abrir mesa
              </button>
            </>
          )}
          <button className="text-button" onClick={() => setQrTable(table)}>
            Ver QR Code
          </button>
        </article>
      ))}
      {qrTable && <QrDialog table={qrTable} onClose={() => setQrTable(null)} />}
    </main>
  );
}

function QrDialog({ table, onClose }: { table: TableView; onClose: () => void }) {
  const url = `${window.location.origin}/m/${table.qrToken}`;
  const [image, setImage] = useState('');
  const isLocalhost = ['localhost', '127.0.0.1'].includes(window.location.hostname);

  useEffect(() => {
    void QRCode.toDataURL(url, { width: 320, margin: 1 }).then(setImage);
  }, [url]);

  return (
    <div className="sheet-backdrop" onClick={onClose}>
      <div
        className="dialog"
        role="dialog"
        aria-modal="true"
        aria-label={`QR Code da mesa ${table.number}`}
        onClick={(event) => event.stopPropagation()}
      >
        <h2>Mesa {table.number}</h2>
        {image && <img className="qr" src={image} alt={`QR Code da mesa ${table.number}`} />}
        <a className="break" href={url} target="_blank" rel="noreferrer">
          {url}
        </a>
        {isLocalhost && (
          <p className="hint">
            Este endereço só funciona neste computador. Para testar no celular, abra o painel pelo IP da rede.
          </p>
        )}
        <button className="button" onClick={onClose}>
          Fechar
        </button>
      </div>
    </div>
  );
}

export function AvailabilityTab({ staff }: { staff: Staff }) {
  const { menu } = staff;
  const toggle = (kind: 'products' | 'options', id: string, available: boolean) =>
    staff.run(() => staff.call(`/api/staff/${kind}/${id}/availability`, 'PATCH', { available }));

  return (
    <main className="availability">
      {menu.categories.map((category) => (
        <section key={category.id}>
          <h2>{category.name}</h2>
          {menu.products
            .filter((product) => product.categoryId === category.id)
            .map((product) => (
              <AvailabilityRow
                key={product.id}
                name={product.name}
                available={product.available}
                onChange={(value) => toggle('products', product.id, value)}
              />
            ))}
        </section>
      ))}
      {menu.optionGroups.map((group) => (
        <section key={group.id}>
          <h2>{group.name}</h2>
          {group.options.map((option) => (
            <AvailabilityRow
              key={option.id}
              name={option.name}
              available={option.available}
              onChange={(value) => toggle('options', option.id, value)}
            />
          ))}
        </section>
      ))}
    </main>
  );
}

function AvailabilityRow({
  name,
  available,
  onChange,
}: {
  name: string;
  available: boolean;
  onChange: (available: boolean) => void;
}) {
  return (
    <label className="availability-row">
      <span>{name}</span>
      <span className={available ? 'available' : 'sold-out'}>{available ? 'Disponível' : 'Esgotado'}</span>
      <input type="checkbox" role="switch" checked={available} onChange={(event) => onChange(event.target.checked)} />
    </label>
  );
}
