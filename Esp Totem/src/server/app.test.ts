import assert from 'node:assert/strict';
import { after, before, beforeEach, describe, it } from 'node:test';
import { buildApp } from './app.ts';
import { createToken, hashPassword } from './auth.ts';
import { loadCatalog, type Catalog } from './catalog.ts';
import { one, openDb, type Db } from './db.ts';
import { DEMO_SLUG, seedDemoStore } from './seed.ts';
import { computeTotals } from './totals.ts';

const SECRET = 'segredo-de-teste';
const PASSWORD = 'senha-teste';

type App = Awaited<ReturnType<typeof buildApp>>;
type Method = 'GET' | 'POST' | 'PATCH' | 'PUT' | 'DELETE';

let db: Db;
let app: App;
let storeId: string;
let catalog: Catalog;
let admin: string;
let cashier: string;
let keyCounter = 0;

const newKey = () => `chave-${String(++keyCounter).padStart(4, '0')}`;

async function call(token: string, method: Method, url: string, payload?: object) {
  return app.inject({ method, url, headers: { authorization: `Bearer ${token}` }, payload });
}

async function login(loginName: string) {
  const response = await app.inject({
    method: 'POST',
    url: '/api/staff/login',
    payload: { slug: DEMO_SLUG, login: loginName, password: PASSWORD },
  });
  return response.json().token as string;
}

const tablesList = async () => (await call(cashier, 'GET', '/api/staff/tables')).json();

async function openTable(number: number) {
  const table = (await tablesList()).find((candidate: { number: number }) => candidate.number === number);
  await call(cashier, 'POST', `/api/staff/tables/${table.id}/open`);
  const refreshed = (await tablesList()).find((candidate: { number: number }) => candidate.number === number);
  const join = await app.inject({
    method: 'POST',
    url: `/api/public/tables/${table.qrToken}/join`,
    payload: { pin: refreshed.pin },
  });
  return { table: refreshed, token: join.json().token as string, billId: refreshed.billId as string };
}

function product(name: string) {
  const found = catalog.products.find((candidate) => candidate.name === name);
  assert.ok(found, name);
  return found;
}

const sizeId = (name: string) => catalog.sizes.find((size) => size.name === name)!.id;
const water = () => ({ type: 'simple', productId: product('Água Mineral').id, quantity: 1 });
const WATER_CENTS = 590;

const placeOrder = (token: string, items: object[] = [water()], idempotencyKey = newKey()) =>
  call(token, 'POST', '/api/public/orders', { items, idempotencyKey });

const staffOrder = (target: object, items: object[] = [water()], token = cashier) =>
  call(token, 'POST', '/api/staff/orders', { ...target, items, idempotencyKey: newKey() });

const finishOrders = (billId: string) => db.query(`UPDATE orders SET status = 'delivered' WHERE bill_id = $1`, [billId]);

const pay = (billId: string, body: object, token = cashier) =>
  call(token, 'POST', `/api/staff/bills/${billId}/payments`, body);

const cash = async () => (await call(cashier, 'GET', '/api/staff/cash')).json().current;

before(async () => {
  db = await openDb();
  storeId = await seedDemoStore(db, PASSWORD);
  app = await buildApp({ db, secret: SECRET, pinAttemptsPerMinute: 1000 });
  catalog = await loadCatalog(db, storeId);
  admin = await login('admin');
  cashier = await login('caixa');
});

after(async () => {
  await app.close();
  await db.close();
});

beforeEach(async () => {
  await db.query(`UPDATE bills SET status = 'closed' WHERE status = 'open'`);
  await db.query(`UPDATE cash_sessions SET status = 'closed', closed_at = now() - interval '1 day' WHERE status = 'open'`);
  await db.query(`UPDATE products SET available = true`);
  await db.query('UPDATE stores SET service_percent = 10');
  catalog = await loadCatalog(db, storeId);
});

