import { mkdirSync, readFileSync } from 'node:fs';
import { dirname } from 'node:path';
import { PGlite, type Transaction } from '@electric-sql/pglite';

export type Db = PGlite;
export type Queryable = Pick<Db | Transaction, 'query'>;

const schema = readFileSync(new URL('./schema.sql', import.meta.url), 'utf8');

/** Sem `dataDir` o banco fica em memoria (usado nos testes). */
export async function openDb(dataDir?: string): Promise<Db> {
  if (dataDir) mkdirSync(dirname(dataDir), { recursive: true });
  const db = new PGlite(dataDir);
  await db.exec(schema);
  return db;
}

export async function rows<T>(db: Queryable, sql: string, params: unknown[] = []): Promise<T[]> {
  return (await db.query<T>(sql, params)).rows;
}

export async function one<T>(db: Queryable, sql: string, params: unknown[] = []): Promise<T | undefined> {
  return (await db.query<T>(sql, params)).rows[0];
}
