import { useCallback, useEffect, useMemo, useState } from 'react';
import type { OrderView } from '../../server/orders.ts';
import type { Menu } from '../cart.ts';
import { api, ApiError, storage, useLiveRefresh } from '../lib.ts';
import { CashTab } from './CashTab.tsx';
import type { Me, Staff, TableView } from './context.ts';
import { MenuEditor } from './MenuEditor.tsx';
import { NewOrderTab } from './NewOrderTab.tsx';
import { AvailabilityTab, OrdersTab, TablesTab } from './OperationTabs.tsx';
import { ReportsTab, TeamTab } from './AdminTabs.tsx';

type Tab = 'orders' | 'new' | 'cash' | 'tables' | 'availability' | 'menu' | 'reports' | 'team';

const TABS: { id: Tab; label: string; adminOnly?: boolean }[] = [
  { id: 'orders', label: 'Pedidos' },
  { id: 'new', label: 'Novo pedido' },
  { id: 'cash', label: 'Caixa' },
  { id: 'tables', label: 'Mesas' },
  { id: 'availability', label: 'Disponibilidade' },
  { id: 'menu', label: 'Cardápio', adminOnly: true },
  { id: 'reports', label: 'Relatórios', adminOnly: true },
  { id: 'team', label: 'Equipe', adminOnly: true },
];

export function StaffApp({ slug }: { slug: string }) {
  const tokenKey = `equipe:${slug}`;
  const [token, setToken] = useState(() => storage.get(tokenKey));
  const [tab, setTab] = useState<Tab>('orders');
  const [me, setMe] = useState<Me | null>(null);
  const [orders, setOrders] = useState<OrderView[]>([]);
  const [tables, setTables] = useState<TableView[]>([]);
  const [menu, setMenu] = useState<Menu | null>(null);
  const [version, setVersion] = useState(0);
  const [error, setError] = useState('');

  const signOut = useCallback(() => {
    storage.remove(tokenKey);
    setToken(null);
    setMe(null);
  }, [tokenKey]);

  const handleError = useCallback(
    (failure: unknown) => {
      if ((failure as ApiError).status === 401) signOut();
      else setError((failure as Error).message);
    },
    [signOut],
  );

  const refresh = useCallback(async () => {
    if (!token) return;
    try {
      const [nextMe, nextOrders, nextTables, nextMenu] = await Promise.all([
        api<Me>('/api/staff/me', { token }),
        api<OrderView[]>('/api/staff/orders', { token }),
        api<TableView[]>('/api/staff/tables', { token }),
        api<Menu>('/api/staff/menu', { token }),
      ]);
      setMe(nextMe);
      setOrders(nextOrders);
      setTables(nextTables);
      setMenu(nextMenu);
      setVersion((value) => value + 1);
    } catch (failure) {
      handleError(failure);
    }
  }, [token, handleError]);

  useEffect(() => void refresh(), [refresh]);
  useLiveRefresh(token ? `/api/staff/events?token=${encodeURIComponent(token)}` : null, refresh);

  const call = useCallback(
    <T,>(path: string, method = 'GET', body?: unknown) => api<T>(path, { method, body, token }),
    [token],
  );

  const run = useCallback(
    async (action: () => Promise<unknown>) => {
      setError('');
      try {
        await action();
        await refresh();
        return true;
      } catch (failure) {
        handleError(failure);
        return false;
      }
    },
    [refresh, handleError],
  );

  const staff = useMemo<Staff | null>(
    () => (me && menu ? { me, menu, tables, version, call, run } : null),
    [me, menu, tables, version, call, run],
  );

  if (!token) {
    return (
      <Login
        slug={slug}
        onLogin={(newToken) => {
          storage.set(tokenKey, newToken);
          setToken(newToken);
        }}
      />
    );
  }
  if (!staff) return <main className="center-page">Carregando…</main>;

  const newCount = orders.filter((order) => order.status === 'new').length;
  document.title = newCount ? `(${newCount}) ${staff.me.store.name}` : staff.me.store.name;
  const isAdmin = staff.me.user.role === 'admin';

  return (
    <div className="staff">
      <header className="staff-header">
        <strong>{staff.me.store.name}</strong>
        <nav aria-label="Seções">
          {TABS.filter((item) => isAdmin || !item.adminOnly).map(({ id, label }) => (
            <button key={id} className={tab === id ? 'active' : ''} onClick={() => setTab(id)}>
              {label}
              {id === 'orders' && newCount > 0 && <span className="badge">{newCount}</span>}
            </button>
          ))}
        </nav>
        <span className="staff-user">{staff.me.user.name}</span>
        <button className="link-on-dark" onClick={signOut}>
          Sair
        </button>
      </header>

      {error && (
        <div className="toast" role="alert">
          <span>{error}</span>
          <button className="icon-button" onClick={() => setError('')} aria-label="Fechar aviso">
            ×
          </button>
        </div>
      )}

      {tab === 'orders' && <OrdersTab orders={orders} staff={staff} />}
      {tab === 'new' && <NewOrderTab staff={staff} />}
      {tab === 'cash' && <CashTab staff={staff} />}
      {tab === 'tables' && <TablesTab staff={staff} onOpenBill={() => setTab('cash')} />}
      {tab === 'availability' && <AvailabilityTab staff={staff} />}
      {tab === 'menu' && isAdmin && <MenuEditor staff={staff} />}
      {tab === 'reports' && isAdmin && <ReportsTab staff={staff} />}
      {tab === 'team' && isAdmin && <TeamTab staff={staff} />}
    </div>
  );
}

function Login({ slug, onLogin }: { slug: string; onLogin: (token: string) => void }) {
  const [login, setLogin] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    setError('');
    try {
      const { token } = await api<{ token: string }>('/api/staff/login', {
        method: 'POST',
        body: { slug, login, password },
      });
      onLogin(token);
    } catch (failure) {
      setError((failure as Error).message);
    }
  }

  return (
    <main className="center-page">
      <form className="dialog static" onSubmit={submit}>
        <h1>Entrar no sistema</h1>
        <label className="field">
          <span>Usuário</span>
          <input
            autoFocus
            autoCapitalize="none"
            autoComplete="username"
            value={login}
            onChange={(event) => setLogin(event.target.value)}
          />
        </label>
        <label className="field">
          <span>Senha</span>
          <input
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
          />
        </label>
        {error && (
          <p className="error" role="alert">
            {error}
          </p>
        )}
        <button className="button primary">Entrar</button>
      </form>
    </main>
  );
}