describe('totais da conta', () => {
  const base = { subtotalCents: 10000, servicePercent: 10, serviceApplied: true, discountCents: 0, paidCents: 0 };

  it('soma serviço, abate desconto e calcula o saldo', () => {
    const totals = computeTotals({ ...base, discountCents: 500, paidCents: 2000 });
    assert.deepEqual(
      [totals.serviceCents, totals.totalCents, totals.remainingCents],
      [1000, 10500, 8500],
    );
  });

  it('sem serviço aplicado não cobra a taxa', () => {
    assert.equal(computeTotals({ ...base, serviceApplied: false }).totalCents, 10000);
  });

  it('arredonda a taxa ao centavo', () => {
    assert.equal(computeTotals({ ...base, subtotalCents: 8895 }).serviceCents, 890);
  });
});

describe('mesa e cliente', () => {
  it('não entra com mesa fechada nem com PIN errado', async () => {
    const [{ qrToken, id }] = await tablesList();
    const join = (pin: string) =>
      app.inject({ method: 'POST', url: `/api/public/tables/${qrToken}/join`, payload: { pin } });

    assert.equal((await join('0000')).statusCode, 409);
    await call(cashier, 'POST', `/api/staff/tables/${id}/open`);
    const { pin } = (await tablesList())[0];
    assert.equal((await join(pin === '0000' ? '0001' : '0000')).statusCode, 403);
    assert.equal((await join(pin)).statusCode, 200);
  });

  it('calcula o preço no servidor e ignora o valor enviado', async () => {
    const { token } = await openTable(1);
    const response = await placeOrder(token, [
      {
        type: 'pizza',
        sizeId: sizeId('Grande'),
        flavorIds: [product('Calabresa').id, product('Portuguesa').id],
        quantity: 1,
        priceCents: 1,
      },
    ]);
    assert.equal(response.statusCode, 201);
    assert.equal(response.json().totalCents, 7990);
    assert.equal(response.json().label, 'Mesa 1');
  });

  it('não duplica ao repetir a mesma chave', async () => {
    const { token } = await openTable(1);
    const key = newKey();
    const first = await placeOrder(token, [water()], key);
    const second = await placeOrder(token, [water()], key);
    assert.equal(first.json().id, second.json().id);
  });

  it('a conta do cliente inclui serviço e exclui cancelados', async () => {
    const { token } = await openTable(1);
    await placeOrder(token);
    const cancelled = (await placeOrder(token)).json();
    await call(cashier, 'POST', `/api/staff/orders/${cancelled.id}/status`, { status: 'cancelled' });
    const bill = (await call(token, 'GET', '/api/public/session/orders')).json();
    assert.equal(bill.subtotalCents, WATER_CENTS);
    assert.equal(bill.totalCents, WATER_CENTS + 59);
  });

  it('o token do cliente deixa de valer quando a conta fecha', async () => {
    await call(cashier, 'POST', '/api/staff/cash/open', { openingCents: 0 });
    const { token, billId } = await openTable(1);
    assert.equal((await call(cashier, 'POST', `/api/staff/bills/${billId}/close`)).statusCode, 200);
    assert.equal((await placeOrder(token)).statusCode, 401);
  });

  it('o limite de PIN é por mesa, para não bloquear o mesmo Wi-Fi', async () => {
    const limited = await buildApp({ db, secret: SECRET, pinAttemptsPerMinute: 2 });
    const [first, second] = await tablesList();
    const join = (qrToken: string) =>
      limited.inject({ method: 'POST', url: `/api/public/tables/${qrToken}/join`, payload: { pin: '0000' } });

    await join(first.qrToken);
    await join(first.qrToken);
    assert.equal((await join(first.qrToken)).statusCode, 429);
    assert.notEqual((await join(second.qrToken)).statusCode, 429);
    await limited.close();
  });
});

