import { useState, type ReactNode } from 'react';
import type { OptionGroup, PizzaSize, Product } from '../../server/catalog.ts';
import type { Menu } from '../cart.ts';
import { formatMoney, moneyInput, parseMoney } from '../lib.ts';
import type { Staff } from './context.ts';

type MenuCategory = Menu['categories'][number];

const timeInput = (minute: number | null) =>
  minute === null ? '' : `${String(Math.floor(minute / 60)).padStart(2, '0')}:${String(minute % 60).padStart(2, '0')}`;

function parseTime(value: string): number | null {
  if (!value) return null;
  const [hour, minute] = value.split(':').map(Number);
  return hour * 60 + minute;
}

type Editing =
  | { type: 'category'; item?: MenuCategory }
  | { type: 'product'; item?: Product; categoryId: string; kind: Product['kind'] }
  | { type: 'size'; item?: PizzaSize }
  | { type: 'group'; item?: OptionGroup }
  | null;

export function MenuEditor({ staff }: { staff: Staff }) {
  const { menu } = staff;
  const [editing, setEditing] = useState<Editing>(null);

  const remove = (path: string) => staff.run(() => staff.call(path, 'DELETE'));
  const reorder = (type: string, ids: string[], index: number, delta: number) => {
    const next = [...ids];
    const [moved] = next.splice(index, 1);
    next.splice(index + delta, 0, moved);
    return staff.run(() => staff.call('/api/admin/reorder', 'POST', { type, ids: next }));
  };

  const productIds = menu.products.map((product) => product.id);

  /** A ordem e global: troca o produto de lugar com o vizinho da mesma categoria. */
  function moveProduct(products: Product[], index: number, delta: number) {
    const from = productIds.indexOf(products[index].id);
    const to = productIds.indexOf(products[index + delta].id);
    return reorder('products', productIds, from, to - from);
  }

  return (
    <main className="menu-editor">
      <StoreSettings staff={staff} />

      <EditorSection
        title="Categorias e produtos"
        help="Organize o cardápio em categorias. Cada categoria pode ter produtos comuns e sabores de pizza."
        action={
          <button className="button primary" onClick={() => setEditing({ type: 'category' })}>
            Nova categoria
          </button>
        }
      >
        {menu.categories.length === 0 && <p className="muted">Crie a primeira categoria.</p>}
        {menu.categories.map((category, index) => {
          const products = menu.products.filter((product) => product.categoryId === category.id);
          return (
            <article key={category.id} className="editor-category">
              <header>
                <div>
                  <h3>{category.name}</h3>
                  {category.startMinute !== null && (
                    <p className="muted">
                      Das {timeInput(category.startMinute)} às {timeInput(category.endMinute)}
                    </p>
                  )}
                </div>
                <RowActions
                  label={category.name}
                  canUp={index > 0}
                  canDown={index < menu.categories.length - 1}
                  onMove={(delta) =>
                    reorder('categories', menu.categories.map((item) => item.id), index, delta)
                  }
                  onEdit={() => setEditing({ type: 'category', item: category })}
                  onDelete={() => remove(`/api/admin/categories/${category.id}`)}
                />
              </header>
              {products.map((product, productIndex) => (
                <div key={product.id} className="editor-row">
                  <div>
                    <strong>{product.name}</strong>
                    <p className="muted">
                      {product.kind === 'pizza'
                        ? menu.sizes
                            .filter((size) => product.pricesBySize[size.id] !== undefined)
                            .map((size) => `${size.name} ${formatMoney(product.pricesBySize[size.id])}`)
                            .join(' · ')
                        : formatMoney(product.priceCents ?? 0)}
                    </p>
                  </div>
                  <RowActions
                    label={product.name}
                    canUp={productIndex > 0}
                    canDown={productIndex < products.length - 1}
                    onMove={(delta) => moveProduct(products, productIndex, delta)}
                    onEdit={() =>
                      setEditing({ type: 'product', item: product, categoryId: category.id, kind: product.kind })
                    }
                    onDelete={() => remove(`/api/admin/products/${product.id}`)}
                  />
                </div>
              ))}
              <div className="card-links">
                <button
                  className="text-button"
                  onClick={() => setEditing({ type: 'product', categoryId: category.id, kind: 'simple' })}
                >
                  Adicionar produto
                </button>
                <button
                  className="text-button"
                  onClick={() => setEditing({ type: 'product', categoryId: category.id, kind: 'pizza' })}
                >
                  Adicionar sabor de pizza
                </button>
              </div>
            </article>
          );
        })}
      </EditorSection>

      <EditorSection
        title="Tamanhos de pizza"
        help="Cada sabor tem um preço por tamanho."
        action={
          <button className="button" onClick={() => setEditing({ type: 'size' })}>
            Novo tamanho
          </button>
        }
      >
        {menu.sizes.map((size, index) => (
          <div key={size.id} className="editor-row">
            <div>
              <strong>{size.name}</strong>
              <p className="muted">
                {size.slices} fatias · até {size.maxFlavors} {size.maxFlavors > 1 ? 'sabores' : 'sabor'}
              </p>
            </div>
            <RowActions
              label={size.name}
              canUp={index > 0}
              canDown={index < menu.sizes.length - 1}
              onMove={(delta) => reorder('sizes', menu.sizes.map((item) => item.id), index, delta)}
              onEdit={() => setEditing({ type: 'size', item: size })}
              onDelete={() => remove(`/api/admin/sizes/${size.id}`)}
            />
          </div>
        ))}
      </EditorSection>

      <EditorSection
        title="Adicionais e opções"
        help="Grupos como borda, adicionais ou o sabor de um combo. Depois, ligue o grupo aos produtos."
        action={
          <button className="button" onClick={() => setEditing({ type: 'group' })}>
            Novo grupo
          </button>
        }
      >
        {menu.optionGroups.map((group, index) => (
          <div key={group.id} className="editor-row">
            <div>
              <strong>{group.name}</strong>
              <p className="muted">
                {group.options.map((option) => option.name).join(', ')}
              </p>
            </div>
            <RowActions
              label={group.name}
              canUp={index > 0}
              canDown={index < menu.optionGroups.length - 1}
              onMove={(delta) => reorder('groups', menu.optionGroups.map((item) => item.id), index, delta)}
              onEdit={() => setEditing({ type: 'group', item: group })}
              onDelete={() => remove(`/api/admin/option-groups/${group.id}`)}
            />
          </div>
        ))}
      </EditorSection>

      <EditorSection
        title="Mesas"
        help={`A loja tem ${staff.tables.length} mesas. Uma mesa com histórico de contas não pode ser excluída.`}
        action={
          <button className="button" onClick={() => staff.run(() => staff.call('/api/admin/tables', 'POST'))}>
            Adicionar mesa
          </button>
        }
      >
        <div className="table-chips">
          {staff.tables.map((table) => (
            <ConfirmDelete
              key={table.id}
              label={`Mesa ${table.number}`}
              text={`Mesa ${table.number} ×`}
              className="chip"
              onConfirm={() => remove(`/api/admin/tables/${table.id}`)}
            />
          ))}
        </div>
      </EditorSection>

      {editing?.type === 'category' && (
        <CategoryForm staff={staff} item={editing.item} onClose={() => setEditing(null)} />
      )}
      {editing?.type === 'product' && (
        <ProductForm
          key={editing.item?.id ?? 'novo'}
          staff={staff}
          item={editing.item}
          categoryId={editing.categoryId}
          kind={editing.kind}
          onClose={() => setEditing(null)}
        />
      )}
      {editing?.type === 'size' && <SizeForm staff={staff} item={editing.item} onClose={() => setEditing(null)} />}
      {editing?.type === 'group' && <GroupForm staff={staff} item={editing.item} onClose={() => setEditing(null)} />}
    </main>
  );
}

