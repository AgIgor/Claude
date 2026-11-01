CREATE TABLE IF NOT EXISTS stores (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  slug text NOT NULL UNIQUE,
  name text NOT NULL,
  timezone text NOT NULL DEFAULT 'America/Sao_Paulo',
  half_pizza_rule text NOT NULL DEFAULT 'highest' CHECK (half_pizza_rule IN ('highest', 'average')),
  service_percent integer NOT NULL DEFAULT 0 CHECK (service_percent BETWEEN 0 AND 30)
);

CREATE TABLE IF NOT EXISTS users (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  name text NOT NULL,
  login text NOT NULL,
  password_hash text NOT NULL,
  role text NOT NULL CHECK (role IN ('admin', 'staff')),
  active boolean NOT NULL DEFAULT true,
  UNIQUE (store_id, login)
);

CREATE TABLE IF NOT EXISTS categories (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  name text NOT NULL,
  sort integer NOT NULL DEFAULT 0,
  -- Janela de venda em minutos do dia, no fuso da loja. NULL = o dia todo.
  start_minute integer,
  end_minute integer
);

CREATE TABLE IF NOT EXISTS pizza_sizes (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  name text NOT NULL,
  slices integer NOT NULL,
  max_flavors integer NOT NULL CHECK (max_flavors >= 1),
  sort integer NOT NULL DEFAULT 0
);

-- kind = 'pizza' representa um sabor: o preco vem de pizza_prices, por tamanho.
CREATE TABLE IF NOT EXISTS products (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  category_id uuid NOT NULL REFERENCES categories(id) ON DELETE CASCADE,
  kind text NOT NULL CHECK (kind IN ('simple', 'pizza')),
  name text NOT NULL,
  description text NOT NULL DEFAULT '',
  price_cents integer CHECK (price_cents >= 0),
  available boolean NOT NULL DEFAULT true,
  sort integer NOT NULL DEFAULT 0,
  CHECK ((kind = 'simple') = (price_cents IS NOT NULL))
);

CREATE TABLE IF NOT EXISTS pizza_prices (
  product_id uuid NOT NULL REFERENCES products(id) ON DELETE CASCADE,
  size_id uuid NOT NULL REFERENCES pizza_sizes(id) ON DELETE CASCADE,
  price_cents integer NOT NULL CHECK (price_cents >= 0),
  PRIMARY KEY (product_id, size_id)
);

CREATE TABLE IF NOT EXISTS option_groups (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  name text NOT NULL,
  min_select integer NOT NULL DEFAULT 0 CHECK (min_select >= 0),
  max_select integer NOT NULL DEFAULT 1 CHECK (max_select >= 1),
  sort integer NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS options (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  group_id uuid NOT NULL REFERENCES option_groups(id) ON DELETE CASCADE,
  name text NOT NULL,
  price_cents integer NOT NULL DEFAULT 0 CHECK (price_cents >= 0),
  available boolean NOT NULL DEFAULT true,
  sort integer NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS product_option_groups (
  product_id uuid NOT NULL REFERENCES products(id) ON DELETE CASCADE,
  group_id uuid NOT NULL REFERENCES option_groups(id) ON DELETE CASCADE,
  PRIMARY KEY (product_id, group_id)
);

CREATE TABLE IF NOT EXISTS dining_tables (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  number integer NOT NULL,
  qr_token text NOT NULL UNIQUE,
  UNIQUE (store_id, number)
);

-- Conta de uma mesa ou do balcao: agrupa pedidos e pagamentos.
CREATE TABLE IF NOT EXISTS bills (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  kind text NOT NULL CHECK (kind IN ('table', 'counter')),
  table_id uuid REFERENCES dining_tables(id),
  label text NOT NULL,
  pin text,
  status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'closed')),
  service_applied boolean NOT NULL DEFAULT false,
  discount_cents integer NOT NULL DEFAULT 0 CHECK (discount_cents >= 0),
  opened_at timestamptz NOT NULL DEFAULT now(),
  closed_at timestamptz,
  closed_by text,
  CHECK ((kind = 'table') = (table_id IS NOT NULL))
);

CREATE UNIQUE INDEX IF NOT EXISTS one_open_bill_per_table
  ON bills (table_id) WHERE status = 'open';

CREATE TABLE IF NOT EXISTS order_counters (
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  day date NOT NULL,
  last_number integer NOT NULL DEFAULT 0,
  PRIMARY KEY (store_id, day)
);

CREATE TABLE IF NOT EXISTS orders (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  number integer NOT NULL,
  channel text NOT NULL CHECK (channel IN ('qr', 'staff')),
  bill_id uuid NOT NULL REFERENCES bills(id),
  label text NOT NULL,
  status text NOT NULL DEFAULT 'new'
    CHECK (status IN ('new', 'accepted', 'preparing', 'ready', 'delivered', 'cancelled')),
  total_cents integer NOT NULL CHECK (total_cents >= 0),
  idempotency_key text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE (store_id, idempotency_key)
);

CREATE INDEX IF NOT EXISTS orders_by_store_recent ON orders (store_id, created_at DESC);
CREATE INDEX IF NOT EXISTS orders_by_bill ON orders (bill_id);

-- Nomes e precos sao copiados para o pedido: mudar o cardapio nao altera o historico.
CREATE TABLE IF NOT EXISTS order_items (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  order_id uuid NOT NULL REFERENCES orders(id) ON DELETE CASCADE,
  position integer NOT NULL,
  name text NOT NULL,
  quantity integer NOT NULL CHECK (quantity > 0),
  unit_price_cents integer NOT NULL CHECK (unit_price_cents >= 0),
  total_cents integer NOT NULL CHECK (total_cents >= 0),
  notes text NOT NULL DEFAULT '',
  details jsonb NOT NULL
);

CREATE INDEX IF NOT EXISTS order_items_by_order ON order_items (order_id, position);

CREATE TABLE IF NOT EXISTS cash_sessions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'closed')),
  opening_cents integer NOT NULL CHECK (opening_cents >= 0),
  opened_by text NOT NULL,
  opened_at timestamptz NOT NULL DEFAULT now(),
  expected_cents integer,
  counted_cents integer,
  closed_by text,
  closed_at timestamptz
);

CREATE UNIQUE INDEX IF NOT EXISTS one_open_cash_per_store
  ON cash_sessions (store_id) WHERE status = 'open';

CREATE TABLE IF NOT EXISTS cash_movements (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  cash_session_id uuid NOT NULL REFERENCES cash_sessions(id) ON DELETE CASCADE,
  type text NOT NULL CHECK (type IN ('withdrawal', 'deposit')),
  amount_cents integer NOT NULL CHECK (amount_cents > 0),
  reason text NOT NULL,
  created_by text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now()
);

-- O pagamento e apenas registrado: nao ha integracao com maquininha ou banco.
CREATE TABLE IF NOT EXISTS payments (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  store_id uuid NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
  bill_id uuid NOT NULL REFERENCES bills(id),
  cash_session_id uuid NOT NULL REFERENCES cash_sessions(id),
  method text NOT NULL CHECK (method IN ('cash', 'pix', 'debit', 'credit')),
  amount_cents integer NOT NULL CHECK (amount_cents > 0),
  received_cents integer,
  change_cents integer NOT NULL DEFAULT 0,
  created_by text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  voided_at timestamptz,
  voided_by text
);

CREATE INDEX IF NOT EXISTS payments_by_bill ON payments (bill_id);
CREATE INDEX IF NOT EXISTS payments_by_store_date ON payments (store_id, created_at);
