import type { Menu } from '../cart.ts';

export interface Me {
  user: { userId: string; name: string; role: 'admin' | 'staff' };
  store: { name: string; halfPizzaRule: 'highest' | 'average'; servicePercent: number };
}

export interface TableView {
  id: string;
  number: number;
  qrToken: string;
  billId: string | null;
  pin: string | null;
}

export interface Staff {
  me: Me;
  menu: Menu;
  tables: TableView[];
  /** Muda a cada atualizacao vinda do servidor. */
  version: number;
  call: <T = unknown>(path: string, method?: string, body?: unknown) => Promise<T>;
  /** Executa, mostra o erro se houver e atualiza a tela. Devolve true se deu certo. */
  run: (action: () => Promise<unknown>) => Promise<boolean>;
}