function EditorSection({
  title,
  help,
  action,
  children,
}: {
  title: string;
  help: string;
  action: ReactNode;
  children: ReactNode;
}) {
  return (
    <section className="editor-section">
      <header>
        <div>
          <h2>{title}</h2>
          <p className="muted">{help}</p>
        </div>
        {action}
      </header>
      {children}
    </section>
  );
}

function RowActions({
  label,
  canUp,
  canDown,
  onMove,
  onEdit,
  onDelete,
}: {
  label: string;
  canUp: boolean;
  canDown: boolean;
  onMove: (delta: number) => void;
  onEdit: () => void;
  onDelete: () => void;
}) {
  return (
    <div className="row-actions">
      <button className="arrow" disabled={!canUp} onClick={() => onMove(-1)} aria-label={`Subir ${label}`}>
        ↑
      </button>
      <button className="arrow" disabled={!canDown} onClick={() => onMove(1)} aria-label={`Descer ${label}`}>
        ↓
      </button>
      <button className="text-button" onClick={onEdit} aria-label={`Editar ${label}`}>
        Editar
      </button>
      <ConfirmDelete label={label} text="Excluir" className="text-button" onConfirm={onDelete} />
    </div>
  );
}

function ConfirmDelete({
  label,
  text,
  className,
  onConfirm,
}: {
  label: string;
  text: string;
  className: string;
  onConfirm: () => void;
}) {
  const [confirming, setConfirming] = useState(false);
  if (confirming) {
    return (
      <span className="confirm">
        <button
          className="text-button danger-text"
          onClick={() => {
            setConfirming(false);
            onConfirm();
          }}
        >
          Confirmar exclusão
        </button>
        <button className="text-button" onClick={() => setConfirming(false)}>
          Voltar
        </button>
      </span>
    );
  }
  return (
    <button className={className} onClick={() => setConfirming(true)} aria-label={`Excluir ${label}`}>
      {text}
    </button>
  );
}

