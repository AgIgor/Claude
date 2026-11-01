import { useRef, useState } from 'react';
import { cartTotal, startingPrice, type CartLine, type MenuProduct } from '../cart.ts';
import { ItemSheet } from '../customer/ItemSheet.tsx';
import { formatMoney, randomKey } from '../lib.ts';
import type { Staff } from './context.ts';

export function NewOrderTab({ staff }: { staff: Staff }) {
  const { menu, tables, me } = staff;
  const [mode, setMode] = useState<'table' | 'counter'>('counter');
  const [tableId, setTableId] = useState('');
  const [customer, setCustomer] = useState('');
  const [search, setSearch] = useState('');
  const [selected, setSelected] = useState<MenuProduct | null>(null);
  const [cart, setCart] = useState<CartLine[]>([]);
  const [saved, setSaved] = useState('');
  const orderKey = useRef(randomKey());

  function changeCart(update: (lines: CartLine[]) => CartLine[]) {
    orderKey.current = randomKey();
    setCart(update);
    setSaved('');
  }

  const term = search.trim().toLocaleLowerCase('pt-BR');
  const canSend = cart.length > 0 && (mode === 'table' ? Boolean(tableId) : customer.trim() !== '');

  async function send() {
    const target = mode === 'table' ? { tableId } : { label: customer.trim() };
    const ok = await staff.run(() =>
      staff.call('/api/staff/orders', 'POST', {
        ...target,
        items: cart.map((line) => line.payload),
        idempotencyKey: orderKey.current,
      }),
    );
    if (!ok) return;
    orderKey.current = randomKey();
    setCart([]);
    setCustomer('');
    setSaved('Pedido enviado para a cozinha.');
  }

  return (
    <main className="order-entry">
      <section className="order-menu" aria-label="Cardápio">
        <input
          className="search"
          type="search"
          aria-label="Buscar no cardápio"
          placeholder="Buscar no cardápio"
          value={search}
          onChange={(event) => setSearch(event.target.value)}
        />
        {menu.categories.map((category) => {
          const products = menu.products.filter(
            (product) =>
              product.categoryId === category.id && product.name.toLocaleLowerCase('pt-BR').includes(term),
          );
          if (products.length === 0) return null;
          return (
            <div key={category.id}>
              <h2>{category.name}</h2>
              <div className="product-grid">
                {products.map((product) => (
                  <button
                    key={product.id}
                    className="product-tile"
                    disabled={!product.available || !category.openNow}
                    onClick={() => setSelected(product)}
                  >
                    <strong>{product.name}</strong>
                    <span>{product.available ? formatMoney(startingPrice(product)) : 'Esgotado'}</span>
                  </button>
                ))}
              </div>
            </div>
          );
        })}
      </section>

      <aside className="order-summary" aria-label="Pedido atual">
        <div className="segmented" role="group" aria-label="Destino do pedido">
          <button className={mode === 'counter' ? 'active' : ''} onClick={() => setMode('counter')}>
            Balcão
          </button>
          <button className={mode === 'table' ? 'active' : ''} onClick={() => setMode('table')}>
            Mesa
          </button>
        </div>
        {mode === 'counter' ? (
          <label className="field">
            <span>Nome do cliente</span>
            <input maxLength={60} value={customer} onChange={(event) => setCustomer(event.target.value)} />
          </label>
        ) : (
          <label className="field">
            <span>Mesa</span>
            <select value={tableId} onChange={(event) => setTableId(event.target.value)}>
              <option value="">Escolha a mesa</option>
              {tables.map((table) => (
                <option key={table.id} value={table.id}>
                  Mesa {table.number}
                  {table.billId ? ' (aberta)' : ''}
                </option>
              ))}
            </select>
          </label>
        )}

        {cart.length === 0 ? (
          <p className="muted">{saved || 'Escolha os itens no cardápio.'}</p>
        ) : (
          <ul className="line-list">
            {cart.map((line) => (
              <li key={line.id}>
                <div className="line-main">
                  <strong>
                    {line.payload.quantity}× {line.name}
                  </strong>
                  <span>{formatMoney(line.unitPriceCents * line.payload.quantity)}</span>
                </div>
                {line.details.length > 0 && <p className="muted">{line.details.join(', ')}</p>}
                {line.notes && <p className="muted">Obs.: {line.notes}</p>}
                <button
                  className="text-button"
                  onClick={() => changeCart((lines) => lines.filter((candidate) => candidate.id !== line.id))}
                >
                  Remover
                </button>
              </li>
            ))}
          </ul>
        )}
        <button className="button primary" disabled={!canSend} onClick={() => void send()}>
          Enviar · {formatMoney(cartTotal(cart))}
        </button>
      </aside>

      {selected && (
        <ItemSheet
          menu={menu}
          product={selected}
          rule={me.store.halfPizzaRule}
          onClose={() => setSelected(null)}
          onAdd={(line) => {
            changeCart((lines) => [...lines, { ...line, id: randomKey() }]);
            setSelected(null);
          }}
        />
      )}
    </main>
  );
}
