import { rows, type Queryable } from './db.ts';

export interface Option {
  id: string;
  name: string;
  priceCents: number;
  available: boolean;
}

export interface OptionGroup {
  id: string;
  name: string;
  minSelect: number;
  maxSelect: number;
  options: Option[];
}

export interface PizzaSize {
  id: string;
  name: string;
  slices: number;
  maxFlavors: number;
}

export interface Product {
  id: string;
  categoryId: string;
  kind: 'simple' | 'pizza';
  name: string;
  description: string;
  priceCents: number | null;
  available: boolean;
  pricesBySize: Record<string, number>;
  optionGroupIds: string[];
}

export interface Category {
  id: string;
  name: string;
  startMinute: number | null;
  endMinute: number | null;
}

export interface Catalog {
  categories: Category[];
  sizes: PizzaSize[];
  products: Product[];
  optionGroups: OptionGroup[];
}

export async function loadCatalog(db: Queryable, storeId: string): Promise<Catalog> {
  const [categories, sizes, products, prices, links, groups, options] = [
    await rows<Category>(
      db,
      `SELECT id, name, start_minute AS "startMinute", end_minute AS "endMinute"
       FROM categories WHERE store_id = $1 ORDER BY sort`,
      [storeId],
    ),
    await rows<PizzaSize>(
      db,
      `SELECT id, name, slices, max_flavors AS "maxFlavors"
       FROM pizza_sizes WHERE store_id = $1 ORDER BY sort`,
      [storeId],
    ),
    await rows<Omit<Product, 'pricesBySize' | 'optionGroupIds'>>(
      db,
      `SELECT id, category_id AS "categoryId", kind, name, description, price_cents AS "priceCents",
              available
       FROM products WHERE store_id = $1 ORDER BY sort`,
      [storeId],
    ),
    await rows<{ productId: string; sizeId: string; priceCents: number }>(
      db,
      `SELECT pp.product_id AS "productId", pp.size_id AS "sizeId", pp.price_cents AS "priceCents"
       FROM pizza_prices pp JOIN products p ON p.id = pp.product_id WHERE p.store_id = $1`,
      [storeId],
    ),
    await rows<{ productId: string; groupId: string }>(
      db,
      `SELECT pog.product_id AS "productId", pog.group_id AS "groupId"
       FROM product_option_groups pog JOIN products p ON p.id = pog.product_id WHERE p.store_id = $1`,
      [storeId],
    ),
    await rows<Omit<OptionGroup, 'options'>>(
      db,
      `SELECT id, name, min_select AS "minSelect", max_select AS "maxSelect"
       FROM option_groups WHERE store_id = $1 ORDER BY sort`,
      [storeId],
    ),
    await rows<Option & { groupId: string }>(
      db,
      `SELECT o.id, o.group_id AS "groupId", o.name, o.price_cents AS "priceCents",
              o.available
       FROM options o JOIN option_groups g ON g.id = o.group_id
       WHERE g.store_id = $1 ORDER BY o.sort`,
      [storeId],
    ),
  ];

  return {
    categories,
    sizes,
    optionGroups: groups.map((group) => ({
      ...group,
      options: options.filter((option) => option.groupId === group.id).map(({ groupId: _, ...option }) => option),
    })),
    products: products.map((product) => ({
      ...product,
      pricesBySize: Object.fromEntries(
        prices.filter((price) => price.productId === product.id).map((price) => [price.sizeId, price.priceCents]),
      ),
      optionGroupIds: links.filter((link) => link.productId === product.id).map((link) => link.groupId),
    })),
  };
}

export function minuteOfDay(now: Date, timezone: string): number {
  const parts = new Intl.DateTimeFormat('en-GB', {
    timeZone: timezone,
    hour: '2-digit',
    minute: '2-digit',
    hourCycle: 'h23',
  }).formatToParts(now);
  const value = (type: string) => Number(parts.find((part) => part.type === type)?.value);
  return value('hour') * 60 + value('minute');
}

export function isCategoryOpen(category: Category, minute: number): boolean {
  if (category.startMinute === null || category.endMinute === null) return true;
  return minute >= category.startMinute && minute < category.endMinute;
}

/** Cardapio com o que muda ao longo do dia: pode ser pedido agora? */
export function menuAt(catalog: Catalog, minute: number) {
  return {
    ...catalog,
    categories: catalog.categories.map((category) => ({
      ...category,
      openNow: isCategoryOpen(category, minute),
    })),
  };
}