function FormDialog({
  title,
  onClose,
  onSubmit,
  invalid,
  children,
}: {
  title: string;
  onClose: () => void;
  onSubmit: () => Promise<boolean>;
  invalid?: string;
  children: ReactNode;
}) {
  const [busy, setBusy] = useState(false);

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    setBusy(true);
    if (await onSubmit()) onClose();
    else setBusy(false);
  }

  return (
    <div className="sheet-backdrop" onClick={onClose}>
      <form
        className="sheet"
        role="dialog"
        aria-modal="true"
        aria-label={title}
        onSubmit={submit}
        onClick={(event) => event.stopPropagation()}
      >
        <header className="sheet-header">
          <h2>{title}</h2>
          <button type="button" className="icon-button" onClick={onClose} aria-label="Fechar">
            ×
          </button>
        </header>
        <div className="sheet-body">{children}</div>
        <footer className="sheet-footer">
          <button className="button primary grow" disabled={busy || Boolean(invalid)}>
            {invalid ?? 'Salvar'}
          </button>
        </footer>
      </form>
    </div>
  );
}

function StoreSettings({ staff }: { staff: Staff }) {
  const { store } = staff.me;
  const [name, setName] = useState(store.name);
  const [rule, setRule] = useState(store.halfPizzaRule);
  const [service, setService] = useState(String(store.servicePercent));
  const percent = Number(service);
  const valid = name.trim() !== '' && Number.isInteger(percent) && percent >= 0 && percent <= 30;

  return (
    <section className="editor-section">
      <header>
        <h2>Dados da loja</h2>
      </header>
      <div className="form-row wrap">
        <label className="field">
          <span>Nome</span>
          <input maxLength={80} value={name} onChange={(event) => setName(event.target.value)} />
        </label>
        <label className="field">
          <span>Pizza com mais de um sabor</span>
          <select value={rule} onChange={(event) => setRule(event.target.value as typeof rule)}>
            <option value="highest">Cobra o sabor mais caro</option>
            <option value="average">Cobra a média dos sabores</option>
          </select>
        </label>
        <label className="field">
          <span>Taxa de serviço (%)</span>
          <input inputMode="numeric" value={service} onChange={(event) => setService(event.target.value)} />
        </label>
      </div>
      <button
        className="button"
        disabled={!valid}
        onClick={() =>
          staff.run(() =>
            staff.call('/api/admin/store', 'PUT', { name: name.trim(), halfPizzaRule: rule, servicePercent: percent }),
          )
        }
      >
        Salvar dados da loja
      </button>
    </section>
  );
}

