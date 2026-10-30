# Gera include/web_index.h a partir de web/index.html (compactado com gzip).
# A pagina fica na flash de programa (rodata) e e servida sem acessar o sistema
# de arquivos -- leitura de LittleFS desliga o cache da flash e atrasaria as
# interrupcoes de ignicao/injecao com o motor girando.
import gzip
import os

Import("env")  # noqa: F821 (fornecido pelo PlatformIO)

root = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.join(root, "web", "index.html")
dst = os.path.join(root, "include", "web_index.h")

with open(src, "rb") as f:
    data = gzip.compress(f.read(), 9, mtime=0)

lines = []
for i in range(0, len(data), 20):
    lines.append("  " + ",".join(str(b) for b in data[i:i + 20]) + ",")

out = (
    "// Gerado automaticamente por tools/embed_web.py -- nao editar.\n"
    "#pragma once\n"
    "#include <Arduino.h>\n"
    f"const size_t WEB_INDEX_GZ_LEN = {len(data)};\n"
    "const uint8_t WEB_INDEX_GZ[] PROGMEM = {\n" + "\n".join(lines) + "\n};\n"
)

old = None
if os.path.exists(dst):
    with open(dst, "r") as f:
        old = f.read()
if old != out:
    with open(dst, "w") as f:
        f.write(out)
    print("web_index.h atualizado (%d bytes gzip)" % len(data))