describe('pedidos da equipe', () => {
  it('lançado na mesa abre a conta e já nasce aceito', async () => {
    const [table] = await tablesList();
    const order = (await staffOrder({ tableId: table.id })).json();
    assert.equal(order.status, 'accepted');
    assert.equal(order.channel, 'staff');
    assert.equal((await tablesList())[0].billId, order.billId);
  });

  it('no balcão cria uma conta com o nome do cliente', async () => {
    const order = (await staffOrder({ label: 'João' })).json();
    assert.equal(order.label, 'João');
    const bill = (await call(cashier, 'GET', `/api/staff/bills/${order.billId}`)).json();
    assert.equal(bill.kind, 'counter');
  });

  it('pedido recusado não deixa conta vazia no balcão', async () => {
    await call(cashier, 'PATCH', `/api/staff/products/${product('Água Mineral').id}/availability`, { available: false });
    assert.equal((await staffOrder({ label: 'Maria' })).statusCode, 409);
    assert.equal((await call(cashier, 'GET', '/api/staff/bills')).json().length, 0);
  });

  it('exige exatamente um destino', async () => {
    const [table] = await tablesList();
    assert.equal((await staffOrder({})).statusCode, 422);
    assert.equal((await staffOrder({ tableId: table.id, label: 'Ana' })).statusCode, 422);
  });

  it('segue a sequência de status e recusa saltos', async () => {
    const order = (await staffOrder({ label: 'Ana' })).json();
    const move = (status: string) => call(cashier, 'POST', `/api/staff/orders/${order.id}/status`, { status });
    assert.equal((await move('ready')).statusCode, 409);
    for (const status of ['preparing', 'ready', 'delivered']) {
      assert.equal((await move(status)).json().status, status);
    }
    assert.equal((await move('cancelled')).statusCode, 409);
  });
});