function CategoryForm({ staff, item, onClose }: { staff: Staff; item?: MenuCategory; onClose: () => void }) {
  const [name, setName] = useState(item?.name ?? '');
  const [start, setStart] = useState(timeInput(item?.startMinute ?? null));
  const [end, setEnd] = useState(timeInput(item?.endMinute ?? null));

  return (
    <FormDialog
      title={item ? 'Editar categoria' : 'Nova categoria'}
      onClose={onClose}
      invalid={name.trim() ? undefined : 'Informe o nome'}
      onSubmit={() =>
        staff.run(() =>
          staff.call(item ? `/api/admin/categories/${item.id}` : '/api/admin/categories', item ? 'PUT' : 'POST', {
            name: name.trim(),
            startMinute: parseTime(start),
            endMinute: parseTime(end),
          }),
        )
      }
    >
      <label className="field">
        <span>Nome</span>
        <input autoFocus maxLength={80} value={name} onChange={(event) => setName(event.target.value)} />
      </label>
      <p className="muted">Deixe os horários vazios para vender o dia todo.</p>
      <div className="form-row">
        <label className="field">
          <span>Vender a partir de</span>
          <input type="time" value={start} onChange={(event) => setStart(event.target.value)} />
        </label>
        <label className="field">
          <span>Até</span>
          <input type="time" value={end} onChange={(event) => setEnd(event.target.value)} />
        </label>
      </div>
    </FormDialog>
  );
}

