import { useCallback, useEffect, useRef, useState } from 'react';
import type { OrderStatus, OrderView } from '../../server/orders.ts';
import type { HalfPizzaRule } from '../../server/pricing.ts';
import { cartTotal, startingPrice, type CartLine, type Menu, type MenuProduct } from '../cart.ts';
import { api, ApiError, formatMinute, formatMoney, orderNumber, randomKey, storage, useLiveRefresh } from '../lib.ts';
import { ItemSheet } from './ItemSheet.tsx';

interface TableInfo {
  store: { name: string; halfPizzaRule: HalfPizzaRule };
  table: { number: number; open: boolean };
  menu: Menu;
}

interface Bill {
  orders: OrderView[];
  subtotalCents: number;
  serviceCents: number;
  discountCents: number;
  totalCents: number;
}

const STATUS_LABEL: Record<OrderStatus, string> = {
  new: 'Aguardando confirmação',
  accepted: 'Confirmado',
  preparing: 'Em preparo',
  ready: 'Pronto',
  delivered: 'Entregue',
  cancelled: 'Cancelado',
};

export function CustomerApp({ qrToken }: { qrToken: string }) {
  const tokenKey = `mesa:${qrToken}`;
  const [info, setInfo] = useState<TableInfo | null>(null);
  const [loadError, setLoadError] = useState('');
  const [token, setToken] = useState(() => storage.get(tokenKey));
  const [view, setView] = useState<'menu' | 'cart' | 'bill'>('menu');
  const [selected, setSelected] = useState<MenuProduct | null>(null);
  const [cart, setCart] = useState<CartLine[]>([]);
  const [askPin, setAskPin] = useState(false);
  const [sending, setSending] = useState(false);
  const [sendError, setSendError] = useState('');
  const [bill, setBill] = useState<Bill | null>(null);
  const [notice, setNotice] = useState('');

  // A chave so muda com o carrinho: repetir o envio apos uma falha de rede nao duplica o pedido.
  const orderKey = useRef(randomKey());

  const load = useCallback(async () => {
    try {
      setInfo(await api<TableInfo>(`/api/public/tables/${qrToken}`));
      setLoadError('');
    } catch (error) {
      setLoadError((error as Error).message);
    }
  }, [qrToken]);

  useEffect(() => void load(), [load]);

  const dropToken = useCallback(() => {
    storage.remove(tokenKey);
    setToken(null);
    setBill(null);
  }, [tokenKey]);

  const loadBill = useCallback(async () => {
    if (!token) return;
    try {
      setBill(await api<Bill>('/api/public/session/orders', { token }));
    } catch (error) {
      if ((error as ApiError).status === 401) {
        dropToken();
        void load();
      }
    }
  }, [token, dropToken, load]);

  useEffect(() => void loadBill(), [loadBill]);
  useLiveRefresh(token ? `/api/public/session/events?token=${encodeURIComponent(token)}` : null, loadBill);

  function changeCart(update: (lines: CartLine[]) => CartLine[]) {
    orderKey.current = randomKey();
    setCart(update);
    setSendError('');
  }

  async function send(activeToken = token) {
    if (!activeToken) {
      setAskPin(true);
      return;
    }
    setSending(true);
    setSendError('');
    try {
      await api<OrderView>('/api/public/orders', {
        method: 'POST',
        token: activeToken,
        body: { items: cart.map((line) => line.payload), idempotencyKey: orderKey.current },
      });
      orderKey.current = randomKey();
      setCart([]);
      setView('bill');
      setNotice('Pedido enviado. Aguarde a confirmação da equipe.');
      void loadBill();
    } catch (error) {
      const apiError = error as ApiError;
      if (apiError.status === 401) {
        dropToken();
        await load();
        setAskPin(true);
      } else {
        setSendError(apiError.message);
      }
    } finally {
      setSending(false);
    }
  }

  if (loadError && !info) {
    return (
      <main className="center-page">
        <h1>Não foi possível abrir o cardápio</h1>
        <p>{loadError}</p>
        <button className="button primary" onClick={() => void load()}>
          Tentar de novo
        </button>
      </main>
    );
  }
  if (!info) return <main className="center-page">Carregando cardápio…</main>;

  const cartCount = cart.reduce((sum, line) => sum + line.payload.quantity, 0);
  const cartTotalCents = cartTotal(cart);

  return (
    <div className="customer">
      <header className="customer-header">
        <div>
          <h1>{info.store.name}</h1>
          <p>Mesa {info.table.number}</p>
        </div>
        <button className="link-on-dark" onClick={() => setView(view === 'bill' ? 'menu' : 'bill')}>
          {view === 'bill' ? 'Ver cardápio' : 'Minha conta'}
        </button>
      </header>

      {view === 'menu' && (
        <>
          <nav className="category-nav" aria-label="Categorias">
            {info.menu.categories.map((category) => (
              <a key={category.id} href={`#c-${category.id}`}>
                {category.name}
              </a>
            ))}
          </nav>
          <main className="menu">
            {info.menu.categories.map((category) => (
              <section key={category.id} id={`c-${category.id}`} className="menu-section">
                <h2>{category.name}</h2>
                {!category.openNow && category.startMinute !== null && (
                  <p className="hint">
                    Disponível das {formatMinute(category.startMinute)} às {formatMinute(category.endMinute!)}.
                  </p>
                )}
                {info.menu.products
                  .filter((product) => product.categoryId === category.id)
                  .map((product) => {
                    const blocked = !product.available || !category.openNow;
                    return (
                      <button
                        key={product.id}
                        className="menu-item"
                        disabled={blocked}
                        onClick={() => setSelected(product)}
                      >
                        <span className="menu-item-text">
                          <strong>{product.name}</strong>
                          <span>{product.description}</span>
                        </span>
                        <span className="menu-item-price">
                          {!product.available ? (
                            'Esgotado'
                          ) : (
                            <>
                              {product.kind === 'pizza' && <small>a partir de</small>}
                              {formatMoney(startingPrice(product))}
                            </>
                          )}
                        </span>
                      </button>
                    );
                  })}
              </section>
            ))}
          </main>
        </>
      )}

      {view === 'cart' && (
        <main className="panel-page">
          <h2>Seu pedido</h2>
          {cart.length === 0 ? (
            <p className="muted">Nenhum item adicionado.</p>
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
          {sendError && (
            <p className="error" role="alert">
              {sendError}
            </p>
          )}
          <div className="stack">
            {cart.length > 0 && (
              <button className="button primary" disabled={sending} onClick={() => void send()}>
                {sending ? 'Enviando…' : `Enviar pedido · ${formatMoney(cartTotalCents)}`}
              </button>
            )}
            <button className="button" onClick={() => setView('menu')}>
              Continuar escolhendo
            </button>
          </div>
        </main>
      )}

      {view === 'bill' && (
        <main className="panel-page">
          <h2>Minha conta</h2>
          {notice && bill?.orders.at(-1)?.status === 'new' && <p className="success">{notice}</p>}
          {!token ? (
            <>
              <p className="muted">Digite o código da mesa para acompanhar os pedidos.</p>
              <button className="button primary" onClick={() => setAskPin(true)}>
                Digitar código
              </button>
            </>
          ) : !bill || bill.orders.length === 0 ? (
            <p className="muted">Nenhum pedido enviado ainda.</p>
          ) : (
            <>
              {bill.orders.map((order) => (
                <article key={order.id} className="bill-order">
                  <header>
                    <strong>Pedido {orderNumber(order.number)}</strong>
                    <span className={`status status-${order.status}`}>{STATUS_LABEL[order.status]}</span>
                  </header>
                  {order.items.map((item, index) => (
                    <div key={index}>
                      <div className="line-main">
                        <span>
                          {item.quantity}× {item.name}
                        </span>
                        <span>{formatMoney(item.totalCents)}</span>
                      </div>
                      {item.details.options.length > 0 && (
                        <p className="muted">{item.details.options.map((option) => option.name).join(', ')}</p>
                      )}
                    </div>
                  ))}
                </article>
              ))}
              {(bill.serviceCents > 0 || bill.discountCents > 0) && (
                <div className="line-main">
                  <span>Subtotal</span>
                  <span>{formatMoney(bill.subtotalCents)}</span>
                </div>
              )}
              {bill.serviceCents > 0 && (
                <div className="line-main">
                  <span>Serviço</span>
                  <span>{formatMoney(bill.serviceCents)}</span>
                </div>
              )}
              {bill.discountCents > 0 && (
                <div className="line-main">
                  <span>Desconto</span>
                  <span>- {formatMoney(bill.discountCents)}</span>
                </div>
              )}
              <div className="line-main bill-total">
                <strong>Total da mesa</strong>
                <strong>{formatMoney(bill.totalCents)}</strong>
              </div>
              <p className="muted">O pagamento é feito no caixa.</p>
            </>
          )}
        </main>
      )}

      {view === 'menu' && cartCount > 0 && (
        <button className="cart-bar" onClick={() => setView('cart')}>
          <span>
            {cartCount} {cartCount > 1 ? 'itens' : 'item'}
          </span>
          <strong>Ver pedido · {formatMoney(cartTotalCents)}</strong>
        </button>
      )}

      {selected && (
        <ItemSheet
          menu={info.menu}
          product={selected}
          rule={info.store.halfPizzaRule}
          onClose={() => setSelected(null)}
          onAdd={(line) => {
            changeCart((lines) => [...lines, { ...line, id: randomKey() }]);
            setSelected(null);
          }}
        />
      )}

      {askPin && (
        <PinDialog
          qrToken={qrToken}
          tableNumber={info.table.number}
          onCancel={() => setAskPin(false)}
          onJoined={(newToken) => {
            storage.set(tokenKey, newToken);
            setToken(newToken);
            setAskPin(false);
            if (cart.length > 0) void send(newToken);
          }}
        />
      )}
    </div>
  );
}

interface PinProps {
  qrToken: string;
  tableNumber: number;
  onCancel: () => void;
  onJoined: (token: string) => void;
}

function PinDialog({ qrToken, tableNumber, onCancel, onJoined }: PinProps) {
  const [pin, setPin] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    setBusy(true);
    setError('');
    try {
      const { token } = await api<{ token: string }>(`/api/public/tables/${qrToken}/join`, {
        method: 'POST',
        body: { pin },
      });
      onJoined(token);
    } catch (failure) {
      setError((failure as Error).message);
      setBusy(false);
    }
  }

  return (
    <div className="sheet-backdrop" onClick={onCancel}>
      <form
        className="dialog"
        role="dialog"
        aria-modal="true"
        aria-label="Código da mesa"
        onSubmit={submit}
        onClick={(event) => event.stopPropagation()}
      >
        <h2>Código da mesa {tableNumber}</h2>
        <p className="muted">Peça ao atendente o código de 4 dígitos. Todos da mesa usam o mesmo.</p>
        <input
          className="pin-input"
          inputMode="numeric"
          autoComplete="off"
          autoFocus
          maxLength={4}
          pattern="[0-9]{4}"
          aria-label="Código de 4 dígitos"
          value={pin}
          onChange={(event) => setPin(event.target.value.replace(/\D/g, ''))}
        />
        {error && (
          <p className="error" role="alert">
            {error}
          </p>
        )}
        <div className="stack">
          <button className="button primary" disabled={pin.length !== 4 || busy}>
            {busy ? 'Confirmando…' : 'Confirmar'}
          </button>
          <button type="button" className="button" onClick={onCancel}>
            Cancelar
          </button>
        </div>
      </form>
    </div>
  );
}