describe('caixa', () => {
  const openCash = (openingCents = 10000) => call(cashier, 'POST', '/api/staff/cash/open', { openingCents });

  async function billWithWater() {
    const order = (await staffOrder({ label: 'Cliente' })).json();
    return order.billId as string;
  }

  it('não recebe pagamento com o caixa fechado', async () => {
    assert.equal((await pay(await billWithWater(), { method: 'pix', amountCents: 100 })).statusCode, 409);
  });

  it('não abre dois caixas', async () => {
    await openCash();
    assert.equal((await openCash()).statusCode, 409);
  });

  it('calcula o troco e soma só o valor da conta no caixa', async () => {
    await openCash();
    const billId = await billWithWater();
    const bill = (await pay(billId, { method: 'cash', amountCents: WATER_CENTS, receivedCents: 1000 })).json();
    assert.equal(bill.payments[0].changeCents, 410);
    assert.equal(bill.remainingCents, 0);
    assert.equal((await cash()).expectedCashCents, 10000 + WATER_CENTS);
  });

  it('recusa dinheiro recebido a menos e pagamento acima do saldo', async () => {
    await openCash();
    const billId = await billWithWater();
    assert.equal((await pay(billId, { method: 'cash', amountCents: 500, receivedCents: 400 })).statusCode, 422);
    assert.equal((await pay(billId, { method: 'pix', amountCents: WATER_CENTS + 1 })).statusCode, 422);
  });

  it('divide o pagamento e só fecha com saldo zerado', async () => {
    await openCash();
    const billId = await billWithWater();
    await pay(billId, { method: 'pix', amountCents: 300 });
    assert.equal((await call(cashier, 'POST', `/api/staff/bills/${billId}/close`)).statusCode, 409);
    await pay(billId, { method: 'debit', amountCents: WATER_CENTS - 300 });
    assert.equal((await call(cashier, 'POST', `/api/staff/bills/${billId}/close`)).statusCode, 200);
    assert.equal((await call(cashier, 'GET', '/api/staff/bills')).json().length, 0);

    const { byMethod, expectedCashCents } = await cash();
    assert.deepEqual([byMethod.pix, byMethod.debit], [300, WATER_CENTS - 300]);
    assert.equal(expectedCashCents, 10000, 'Pix e cartão não entram na gaveta');
  });

  it('mesa só fecha com pedidos concluídos; balcão pode pagar antes', async () => {
    await openCash();
    const { billId, token } = await openTable(1);
    await placeOrder(token);
    await db.query(`UPDATE bills SET service_applied = false WHERE id = $1`, [billId]);
    await pay(billId, { method: 'pix', amountCents: WATER_CENTS });
    assert.equal((await call(cashier, 'POST', `/api/staff/bills/${billId}/close`)).statusCode, 409);
    await finishOrders(billId);
    assert.equal((await call(cashier, 'POST', `/api/staff/bills/${billId}/close`)).statusCode, 200);

    const counter = await billWithWater();
    await pay(counter, { method: 'pix', amountCents: WATER_CENTS });
    assert.equal((await call(cashier, 'POST', `/api/staff/bills/${counter}/close`)).statusCode, 200);
  });

  it('aplica desconto e tira o serviço, sem deixar o total abaixo do pago', async () => {
    await openCash();
    const { billId, token } = await openTable(1);
    await placeOrder(token);
    const adjust = (body: object) => call(cashier, 'POST', `/api/staff/bills/${billId}/adjust`, body);

    const adjusted = (await adjust({ serviceApplied: false, discountCents: 90 })).json();
    assert.equal(adjusted.totalCents, 500);
    assert.equal((await adjust({ serviceApplied: false, discountCents: 99999 })).statusCode, 422);

    await pay(billId, { method: 'pix', amountCents: 500 });
    assert.equal((await adjust({ serviceApplied: false, discountCents: 200 })).statusCode, 409);
  });

  it('não cancela pedido já pago sem estornar antes', async () => {
    await openCash();
    const order = (await staffOrder({ label: 'Pago' })).json();
    const paid = (await pay(order.billId, { method: 'pix', amountCents: WATER_CENTS })).json();
    const cancel = () => call(cashier, 'POST', `/api/staff/orders/${order.id}/status`, { status: 'cancelled' });

    assert.equal((await cancel()).statusCode, 409);
    assert.equal((await staffOrderStatus(order.id)), 'accepted', 'a recusa desfaz o cancelamento');

    const voidUrl = `/api/staff/payments/${paid.payments[0].id}/void`;
    assert.equal((await call(cashier, 'POST', voidUrl)).statusCode, 403);
    const voided = (await call(admin, 'POST', voidUrl)).json();
    assert.equal(voided.remainingCents, WATER_CENTS);
    assert.equal((await call(admin, 'POST', voidUrl)).statusCode, 409);
    assert.equal((await cancel()).statusCode, 200);
  });

  async function staffOrderStatus(orderId: string) {
    return (await one<{ status: string }>(db, 'SELECT status FROM orders WHERE id = $1', [orderId]))!.status;
  }

  it('sangria e suprimento alteram o esperado, e o fechamento mostra a diferença', async () => {
    await openCash(10000);
    const move = (body: object) => call(cashier, 'POST', '/api/staff/cash/movements', body);
    await move({ type: 'deposit', amountCents: 2000, reason: 'Troco extra' });
    await move({ type: 'withdrawal', amountCents: 5000, reason: 'Depósito no banco' });
    assert.equal((await move({ type: 'withdrawal', amountCents: 999999, reason: 'Erro' })).statusCode, 422);
    assert.equal((await move({ type: 'withdrawal', amountCents: 100, reason: '   ' })).statusCode, 400);

    const closed = (await call(cashier, 'POST', '/api/staff/cash/close', { countedCents: 6900 })).json();
    assert.equal(closed.expectedCashCents, 7000);
    assert.equal(closed.differenceCents, -100);
    assert.equal(await cash(), null);

    const [history] = (await call(admin, 'GET', '/api/admin/cash/history')).json();
    assert.deepEqual([history.expectedCents, history.countedCents], [7000, 6900]);
  });

  it('o estorno não vale depois que o caixa fechou', async () => {
    await openCash();
    const billId = await billWithWater();
    const paid = (await pay(billId, { method: 'pix', amountCents: 100 })).json();
    await call(cashier, 'POST', '/api/staff/cash/close', { countedCents: 10000 });
    assert.equal((await call(admin, 'POST', `/api/staff/payments/${paid.payments[0].id}/void`)).statusCode, 409);
  });

  it('o relatório soma pagamentos válidos e os itens vendidos', async () => {
    const today = new Date().toLocaleDateString('en-CA', { timeZone: 'America/Sao_Paulo' });
    const url = `/api/admin/reports/sales?from=${today}&to=${today}`;
    const before = (await call(admin, 'GET', url)).json();

    await openCash();
    const billId = await billWithWater();
    await pay(billId, { method: 'cash', amountCents: WATER_CENTS, receivedCents: 1000 });
    await call(cashier, 'POST', `/api/staff/bills/${billId}/close`);

    const report = (await call(admin, 'GET', url)).json();
    assert.equal(report.totalCents, before.totalCents + WATER_CENTS, 'não conta o troco');
    assert.equal(report.billCount, before.billCount + 1);
    assert.ok(report.topItems.some((item: { name: string }) => item.name === 'Água Mineral'));
    assert.equal((await call(cashier, 'GET', url)).statusCode, 403);
  });
});

