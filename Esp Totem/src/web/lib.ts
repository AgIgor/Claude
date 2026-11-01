import { useEffect, useRef } from 'react';

export class ApiError extends Error {
  status: number;

  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

export async function api<T>(
  path: string,
  { method = 'GET', body, token }: { method?: string; body?: unknown; token?: string | null } = {},
): Promise<T> {
  const headers: Record<string, string> = {};
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  if (token) headers.Authorization = `Bearer ${token}`;

  let response: Response;
  try {
    response = await fetch(path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  } catch {
    throw new ApiError('Sem conexão. Verifique a internet e tente de novo.', 0);
  }
  const data = await response.json().catch(() => null);
  if (!response.ok) throw new ApiError(data?.error ?? 'Não foi possível concluir.', response.status);
  return data as T;
}

const money = new Intl.NumberFormat('pt-BR', { style: 'currency', currency: 'BRL' });
export const formatMoney = (cents: number) => money.format(cents / 100);

export const formatTime = (value: string | Date) =>
  new Date(value).toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' });

export const formatMinute = (minute: number) => `${Math.floor(minute / 60)}h${minute % 60 ? minute % 60 : ''}`;

/** Converte "39,90", "39.9" ou "1.234,50" em centavos. Devolve null se for invalido. */
export function parseMoney(text: string): number | null {
  const clean = text.trim().replace(/^R\$\s*/, '');
  if (!/^\d{1,3}(\.\d{3})*(,\d{1,2})?$|^\d+([.,]\d{1,2})?$/.test(clean)) return null;
  const normalized = clean.includes(',') ? clean.replace(/\./g, '').replace(',', '.') : clean;
  return Math.round(Number(normalized) * 100);
}

export const moneyInput = (cents: number) => (cents / 100).toFixed(2).replace('.', ',');

export const PAYMENT_LABEL = { cash: 'Dinheiro', pix: 'Pix', debit: 'Débito', credit: 'Crédito' } as const;

export const orderNumber = (number: number) => `#${String(number).padStart(3, '0')}`;

// crypto.randomUUID so existe em HTTPS; getRandomValues tambem funciona na rede local.
export function randomKey(): string {
  return Array.from(crypto.getRandomValues(new Uint8Array(16)), (byte) => byte.toString(16).padStart(2, '0')).join('');
}

// O armazenamento pode estar bloqueado (aba anonima, por exemplo): a sessao segue em memoria.
export const storage = {
  get(key: string): string | null {
    try {
      return localStorage.getItem(key);
    } catch {
      return null;
    }
  },
  set(key: string, value: string) {
    try {
      localStorage.setItem(key, value);
    } catch {
      /* segue sem persistir */
    }
  },
  remove(key: string) {
    try {
      localStorage.removeItem(key);
    } catch {
      /* segue sem persistir */
    }
  },
};

/** Recarrega os dados a cada aviso do servidor e sempre que a conexao volta. */
export function useLiveRefresh(url: string | null, refresh: () => void) {
  const latest = useRef(refresh);
  latest.current = refresh;

  useEffect(() => {
    if (!url) return;
    const source = new EventSource(url);
    source.onopen = () => latest.current();
    source.onmessage = () => latest.current();
    return () => source.close();
  }, [url]);
}
