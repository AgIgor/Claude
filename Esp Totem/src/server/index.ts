import { buildApp } from './app.ts';
import { openDb } from './db.ts';
import { DEMO_SLUG, seedDemoStore } from './seed.ts';

const production = process.env.NODE_ENV === 'production';

function config(name: string, devDefault: string): string {
  const value = process.env[name];
  if (value) return value;
  if (production) throw new Error(`Defina ${name} em produção.`);
  return devDefault;
}

const port = Number(process.env.PORT ?? 3000);
const host = process.env.HOST ?? '0.0.0.0';
const secret = config('APP_SECRET', 'segredo-somente-para-desenvolvimento');
const seedPassword = config('SEED_PASSWORD', 'demo1234');

const db = await openDb(process.env.DATA_DIR ?? '.data/pg');
await seedDemoStore(db, seedPassword);

const app = await buildApp({ db, secret, staticDir: 'dist' });

let stopping = false;
async function shutdown() {
  if (stopping) return;
  stopping = true;
  await app.close();
  await db.close();
  process.exit(0);
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);

await app.listen({ port, host });
console.log(`API em http://localhost:${port} | painel: /painel/${DEMO_SLUG}`);