describe('administração do cardápio', () => {
  const pizzaCategory = () => catalog.categories[0].id;
  const refresh = async () => {
    catalog = await loadCatalog(db, storeId);
  };

  it('a equipe comum não altera o cardápio', async () => {
    const response = await call(cashier, 'POST', '/api/admin/categories', { name: 'Proibida' });
    assert.equal(response.statusCode, 403);
  });

  it('cria um produto que o cliente já consegue pedir', async () => {
    await call(admin, 'POST', '/api/admin/categories', { name: 'Porções' });
    await refresh();
    const category = catalog.categories.find((item) => item.name === 'Porções')!;
    const created = await call(admin, 'POST', '/api/admin/products', {
      categoryId: category.id,
      kind: 'simple',
      name: 'Polenta Frita',
      description: 'Porção média.',
      priceCents: 2490,
    });
    assert.equal(created.statusCode, 200);
    await refresh();

    const order = await staffOrder({ label: 'Teste' }, [
      { type: 'simple', productId: product('Polenta Frita').id, quantity: 2 },
    ]);
    assert.equal(order.json().totalCents, 4980);
  });

  it('edita preços por tamanho e os pedidos seguintes usam o novo valor', async () => {
    const flavor = product('Muçarela');
    const prices = { ...flavor.pricesBySize, [sizeId('Pequena')]: 4500 };
    const saved = await call(admin, 'PUT', `/api/admin/products/${flavor.id}`, {
      categoryId: flavor.categoryId,
      kind: 'pizza',
      name: flavor.name,
      pricesBySize: prices,
      optionGroupIds: flavor.optionGroupIds,
    });
    assert.equal(saved.statusCode, 200);
    const order = await staffOrder({ label: 'Preço' }, [
      { type: 'pizza', sizeId: sizeId('Pequena'), flavorIds: [flavor.id], quantity: 1 },
    ]);
    assert.equal(order.json().totalCents, 4500);
  });

  it('valida o cadastro', async () => {
    const save = (body: object) => call(admin, 'POST', '/api/admin/products', body);
    const base = { categoryId: pizzaCategory(), name: 'Inválido' };
    assert.equal((await save({ ...base, kind: 'simple' })).statusCode, 422);
    assert.equal((await save({ ...base, kind: 'pizza', pricesBySize: {} })).statusCode, 422);
    const windowed = await call(admin, 'POST', '/api/admin/categories', { name: 'X', startMinute: 600, endMinute: 500 });
    assert.equal(windowed.statusCode, 422);
    const group = await call(admin, 'POST', '/api/admin/option-groups', {
      name: 'Y',
      minSelect: 2,
      maxSelect: 1,
      options: [{ name: 'a', priceCents: 0 }],
    });
    assert.equal(group.statusCode, 422);
  });

  it('não exclui categoria com produtos, mas exclui o produto sem afetar o histórico', async () => {
    assert.equal((await call(admin, 'DELETE', `/api/admin/categories/${pizzaCategory()}`)).statusCode, 409);

    const drink = product('Suco de Laranja');
    const order = (await staffOrder({ label: 'Histórico' }, [{ type: 'simple', productId: drink.id, quantity: 1 }])).json();
    assert.equal((await call(admin, 'DELETE', `/api/admin/products/${drink.id}`)).statusCode, 200);
    await refresh();

    const bill = (await call(cashier, 'GET', `/api/staff/bills/${order.billId}`)).json();
    assert.equal(bill.orders[0].items[0].name, 'Suco de Laranja');
  });

  it('edita um grupo mantendo a disponibilidade das opções', async () => {
    const group = catalog.optionGroups.find((item) => item.name === 'Borda recheada')!;
    const [first] = group.options;
    await call(cashier, 'PATCH', `/api/staff/options/${first.id}/availability`, { available: false });
    const saved = await call(admin, 'PUT', `/api/admin/option-groups/${group.id}`, {
      name: group.name,
      minSelect: 0,
      maxSelect: 1,
      options: [
        { id: first.id, name: first.name, priceCents: 1000 },
        { name: 'Catupiry Vegano Demo', priceCents: 1200 },
      ],
    });
    assert.equal(saved.statusCode, 200);
    await refresh();
    const updated = catalog.optionGroups.find((item) => item.id === group.id)!;
    assert.deepEqual(updated.options.map((option) => [option.priceCents, option.available]), [
      [1000, false],
      [1200, true],
    ]);
  });

  it('reordena categorias', async () => {
    const ids = catalog.categories.map((category) => category.id).reverse();
    await call(admin, 'POST', '/api/admin/reorder', { type: 'categories', ids });
    await refresh();
    assert.equal(catalog.categories[0].id, ids[0]);
    await call(admin, 'POST', '/api/admin/reorder', { type: 'categories', ids: [...ids].reverse() });
    await refresh();
  });

  it('adiciona mesa e não exclui mesa com histórico', async () => {
    const count = (await tablesList()).length;
    await call(admin, 'POST', '/api/admin/tables');
    const tables = await tablesList();
    assert.equal(tables.length, count + 1);
    assert.equal(tables.at(-1).number, count + 1);
    assert.equal((await call(admin, 'DELETE', `/api/admin/tables/${tables.at(-1).id}`)).statusCode, 200);

    await openTable(1);
    assert.equal((await call(admin, 'DELETE', `/api/admin/tables/${tables[0].id}`)).statusCode, 409);
  });
});

