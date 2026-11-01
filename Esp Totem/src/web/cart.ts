import type { Catalog, Product } from '../server/catalog.ts';
import type { CartItem } from '../server/pricing.ts';

export type MenuProduct = Product;

export type Menu = Omit<Catalog, 'categories'> & {
  categories: (Catalog['categories'][number] & { openNow: boolean })[];
};

export interface CartLine {
  id: string;
  payload: CartItem;
  name: string;
  details: string[];
  notes: string;
  unitPriceCents: number;
}

export const startingPrice = (product: MenuProduct) =>
  product.priceCents ?? Math.min(...Object.values(product.pricesBySize));

export const cartTotal = (cart: CartLine[]) =>
  cart.reduce((sum, line) => sum + line.unitPriceCents * line.payload.quantity, 0);
