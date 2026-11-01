import type { BillDetail } from '../server/cashier.ts';
import type { OrderView } from '../server/orders.ts';
import { formatMoney, formatTime, orderNumber, PAYMENT_LABEL } from './lib.ts';

const escapeHtml = (text: string) =>
  text.replace(/[&<>"']/g, (char) => `&#${char.charCodeAt(0)};`);

const row = (left: string, right: string) =>
  `<div class="row"><span>${escapeHtml(left)}</span><span>${escapeHtml(right)}</span></div>`;

const itemLines = (order: OrderView) =>
  order.items
    .map((item) => {
      const options = item.details.options.map((option) => option.name).join(', ');
      return [
        row(`${item.quantity}x ${item.name}`, formatMoney(item.totalCents)),
        options && `<p class="sub">${escapeHtml(options)}</p>`,
        item.notes && `<p class="sub">Obs.: ${escapeHtml(item.notes)}</p>`,
      ].join('');
    })
    .join('');

export function billReceiptHtml(bill: BillDetail, storeName: string): string {
  const orders = bill.orders.filter((order) => order.status !== 'cancelled');
  const payments = bill.payments.filter((payment) => !payment.voidedAt);
  return [
    `<h1>${escapeHtml(storeName)}</h1>`,
    `<p>${escapeHtml(bill.label)} - ${formatTime(new Date())}</p><hr>`,
    orders.map(itemLines).join(''),
    '<hr>',
    row('Subtotal', formatMoney(bill.subtotalCents)),
    bill.serviceCents ? row('Serviço', formatMoney(bill.serviceCents)) : '',
    bill.discountCents ? row('Desconto', `- ${formatMoney(bill.discountCents)}`) : '',
    `<strong>${row('Total', formatMoney(bill.totalCents))}</strong>`,
    payments.map((payment) => row(PAYMENT_LABEL[payment.method], formatMoney(payment.amountCents))).join(''),
    '<hr><p class="sub">Não é documento fiscal.</p>',
  ].join('');
}

export function kitchenTicketHtml(order: OrderView): string {
  const lines = order.items
    .map((item) => {
      const options = item.details.options.map((option) => option.name).join(', ');
      return [
        `<p class="item">${item.quantity}x ${escapeHtml(item.name)}</p>`,
        options && `<p>${escapeHtml(options)}</p>`,
        item.notes && `<p><strong>Obs.: ${escapeHtml(item.notes)}</strong></p>`,
      ].join('');
    })
    .join('');
  return [
    `<h1>Pedido ${orderNumber(order.number)}</h1>`,
    `<p>${escapeHtml(order.label)} - ${formatTime(order.createdAt)}</p><hr>`,
    lines,
  ].join('');
}

const STYLES = `
  @page { margin: 0; }
  body { width: 72mm; margin: 0; padding: 2mm; font: 12px monospace; color: #000; }
  h1 { margin: 0 0 4px; font-size: 16px; }
  p { margin: 2px 0; }
  hr { border: 0; border-top: 1px dashed #000; }
  .row { display: flex; justify-content: space-between; gap: 8px; }
  .row span:last-child { flex: none; }
  .sub { font-size: 11px; }
  .item { font-size: 14px; font-weight: bold; }
`;

/** Usa um iframe oculto: imprime em bobina de 80 mm sem sair da tela atual. */
export function printHtml(body: string) {
  const frame = document.createElement('iframe');
  frame.style.cssText = 'position:fixed;width:0;height:0;border:0';
  frame.srcdoc = `<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><style>${STYLES}</style></head><body>${body}</body></html>`;
  frame.onload = () => {
    frame.contentWindow?.print();
    setTimeout(() => frame.remove(), 60_000);
  };
  document.body.append(frame);
}
