import { useMemo, useState } from 'react';
import type { CartItem, HalfPizzaRule } from '../../server/pricing.ts';
import { formatMoney } from '../lib.ts';
import type { CartLine, Menu, MenuProduct } from '../cart.ts';

interface Props {
  menu: Menu;
  product: MenuProduct;
  rule: HalfPizzaRule;
  onAdd: (line: Omit<CartLine, 'id'>) => void;
  onClose: () => void;
}

export function ItemSheet({ menu, product, rule, onAdd, onClose }: Props) {
  const isPizza = product.kind === 'pizza';
  const pricedSizes = menu.sizes.filter((size) => product.pricesBySize[size.id] !== undefined);
  const defaultSize = pricedSizes.find((size) => size.maxFlavors > 1) ?? pricedSizes[0] ?? menu.sizes[0];

  const [sizeId, setSizeId] = useState(defaultSize?.id ?? '');
  const [flavorIds, setFlavorIds] = useState([product.id]);
  const [optionIds, setOptionIds] = useState<string[]>([]);
  const [quantity, setQuantity] = useState(1);
  const [notes, setNotes] = useState('');

  const size = menu.sizes.find((candidate) => candidate.id === sizeId);
  const flavors = menu.products.filter((candidate) => candidate.kind === 'pizza');
  const selectedFlavors = flavorIds
    .map((id) => flavors.find((flavor) => flavor.id === id))
    .filter((flavor): flavor is MenuProduct => Boolean(flavor));

  const groups = useMemo(() => {
    const owners = isPizza ? selectedFlavors : [product];
    return menu.optionGroups.filter((group) => owners.every((owner) => owner.optionGroupIds.includes(group.id)));
  }, [isPizza, menu.optionGroups, product, selectedFlavors]);

  const visibleOptionIds = optionIds.filter((id) => groups.some((group) => group.options.some((o) => o.id === id)));

  function chooseSize(id: string) {
    const max = menu.sizes.find((candidate) => candidate.id === id)?.maxFlavors ?? 1;
    setSizeId(id);
    setFlavorIds((current) => current.slice(0, max));
  }

  function toggleFlavor(id: string) {
    setFlavorIds((current) => {
      if (current.includes(id)) return current.length > 1 ? current.filter((flavor) => flavor !== id) : current;
      const max = size?.maxFlavors ?? 1;
      return current.length < max ? [...current, id] : max === 1 ? [id] : current;
    });
  }

  function toggleOption(groupId: string, optionId: string) {
    const group = groups.find((candidate) => candidate.id === groupId);
    if (!group) return;
    const groupOptionIds = group.options.map((option) => option.id);
    const inGroup = visibleOptionIds.filter((id) => groupOptionIds.includes(id));

    if (visibleOptionIds.includes(optionId)) {
      setOptionIds(visibleOptionIds.filter((id) => id !== optionId));
    } else if (group.maxSelect === 1) {
      setOptionIds([...visibleOptionIds.filter((id) => !groupOptionIds.includes(id)), optionId]);
    } else if (inGroup.length < group.maxSelect) {
      setOptionIds([...visibleOptionIds, optionId]);
    }
  }

  const prices = selectedFlavors.map((flavor) => flavor.pricesBySize[sizeId] ?? 0);
  const base = !isPizza
    ? (product.priceCents ?? 0)
    : rule === 'highest'
      ? Math.max(...prices)
      : Math.ceil(prices.reduce((sum, price) => sum + price, 0) / prices.length);
  const optionTotal = groups
    .flatMap((group) => group.options)
    .filter((option) => visibleOptionIds.includes(option.id))
    .reduce((sum, option) => sum + option.priceCents, 0);
  const unitPriceCents = base + optionTotal;

  const missingGroup = groups.find(
    (group) => group.options.filter((option) => visibleOptionIds.includes(option.id)).length < group.minSelect,
  );

  function add() {
    const trimmed = notes.trim();
    const shared = { quantity, optionIds: visibleOptionIds, ...(trimmed ? { notes: trimmed } : {}) };
    const payload: CartItem = isPizza
      ? { type: 'pizza', sizeId, flavorIds, ...shared }
      : { type: 'simple', productId: product.id, ...shared };

    const flavorNames = selectedFlavors.map((flavor) => flavor.name);
    const name = isPizza
      ? `Pizza ${size?.name}: ${
          flavorNames.length === 1 ? flavorNames[0] : flavorNames.map((n) => `1/${flavorNames.length} ${n}`).join(' + ')
        }`
      : product.name;
    const details = groups
      .flatMap((group) => group.options)
      .filter((option) => visibleOptionIds.includes(option.id))
      .map((option) => option.name);

    onAdd({ payload, name, details, notes: trimmed, unitPriceCents });
  }

  return (
    <div className="sheet-backdrop" onClick={onClose}>
      <section
        className="sheet"
        role="dialog"
        aria-modal="true"
        aria-label={isPizza ? 'Montar pizza' : product.name}
        onClick={(event) => event.stopPropagation()}
      >
        <header className="sheet-header">
          <div>
            <h2>{isPizza ? 'Monte sua pizza' : product.name}</h2>
            {!isPizza && product.description && <p className="muted">{product.description}</p>}
          </div>
          <button className="icon-button" onClick={onClose} aria-label="Fechar">
            ×
          </button>
        </header>

        <div className="sheet-body">
          {isPizza && (
            <>
              <fieldset>
                <legend>Tamanho</legend>
                <div className="size-grid">
                  {menu.sizes.map((candidate) => (
                    <button
                      key={candidate.id}
                      className={`size-choice ${candidate.id === sizeId ? 'selected' : ''}`}
                      aria-pressed={candidate.id === sizeId}
                      onClick={() => chooseSize(candidate.id)}
                    >
                      <strong>{candidate.name}</strong>
                      <span>{candidate.slices} fatias</span>
                      <span>
                        até {candidate.maxFlavors} {candidate.maxFlavors > 1 ? 'sabores' : 'sabor'}
                      </span>
                    </button>
                  ))}
                </div>
              </fieldset>

              <fieldset>
                <legend>
                  Sabores
                  <span className="counter">
                    {flavorIds.length} de {size?.maxFlavors}
                  </span>
                </legend>
                {flavorIds.length > 1 && (
                  <p className="hint">
                    {rule === 'highest'
                      ? 'Em pizzas com mais de um sabor, vale o preço do sabor mais caro.'
                      : 'Em pizzas com mais de um sabor, vale a média dos preços.'}
                  </p>
                )}
                {flavors.map((flavor) => {
                  const selected = flavorIds.includes(flavor.id);
                  const full = !selected && flavorIds.length >= (size?.maxFlavors ?? 1) && size?.maxFlavors !== 1;
                  return (
                    <label key={flavor.id} className={`choice ${!flavor.available ? 'disabled' : ''}`}>
                      <input
                        type="checkbox"
                        checked={selected}
                        disabled={!flavor.available || full}
                        onChange={() => toggleFlavor(flavor.id)}
                      />
                      <span className="choice-name">{flavor.name}</span>
                      <span className="choice-price">
                        {flavor.available ? formatMoney(flavor.pricesBySize[sizeId] ?? 0) : 'Esgotado'}
                      </span>
                    </label>
                  );
                })}
              </fieldset>
            </>
          )}

          {groups.map((group) => (
            <fieldset key={group.id}>
              <legend>
                {group.name}
                <span className="counter">
                  {group.minSelect > 0 ? 'Obrigatório' : group.maxSelect > 1 ? `Até ${group.maxSelect}` : 'Opcional'}
                </span>
              </legend>
              {group.options.map((option) => (
                <label key={option.id} className={`choice ${!option.available ? 'disabled' : ''}`}>
                  <input
                    type="checkbox"
                    checked={visibleOptionIds.includes(option.id)}
                    disabled={!option.available}
                    onChange={() => toggleOption(group.id, option.id)}
                  />
                  <span className="choice-name">{option.name}</span>
                  <span className="choice-price">
                    {!option.available ? 'Esgotado' : option.priceCents ? `+ ${formatMoney(option.priceCents)}` : ''}
                  </span>
                </label>
              ))}
            </fieldset>
          ))}

          <label className="field">
            <span>Observações</span>
            <textarea
              rows={2}
              maxLength={200}
              value={notes}
              onChange={(event) => setNotes(event.target.value)}
              placeholder="Ex.: sem cebola, bem assada"
            />
          </label>
        </div>

        <footer className="sheet-footer">
          <div className="stepper" role="group" aria-label="Quantidade">
            <button onClick={() => setQuantity((value) => Math.max(1, value - 1))} aria-label="Diminuir">
              −
            </button>
            <output>{quantity}</output>
            <button onClick={() => setQuantity((value) => Math.min(50, value + 1))} aria-label="Aumentar">
              +
            </button>
          </div>
          <button className="button primary grow" disabled={Boolean(missingGroup)} onClick={add}>
            {missingGroup ? `Escolha: ${missingGroup.name}` : `Adicionar ${formatMoney(unitPriceCents * quantity)}`}
          </button>
        </footer>
      </section>
    </div>
  );
}