function ProductForm({
  staff,
  item,
  categoryId,
  kind,
  onClose,
}: {
  staff: Staff;
  item?: Product;
  categoryId: string;
  kind: Product['kind'];
  onClose: () => void;
}) {
  const { menu } = staff;
  const [name, setName] = useState(item?.name ?? '');
  const [description, setDescription] = useState(item?.description ?? '');
  const [category, setCategory] = useState(categoryId);
  const [price, setPrice] = useState(item?.priceCents != null ? moneyInput(item.priceCents) : '');
  const [sizePrices, setSizePrices] = useState<Record<string, string>>(() =>
    Object.fromEntries(Object.entries(item?.pricesBySize ?? {}).map(([id, cents]) => [id, moneyInput(cents)])),
  );
  const [groupIds, setGroupIds] = useState(item?.optionGroupIds ?? []);

  const filledSizes = Object.entries(sizePrices).filter(([, value]) => value.trim() !== '');
  const parsedSizes = filledSizes.map(([id, value]) => [id, parseMoney(value)] as const);

  let invalid: string | undefined;
  if (!name.trim()) invalid = 'Informe o nome';
  else if (kind === 'simple' && parseMoney(price) === null) invalid = 'Informe o preço';
  else if (kind === 'pizza' && parsedSizes.length === 0) invalid = 'Informe ao menos um preço';
  else if (parsedSizes.some(([, cents]) => cents === null)) invalid = 'Corrija os preços';

  return (
    <FormDialog
      title={`${item ? 'Editar' : 'Novo'} ${kind === 'pizza' ? 'sabor de pizza' : 'produto'}`}
      onClose={onClose}
      invalid={invalid}
      onSubmit={() =>
        staff.run(() =>
          staff.call(item ? `/api/admin/products/${item.id}` : '/api/admin/products', item ? 'PUT' : 'POST', {
            categoryId: category,
            kind,
            name: name.trim(),
            description: description.trim(),
            priceCents: kind === 'simple' ? parseMoney(price) : null,
            pricesBySize: kind === 'pizza' ? Object.fromEntries(parsedSizes) : {},
            optionGroupIds: groupIds,
          }),
        )
      }
    >
      <label className="field">
        <span>Nome</span>
        <input autoFocus maxLength={80} value={name} onChange={(event) => setName(event.target.value)} />
      </label>
      <label className="field">
        <span>Descrição</span>
        <textarea
          rows={2}
          maxLength={300}
          value={description}
          onChange={(event) => setDescription(event.target.value)}
        />
      </label>
      <label className="field">
        <span>Categoria</span>
        <select value={category} onChange={(event) => setCategory(event.target.value)}>
          {menu.categories.map((option) => (
            <option key={option.id} value={option.id}>
              {option.name}
            </option>
          ))}
        </select>
      </label>

      {kind === 'simple' ? (
        <label className="field">
          <span>Preço (R$)</span>
          <input inputMode="decimal" value={price} onChange={(event) => setPrice(event.target.value)} />
        </label>
      ) : (
        <fieldset>
          <legend>Preço por tamanho (R$)</legend>
          <p className="muted">Deixe vazio o tamanho em que o sabor não é vendido.</p>
          <div className="form-row wrap">
            {menu.sizes.map((size) => (
              <label key={size.id} className="field">
                <span>{size.name}</span>
                <input
                  inputMode="decimal"
                  value={sizePrices[size.id] ?? ''}
                  onChange={(event) => setSizePrices({ ...sizePrices, [size.id]: event.target.value })}
                />
              </label>
            ))}
          </div>
        </fieldset>
      )}

      {menu.optionGroups.length > 0 && (
        <fieldset>
          <legend>Adicionais oferecidos</legend>
          {menu.optionGroups.map((group) => (
            <label key={group.id} className="choice">
              <input
                type="checkbox"
                checked={groupIds.includes(group.id)}
                onChange={(event) =>
                  setGroupIds(
                    event.target.checked ? [...groupIds, group.id] : groupIds.filter((id) => id !== group.id),
                  )
                }
              />
              <span className="choice-name">{group.name}</span>
            </label>
          ))}
        </fieldset>
      )}
    </FormDialog>
  );
}

function SizeForm({ staff, item, onClose }: { staff: Staff; item?: PizzaSize; onClose: () => void }) {
  const [name, setName] = useState(item?.name ?? '');
  const [slices, setSlices] = useState(String(item?.slices ?? 8));
  const [maxFlavors, setMaxFlavors] = useState(String(item?.maxFlavors ?? 2));

  return (
    <FormDialog
      title={item ? 'Editar tamanho' : 'Novo tamanho'}
      onClose={onClose}
      invalid={name.trim() && Number(slices) >= 1 ? undefined : 'Preencha os campos'}
      onSubmit={() =>
        staff.run(() =>
          staff.call(item ? `/api/admin/sizes/${item.id}` : '/api/admin/sizes', item ? 'PUT' : 'POST', {
            name: name.trim(),
            slices: Number(slices),
            maxFlavors: Number(maxFlavors),
          }),
        )
      }
    >
      <label className="field">
        <span>Nome</span>
        <input autoFocus maxLength={80} value={name} onChange={(event) => setName(event.target.value)} />
      </label>
      <div className="form-row">
        <label className="field">
          <span>Fatias</span>
          <input inputMode="numeric" value={slices} onChange={(event) => setSlices(event.target.value)} />
        </label>
        <label className="field">
          <span>Máximo de sabores</span>
          <select value={maxFlavors} onChange={(event) => setMaxFlavors(event.target.value)}>
            {[1, 2, 3, 4].map((value) => (
              <option key={value}>{value}</option>
            ))}
          </select>
        </label>
      </div>
    </FormDialog>
  );
}

