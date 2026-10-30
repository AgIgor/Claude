// Desenho técnico do carro (vista lateral, voltado para a direita), em SVG.

export function svgCarro(largura: number, cotas = false): string {
  const altura = (largura * 130) / 400;
  return `
<svg viewBox="0 0 400 ${cotas ? 160 : 130}" width="${largura}" height="${cotas ? (largura * 160) / 400 : altura}" aria-hidden="true">
  <g fill="none" stroke="#e8f1fa" stroke-width="2" stroke-linejoin="round" stroke-linecap="round">
    <path d="M22,98 L20,70 L30,62 L52,26 L200,22 L254,54 L368,60 L382,70 L384,98" />
    <path d="M22,98 L66,98 M124,98 L276,98 M334,98 L384,98" />
    <path d="M66,98 A29,29 0 0 1 124,98" />
    <path d="M276,98 A29,29 0 0 1 334,98" />
    <path d="M62,32 L196,28 L238,54 L60,56 Z" stroke-width="1.5" />
    <path d="M140,30 L140,56" stroke-width="1.5" />
    <path d="M60,56 L60,92 M238,54 L240,92" stroke-width="1" opacity="0.6" />
    <path d="M30,62 L52,62" stroke-width="1" opacity="0.6" />
    <rect x="364" y="64" width="16" height="8" stroke-width="1.5" />
    <rect x="22" y="70" width="10" height="10" stroke-width="1.5" />
  </g>
  <g fill="#0b2742" stroke="#e8f1fa" stroke-width="2">
    <circle cx="95" cy="98" r="24" />
    <circle cx="305" cy="98" r="24" />
  </g>
  <g fill="none" stroke="#9fbcd8" stroke-width="1">
    <circle cx="95" cy="98" r="12" />
    <circle cx="305" cy="98" r="12" />
    <path d="M95,86 L95,110 M83,98 L107,98 M305,86 L305,110 M293,98 L317,98" opacity="0.6" />
  </g>
  ${cotas ? `
  <g stroke="#ffc24b" stroke-width="1" fill="#ffc24b" font-family="IBM Plex Mono, monospace" font-size="11">
    <path d="M95,130 L95,144 M305,130 L305,144 M95,138 L305,138" />
    <path d="M95,138 l6,-3 l0,6 z M305,138 l-6,-3 l0,6 z" stroke="none" />
    <text x="200" y="154" text-anchor="middle" stroke="none">entre-eixos 2.360 mm</text>
  </g>` : ''}
</svg>`;
}