describe('usuários', () => {
  it('admin cria usuário, e desativar corta o acesso na hora', async () => {
    const login_ = `novo${++keyCounter}`;
    const body = { name: 'Novo', login: login_, password: PASSWORD, role: 'staff' };
    assert.equal((await call(admin, 'POST', '/api/admin/users', body)).statusCode, 200);
    assert.equal((await call(admin, 'POST', '/api/admin/users', body)).statusCode, 409);

    const token = await login(login_);
    assert.equal((await call(token, 'GET', '/api/staff/orders')).statusCode, 200);

    const users = (await call(admin, 'GET', '/api/admin/users')).json();
    assert.equal(JSON.stringify(users).includes('hash'), false);
    const user = users.find((candidate: { login: string }) => candidate.login === login_);
    await call(admin, 'PATCH', `/api/admin/users/${user.id}`, { name: 'Novo', role: 'staff', active: false });
    assert.equal((await call(token, 'GET', '/api/staff/orders')).statusCode, 401);
    assert.equal(await login(login_), undefined);
  });

  it('admin não pode se desativar nem se rebaixar', async () => {
    const { user } = (await call(admin, 'GET', '/api/staff/me')).json();
    const update = (body: object) => call(admin, 'PATCH', `/api/admin/users/${user.userId}`, body);
    assert.equal((await update({ name: 'A', role: 'admin', active: false })).statusCode, 409);
    assert.equal((await update({ name: 'A', role: 'staff', active: true })).statusCode, 409);
  });

  it('recusa senha errada', async () => {
    assert.equal(
      (
        await app.inject({
          method: 'POST',
          url: '/api/staff/login',
          payload: { slug: DEMO_SLUG, login: 'admin', password: 'errada' },
        })
      ).statusCode,
      401,
    );
  });
});

