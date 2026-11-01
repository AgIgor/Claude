"""Gera src/esp01/pages_gz.h: as páginas comprimidas para o ESP-01.

Roda sozinho antes de compilar o ambiente esp01. Comprimir encurta o tempo em que o laço principal
fica enviando a página sem atender o ar.
"""
import gzip
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent if '__file__' in globals() else Path.cwd()
PAGES = {
    'CONTROL_GZ': ROOT / 'src' / 'web_control.h',
    'SETUP_GZ': ROOT / 'src' / 'esp01' / 'web_setup.h',
}
OUTPUT = ROOT / 'src' / 'esp01' / 'pages_gz.h'
BYTES_PER_LINE = 20


def compress(header):
    html = re.search(r'R"HTML\((.*)\)HTML"', header.read_text(encoding='utf8'), re.S).group(1)
    return gzip.compress(html.encode('utf8'), 9, mtime=0)


def render():
    lines = ['#pragma once', '// Gerado por tools/gzip_pages.py. Não edite.', '#include <pgmspace.h>', '']
    for name, header in PAGES.items():
        data = compress(header)
        lines.append('static const uint8_t %s[] PROGMEM = {' % name)
        for i in range(0, len(data), BYTES_PER_LINE):
            lines.append('  ' + ','.join(str(b) for b in data[i:i + BYTES_PER_LINE]) + ',')
        lines += ['};', '']
    return '\n'.join(lines)


text = render()
if not OUTPUT.exists() or OUTPUT.read_text(encoding='utf8') != text:
    OUTPUT.write_text(text, encoding='utf8')