interface OptionDraft {
  key: string;
  id?: string;
  name: string;
  price: string;
}

function GroupForm({ staff, item, onClose }: { staff: Staff; item?: OptionGroup; onClose: () => void }) {
  const [name, setName] = useState(item?.name ?? '');
  const [required, setRequired] = useState((item?.minSelect ?? 0) > 0);
  const [maxSelect, setMaxSelect] = useState(String(item?.maxSelect ?? 1));
  const [options, setOptions] = useState<OptionDraft[]>(
    item?.options.map((option) => ({
      key: option.id,
      id: option.id,
      name: option.name,
      price: moneyInput(option.priceCents),
    })) ?? [{ key: crypto.getRandomValues(new Uint32Array(1))[0].toString(), name: '', price: '0,00' }],
  );

  const update = (key: string, change: Partial<OptionDraft>) =>
    setOptions(options.map((option) => (option.key === key ? { ...option, ...change } : option)));

  const max = Number(maxSelect);
  let invalid: string | undefined;
  if (!name.trim()) invalid = 'Informe o nome';
  else if (options.length === 0 || options.some((option) => !option.name.trim())) invalid = 'Dê nome às opções';
  else if (options.some((option) => parseMoney(option.price) === null)) invalid = 'Corrija os preços';
  else if (!Number.isInteger(max) || max < 1) invalid = 'Informe o máximo';

  return (
    <FormDialog
      title={item ? 'Editar grupo' : 'Novo grupo'}
      onClose={onClose}
      invalid={invalid}
      onSubmit={() =>
        staff.run(() =>
          staff.call(item ? `/api/admin/option-groups/${item.id}` : '/api/admin/option-groups', item ? 'PUT' : 'POST', {
            name: name.trim(),
            minSelect: required ? 1 : 0,
            maxSelect: max,
            options: options.map((option) => ({
              ...(option.id ? { id: option.id } : {}),
              name: option.name.trim(),
              priceCents: parseMoney(option.price),
            })),
          }),
        )
      }
    >
      <label className="field">
        <span>Nome do grupo</span>
        <input autoFocus maxLength={80} value={name} onChange={(event) => setName(event.target.value)} />
      </label>
      <div className="form-row">
        <label className="field">
          <span>Máximo de escolhas</span>
          <input inputMode="numeric" value={maxSelect} onChange={(event) => setMaxSelect(event.target.value)} />
        </label>
        <label className="choice">
          <input type="checkbox" checked={required} onChange={(event) => setRequired(event.target.checked)} />
          <span className="choice-name">Escolha obrigatória</span>
        </label>
      </div>
      <fieldset>
        <legend>Opções</legend>
        {options.map((option) => (
          <div key={option.key} className="option-draft">
            <input
              aria-label="Nome da opção"
              placeholder="Nome"
              maxLength={80}
              value={option.name}
              onChange={(event) => update(option.key, { name: event.target.value })}
            />
            <input
              aria-label="Preço da opção"
              inputMode="decimal"
              value={option.price}
              onChange={(event) => update(option.key, { price: event.target.value })}
            />
            <button
              type="button"
              className="icon-button"
              aria-label={`Remover ${option.name || 'opção'}`}
              onClick={() => setOptions(options.filter((candidate) => candidate.key !== option.key))}
            >
              ×
            </button>
          </div>
        ))}
        <button
          type="button"
          className="text-button"
          onClick={() =>
            setOptions([
              ...options,
              { key: crypto.getRandomValues(new Uint32Array(1))[0].toString(), name: '', price: '0,00' },
            ])
          }
        >
          Adicionar opção
        </button>
      </fieldset>
    </FormDialog>
  );
}