describe('isolamento entre lojas', () => {
  let otherAdmin: string;

  before(async () => {
    const other = await one<{ id: string }>(
      db,
      `INSERT INTO stores (slug, name) VALUES ('outra-demo', 'Outra Loja Demo') RETURNING id`,
    );
    const user = await one<{ id: string }>(
      db,
      `INSERT INTO users (store_id, name, login, password_hash, role)
       VALUES ($1, 'Outro', 'outro', $2, 'admin') RETURNING id`,
      [other!.id, hashPassword(PASSWORD)],
    );
    otherAdmin = createToken(
      { kind: 'staff', storeId: other!.id, userId: user!.id, exp: Date.now() + 60_000 },
      SECRET,
    );
  });

  it('não vê nem altera dados de outra loja', async () => {
    await call(cashier, 'POST', '/api/staff/cash/open', { openingCents: 0 });
    await call(otherAdmin, 'POST', '/api/staff/cash/open', { openingCents: 0 });
    const order = (await staffOrder({ label: 'Isolado' })).json();
    const [table] = await tablesList();
    const paid = (await pay(order.billId, { method: 'pix', amountCents: 100 })).json();
    const flavor = product('Calabresa');

    const attempts: [Method, string, object?][] = [
      ['POST', `/api/staff/orders/${order.id}/status`, { status: 'preparing' }],
      ['GET', `/api/staff/bills/${order.billId}`],
      ['POST', `/api/staff/bills/${order.billId}/payments`, { method: 'pix', amountCents: 100 }],
      ['POST', `/api/staff/bills/${order.billId}/close`],
      ['POST', `/api/staff/payments/${paid.payments[0].id}/void`],
      ['POST', `/api/staff/tables/${table.id}/open`],
      ['PATCH', `/api/staff/products/${flavor.id}/availability`, { available: false }],
      ['DELETE', `/api/admin/products/${flavor.id}`],
      ['DELETE', `/api/admin/categories/${catalog.categories[4].id}`],
    ];
    for (const [method, url, body] of attempts) {
      const response = await call(otherAdmin, method, url, body);
      assert.ok([404, 409].includes(response.statusCode), `${method} ${url}: ${response.statusCode}`);
    }
    assert.equal((await staffOrder({ billId: order.billId }, [water()], otherAdmin)).statusCode, 404);
    assert.equal((await call(otherAdmin, 'GET', '/api/staff/orders')).json().length, 0);

    const moved = await one<{ status: string }>(db, 'SELECT status FROM orders WHERE id = $1', [order.id]);
    assert.equal(moved?.status, 'accepted');
  });

  it('não referencia itens de outra loja ao salvar produto', async () => {
    await call(otherAdmin, 'POST', '/api/admin/categories', { name: 'Minha' });
    const { categories } = (await call(otherAdmin, 'GET', '/api/staff/menu')).json();
    const saved = await call(otherAdmin, 'POST', '/api/admin/products', {
      categoryId: categories[0].id,
      kind: 'pizza',
      name: 'Invasora',
      pricesBySize: { [catalog.sizes[0].id]: 100 },
    });
    assert.equal(saved.statusCode, 422);
  });
});
