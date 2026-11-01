import { useEffect, useState } from 'react';
import type { PaymentMethod } from '../../server/cashier.ts';
import { formatMoney, PAYMENT_LABEL } from '../lib.ts';
import type { Staff } from './context.ts';

interface Report {
  totalCents: number;
  billCount: number;
  averageTicketCents: number;
  byMethod: { method: PaymentMethod; cents: number }[];
  topItems: { name: string; quantity: number; cents: number }[];
}

interface ClosedCash {
  id: string;
  openedBy: string;
  closedBy: string;
  closedAt: string;
  expectedCents: number;
  countedCents: number;
}

const today = () => new Date().toLocaleDateString('en-CA');

export function ReportsTab({ staff }: { staff: Staff }) {
  const [from, setFrom] = useState(today);
  const [to, setTo] = useState(today);
  const [report, setReport] = useState<Report | null>(null);
  const [history, setHistory] = useState<ClosedCash[]>([]);

  useEffect(() => {
    if (!from || !to) return;
    void staff.run(async () => {
      setReport(await staff.call<Report>(`/api/admin/reports/sales?from=${from}&to=${to}`));
      setHistory(await staff.call<ClosedCash[]>('/api/admin/cash/history'));
    });
    // `run` atualiza a tela; depender da versao criaria um ciclo.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [from, to]);

  return (
    <main className="panel-page wide">
      <div className="form-row wrap">
        <label className="field">
          <span>De</span>
          <input type="date" value={from} onChange={(event) => setFrom(event.target.value)} />
        </label>
        <label className="field">
          <span>Até</span>
          <input type="date" value={to} onChange={(event) => setTo(event.target.value)} />
        </label>
      </div>

      {report && (
        <>
          <div className="stat-row">
            <div className="stat">
              <span>Recebido</span>
              <strong>{formatMoney(report.totalCents)}</strong>
            </div>
            <div className="stat">
              <span>Contas fechadas</span>
              <strong>{report.billCount}</strong>
            </div>
            <div className="stat">
              <span>Ticket médio</span>
              <strong>{formatMoney(report.averageTicketCents)}</strong>
            </div>
          </div>

          <div className="report-columns">
            <section>
              <h2>Por forma de pagamento</h2>
              {report.byMethod.length === 0 && <p className="muted">Nenhum pagamento no período.</p>}
              {report.byMethod.map((row) => (
                <div key={row.method} className="line-main report-line">
                  <span>{PAYMENT_LABEL[row.method]}</span>
                  <span>{formatMoney(row.cents)}</span>
                </div>
              ))}
            </section>
            <section>
              <h2>Mais vendidos</h2>
              {report.topItems.length === 0 && <p className="muted">Nenhuma venda no período.</p>}
              {report.topItems.map((item) => (
                <div key={item.name} className="line-main report-line">
                  <span>
                    {item.quantity}× {item.name}
                  </span>
                  <span>{formatMoney(item.cents)}</span>
                </div>
              ))}
            </section>
          </div>
        </>
      )}

      <section>
        <h2>Fechamentos de caixa</h2>
        {history.length === 0 ? (
          <p className="muted">Nenhum caixa fechado.</p>
        ) : (
          <div className="table-scroll">
            <table>
              <thead>
                <tr>
                  <th>Fechamento</th>
                  <th>Operador</th>
                  <th>Esperado</th>
                  <th>Contado</th>
                  <th>Diferença</th>
                </tr>
              </thead>
              <tbody>
                {history.map((row) => {
                  const difference = row.countedCents - row.expectedCents;
                  return (
                    <tr key={row.id}>
                      <td>{new Date(row.closedAt).toLocaleString('pt-BR', { dateStyle: 'short', timeStyle: 'short' })}</td>
                      <td>{row.closedBy}</td>
                      <td>{formatMoney(row.expectedCents)}</td>
                      <td>{formatMoney(row.countedCents)}</td>
                      <td className={difference < 0 ? 'error-text' : ''}>{formatMoney(difference)}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        )}
      </section>
    </main>
  );
}

interface User {
  id: string;
  name: string;
  login: string;
  role: 'admin' | 'staff';
  active: boolean;
}

const ROLE_LABEL = { admin: 'Administrador', staff: 'Equipe' };

export function TeamTab({ staff }: { staff: Staff }) {
  const [users, setUsers] = useState<User[]>([]);
  const [name, setName] = useState('');
  const [login, setLogin] = useState('');
  const [password, setPassword] = useState('');
  const [role, setRole] = useState<User['role']>('staff');
  const [resetFor, setResetFor] = useState<string | null>(null);
  const [newPassword, setNewPassword] = useState('');

  const load = () => staff.call<User[]>('/api/admin/users').then(setUsers);

  useEffect(() => {
    void staff.run(load);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  async function create(event: React.FormEvent) {
    event.preventDefault();
    const ok = await staff.run(async () => {
      await staff.call('/api/admin/users', 'POST', { name: name.trim(), login: login.trim(), password, role });
      await load();
    });
    if (ok) {
      setName('');
      setLogin('');
      setPassword('');
    }
  }

  const save = (user: User, change: Partial<User> & { password?: string }) =>
    staff.run(async () => {
      const { name: userName, role: userRole, active } = user;
      await staff.call(`/api/admin/users/${user.id}`, 'PATCH', {
        name: userName,
        role: userRole,
        active,
        ...change,
      });
      await load();
    });

  async function resetPassword(user: User) {
    if (await save(user, { password: newPassword })) {
      setResetFor(null);
      setNewPassword('');
    }
  }

  return (
    <main className="panel-page wide">
      <h2>Equipe</h2>
      <div className="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Nome</th>
              <th>Usuário</th>
              <th>Perfil</th>
              <th>Situação</th>
              <th aria-label="Ações" />
            </tr>
          </thead>
          <tbody>
            {users.map((user) => (
              <tr key={user.id}>
                <td>{user.name}</td>
                <td>{user.login}</td>
                <td>{ROLE_LABEL[user.role]}</td>
                <td>{user.active ? 'Ativo' : 'Desativado'}</td>
                <td className="row-actions">
                  {resetFor === user.id ? (
                    <>
                      <input
                        type="password"
                        aria-label={`Nova senha de ${user.name}`}
                        placeholder="Nova senha"
                        autoComplete="new-password"
                        value={newPassword}
                        onChange={(event) => setNewPassword(event.target.value)}
                      />
                      <button
                        className="text-button"
                        disabled={newPassword.length < 6}
                        onClick={() => void resetPassword(user)}
                      >
                        Salvar
                      </button>
                    </>
                  ) : (
                    <button className="text-button" onClick={() => setResetFor(user.id)}>
                      Trocar senha
                    </button>
                  )}
                  {user.id !== staff.me.user.userId && (
                    <button className="text-button" onClick={() => void save(user, { active: !user.active })}>
                      {user.active ? 'Desativar' : 'Reativar'}
                    </button>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <form className="amount-form" onSubmit={create}>
        <h2>Novo usuário</h2>
        <div className="form-row wrap">
          <label className="field">
            <span>Nome</span>
            <input maxLength={80} value={name} onChange={(event) => setName(event.target.value)} />
          </label>
          <label className="field">
            <span>Usuário</span>
            <input
              autoCapitalize="none"
              autoComplete="off"
              maxLength={30}
              value={login}
              onChange={(event) => setLogin(event.target.value.toLowerCase())}
            />
          </label>
          <label className="field">
            <span>Senha (mínimo 6)</span>
            <input
              type="password"
              autoComplete="new-password"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
            />
          </label>
          <label className="field">
            <span>Perfil</span>
            <select value={role} onChange={(event) => setRole(event.target.value as User['role'])}>
              <option value="staff">Equipe</option>
              <option value="admin">Administrador</option>
            </select>
          </label>
        </div>
        <button
          className="button primary"
          disabled={!name.trim() || !/^[a-z0-9._-]{3,30}$/.test(login) || password.length < 6}
        >
          Criar usuário
        </button>
      </form>
    </main>
  );
}
