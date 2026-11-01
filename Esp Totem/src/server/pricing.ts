import { isCategoryOpen, type Catalog, type Option, type OptionGroup, type Product } from './catalog.ts';
import { AppError } from './errors.ts';

export type HalfPizzaRule = 'highest' | 'average';

export type CartItem =
  | { type: 'simple'; productId: string; quantity: number; optionIds?: string[]; notes?: string }
  | { type: 'pizza'; sizeId: string; flavorIds: string[]; quantity: number; optionIds?: string[]; notes?: string };

export interface PricedItem {
  name: string;
  quantity: number;
  unitPriceCents: number;
  totalCents: number;
  notes: string;
  details: {
    size: string | null;
    flavors: string[];
    options: { name: string; priceCents: number }[];
  };
}

export function pizzaBasePrice(flavorPrices: number[], rule: HalfPizzaRule): number {
  if (rule === 'highest') return Math.max(...flavorPrices);
  return Math.ceil(flavorPrices.reduce((sum, price) => sum + price, 0) / flavorPrices.length);
}

function flavorLabel(names: string[]): string {
  if (names.length === 1) return names[0];
  return names.map((name) => `1/${names.length} ${name}`).join(' + ');
}

function assertOnSale(product: Product, catalog: Catalog, minute: number): void {
  if (!product.available) throw new AppError(`${product.name} está esgotado.`, 409);
  const category = catalog.categories.find((candidate) => candidate.id === product.categoryId);
  if (category && !isCategoryOpen(category, minute)) {
    throw new AppError(`${product.name} não está disponível neste horário.`, 409);
  }
}

function pickOptions(groups: OptionGroup[], optionIds: string[]): Option[] {
  if (new Set(optionIds).size !== optionIds.length) throw new AppError('Adicional repetido no item.');

  const allowed = new Map(groups.flatMap((group) => group.options.map((option) => [option.id, option])));
  const picked = optionIds.map((id) => {
    const option = allowed.get(id);
    if (!option) throw new AppError('Adicional inválido para este item.');
    if (!option.available) throw new AppError(`${option.name} está esgotado.`, 409);
    return option;
  });

  for (const group of groups) {
    const count = group.options.filter((option) => optionIds.includes(option.id)).length;
    if (count < group.minSelect) throw new AppError(`Escolha uma opção em "${group.name}".`);
    if (count > group.maxSelect) throw new AppError(`Máximo de ${group.maxSelect} em "${group.name}".`);
  }
  return picked;
}

function priced(
  item: CartItem,
  name: string,
  baseCents: number,
  options: Option[],
  identity: Pick<PricedItem['details'], 'size' | 'flavors'>,
): PricedItem {
  const unitPriceCents = baseCents + options.reduce((sum, option) => sum + option.priceCents, 0);
  return {
    name,
    quantity: item.quantity,
    unitPriceCents,
    totalCents: unitPriceCents * item.quantity,
    notes: item.notes?.trim() ?? '',
    details: {
      ...identity,
      options: options.map(({ name, priceCents }) => ({ name, priceCents })),
    },
  };
}

function priceSimple(item: Extract<CartItem, { type: 'simple' }>, catalog: Catalog, minute: number): PricedItem {
  const product = catalog.products.find((candidate) => candidate.id === item.productId);
  if (!product || product.kind !== 'simple') throw new AppError('Produto não encontrado.', 404);
  assertOnSale(product, catalog, minute);

  const groups = catalog.optionGroups.filter((group) => product.optionGroupIds.includes(group.id));
  const options = pickOptions(groups, item.optionIds ?? []);
  return priced(item, product.name, product.priceCents ?? 0, options, {
    size: null,
    flavors: [],
  });
}

function pricePizza(
  item: Extract<CartItem, { type: 'pizza' }>,
  catalog: Catalog,
  rule: HalfPizzaRule,
  minute: number,
): PricedItem {
  const size = catalog.sizes.find((candidate) => candidate.id === item.sizeId);
  if (!size) throw new AppError('Tamanho não encontrado.', 404);
  if (!item.flavorIds?.length) throw new AppError('Escolha ao menos um sabor.');
  if (new Set(item.flavorIds).size !== item.flavorIds.length) throw new AppError('Sabor repetido na pizza.');
  if (item.flavorIds.length > size.maxFlavors) {
    throw new AppError(`A pizza ${size.name} aceita até ${size.maxFlavors} sabor(es).`);
  }

  const flavors = item.flavorIds.map((id) => {
    const flavor = catalog.products.find((candidate) => candidate.id === id);
    if (!flavor || flavor.kind !== 'pizza') throw new AppError('Sabor não encontrado.', 404);
    assertOnSale(flavor, catalog, minute);
    if (flavor.pricesBySize[size.id] === undefined) {
      throw new AppError(`${flavor.name} não está disponível no tamanho ${size.name}.`);
    }
    return flavor;
  });

  // Com mais de um sabor, so vale a opcao oferecida por todos eles.
  const groups = catalog.optionGroups.filter((group) =>
    flavors.every((flavor) => flavor.optionGroupIds.includes(group.id)),
  );
  const options = pickOptions(groups, item.optionIds ?? []);
  const base = pizzaBasePrice(flavors.map((flavor) => flavor.pricesBySize[size.id]), rule);

  return priced(item, `Pizza ${size.name}: ${flavorLabel(flavors.map((flavor) => flavor.name))}`, base, options, {
    size: size.name,
    flavors: flavors.map((flavor) => flavor.name),
  });
}

/** O preco sempre e calculado aqui: valores enviados pelo navegador nunca sao usados. */
export function priceCart(items: CartItem[], catalog: Catalog, rule: HalfPizzaRule, minute: number): PricedItem[] {
  return items.map((item) =>
    item.type === 'pizza' ? pricePizza(item, catalog, rule, minute) : priceSimple(item, catalog, minute),
  );
}
