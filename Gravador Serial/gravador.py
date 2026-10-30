"""Gravador Serial - upload de firmware para Arduino (AVR) e ESP8266/ESP32.

Faz o mesmo que o "Upload" do PlatformIO, mas a partir de firmware já compilado:
  - Arduino: arquivo .hex via avrdude
  - ESP8266/ESP32: arquivo(s) .bin via esptool

Também aceita uma pasta de projeto PlatformIO e encontra sozinho os arquivos
gerados em .pio/build/<env>.

E grava o sistema de arquivos (LittleFS / SPIFFS) a partir da pasta "data",
como o "Upload Filesystem Image" do PlatformIO.

Sketches .ino são compilados com o arduino-cli (que baixa os cores sob demanda)
e depois gravados pelo mesmo caminho dos arquivos .hex/.bin.
"""

import hashlib
import json
import os
import queue
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import tkinter as tk
from collections import deque
from tkinter import filedialog, messagebox, ttk

import serial
import serial.tools.list_ports

APP_NAME = "Gravador Serial"

# Quando roda como .exe (PyInstaller), os arquivos ficam em sys._MEIPASS
BASE_DIR = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
AVRDUDE = os.path.join(BASE_DIR, "tools", "avrdude", "avrdude.exe")
AVRDUDE_CONF = os.path.join(BASE_DIR, "tools", "avrdude", "avrdude.conf")
BOOT_APP0 = os.path.join(BASE_DIR, "tools", "esp32", "boot_app0.bin")
FS_TOOLS = os.path.join(BASE_DIR, "tools", "fs")
ARDUINO_CLI = os.path.join(BASE_DIR, "tools", "arduino-cli", "arduino-cli.exe")
BUILD_CACHE = os.path.join(os.environ.get("LOCALAPPDATA", tempfile.gettempdir()), "GravadorSerial", "build")
# Índices extras do gerenciador de placas (ESP32 e ESP8266)
BOARD_URLS = ",".join([
    "https://espressif.github.io/arduino-esp32/package_esp32_index.json",
    "https://arduino.esp8266.com/stable/package_esp8266com_index.json",
])
CONFIG_FILE = os.path.join(os.environ.get("APPDATA", BASE_DIR), "GravadorSerial.json")
PIO_HOME = os.path.join(os.path.expanduser("~"), ".platformio")

# Placas pré-configuradas. "kind" define a ferramenta usada e
# "fqbn" é a placa usada pelo arduino-cli para compilar sketches .ino.
BOARDS = {
    "Arduino Uno": dict(kind="avr", mcu="atmega328p", protocol="arduino", baud=115200,
                        fqbn="arduino:avr:uno"),
    "Arduino Nano (ATmega328P)": dict(kind="avr", mcu="atmega328p", protocol="arduino", baud=115200,
                                      fqbn="arduino:avr:nano:cpu=atmega328"),
    "Arduino Nano (bootloader antigo)": dict(kind="avr", mcu="atmega328p", protocol="arduino", baud=57600,
                                             fqbn="arduino:avr:nano:cpu=atmega328old"),
    "Arduino Pro Mini 5V/16MHz": dict(kind="avr", mcu="atmega328p", protocol="arduino", baud=57600,
                                      fqbn="arduino:avr:pro:cpu=16MHzatmega328"),
    "Arduino Pro Mini 3.3V/8MHz": dict(kind="avr", mcu="atmega328p", protocol="arduino", baud=57600,
                                       fqbn="arduino:avr:pro:cpu=8MHzatmega328"),
    "Arduino Mega 2560": dict(kind="avr", mcu="atmega2560", protocol="wiring", baud=115200,
                              fqbn="arduino:avr:mega:cpu=atmega2560"),
    "Arduino Leonardo / Micro / Pro Micro": dict(kind="avr", mcu="atmega32u4", protocol="avr109", baud=57600,
                                                 touch1200=True, fqbn="arduino:avr:leonardo"),
    "ESP8266 (NodeMCU, Wemos D1...)": dict(kind="esp", chip="esp8266", baud=921600,
                                           fqbn="esp8266:esp8266:nodemcuv2"),
    "ESP32": dict(kind="esp", chip="esp32", baud=921600, fqbn="esp32:esp32:esp32"),
    "ESP32-S2": dict(kind="esp", chip="esp32s2", baud=921600, fqbn="esp32:esp32:esp32s2"),
    "ESP32-S3": dict(kind="esp", chip="esp32s3", baud=921600, fqbn="esp32:esp32:esp32s3"),
    "ESP32-C3": dict(kind="esp", chip="esp32c3", baud=921600, fqbn="esp32:esp32:esp32c3"),
    "ESP32-C6": dict(kind="esp", chip="esp32c6", baud=921600, fqbn="esp32:esp32:esp32c6"),
}

# Endereço do bootloader varia conforme o chip ESP32
ESP_BOOTLOADER_ADDR = {"esp32": "0x1000", "esp32s2": "0x1000"}  # demais: 0x0

# Layouts de flash do ESP8266 (arquivos eagle.flash.*.ld do core Arduino):
# ldscript -> (início do FS, fim do FS, tamanho do bloco). Endereços em 0x40200000+.
ESP8266_LD = {
    "eagle.flash.512k32.ld": (0x40273000, 0x4027B000, 0x1000),
    "eagle.flash.512k64.ld": (0x4026B000, 0x4027B000, 0x1000),
    "eagle.flash.512k128.ld": (0x4025B000, 0x4027B000, 0x1000),
    "eagle.flash.1m64.ld": (0x402EB000, 0x402FB000, 0x1000),
    "eagle.flash.1m128.ld": (0x402DB000, 0x402FB000, 0x1000),
    "eagle.flash.1m144.ld": (0x402D7000, 0x402FB000, 0x1000),
    "eagle.flash.1m160.ld": (0x402D3000, 0x402FB000, 0x1000),
    "eagle.flash.1m192.ld": (0x402CB000, 0x402FB000, 0x1000),
    "eagle.flash.1m256.ld": (0x402BB000, 0x402FB000, 0x1000),
    "eagle.flash.1m512.ld": (0x4027B000, 0x402FB000, 0x2000),
    "eagle.flash.2m64.ld": (0x403F0000, 0x403FB000, 0x1000),
    "eagle.flash.2m128.ld": (0x403E0000, 0x403FB000, 0x1000),
    "eagle.flash.2m256.ld": (0x403C0000, 0x403FB000, 0x1000),
    "eagle.flash.2m512.ld": (0x40380000, 0x403FA000, 0x2000),
    "eagle.flash.2m1m.ld": (0x40300000, 0x403FA000, 0x2000),
    "eagle.flash.4m1m.ld": (0x40500000, 0x405FA000, 0x2000),
    "eagle.flash.4m2m.ld": (0x40400000, 0x405FA000, 0x2000),
    "eagle.flash.4m3m.ld": (0x40300000, 0x405FA000, 0x2000),
    "eagle.flash.8m6m.ld": (0x40400000, 0x409FA000, 0x2000),
    "eagle.flash.8m7m.ld": (0x40300000, 0x409FA000, 0x2000),
    "eagle.flash.16m14m.ld": (0x40400000, 0x411FA000, 0x2000),
    "eagle.flash.16m15m.ld": (0x40300000, 0x411FA000, 0x2000),
}


def _esp8266_layout_name(ld):
    m = re.search(r"\.(\d+[mk])(\d+[mk]?)\.ld$", ld)
    flash, fs = m.group(1).upper(), m.group(2).upper()
    fs = fs if fs[-1] in "MK" else fs + "K"
    return f"{flash}B (FS {fs}B) - {ld}"


ESP8266_LAYOUTS = {}
for _ld, (_start, _end, _block) in ESP8266_LD.items():
    ESP8266_LAYOUTS[_esp8266_layout_name(_ld)] = (_start - 0x40200000, _end - _start, _block)

# Tabelas de partição padrão do Arduino-ESP32: nome -> (offset, tamanho, bloco)
ESP32_LAYOUTS = {
    "4MB padrão (default.csv)": (0x290000, 0x160000, 0x1000),
    "4MB min_spiffs.csv": (0x3D0000, 0x20000, 0x1000),
    "4MB no_ota.csv": (0x210000, 0x1E0000, 0x1000),
    "4MB huge_app.csv": (0x310000, 0xE0000, 0x1000),
    "8MB padrão (default_8MB.csv)": (0x670000, 0x180000, 0x1000),
    "16MB padrão (default_16MB.csv)": (0xC90000, 0x360000, 0x1000),
    "16MB large_spiffs_16MB.csv": (0x910000, 0x6E0000, 0x1000),
}
DEFAULT_LAYOUT = {"esp8266": "4MB (FS 2MB) - eagle.flash.4m2m.ld", "esp32": "4MB padrão (default.csv)"}
LAYOUT_FROM_PROJECT = "(do projeto)"

# Layout de FS escolhido -> opção da placa no arduino-cli, para que o sketch
# seja compilado com a mesma tabela de partições em que o FS será gravado.
ESP32_SCHEMES = {
    "4MB padrão (default.csv)": "PartitionScheme=default",
    "4MB min_spiffs.csv": "PartitionScheme=min_spiffs",
    "4MB no_ota.csv": "PartitionScheme=no_ota",
    "4MB huge_app.csv": "PartitionScheme=huge_app",
    "8MB padrão (default_8MB.csv)": "FlashSize=8M,PartitionScheme=default_8MB",
}
ESP8266_EESZ = {"eagle.flash.4m1m.ld": "4M1M", "eagle.flash.4m2m.ld": "4M2M", "eagle.flash.4m3m.ld": "4M3M"}


def fqbn_fs_option(chip, layout):
    """Opção extra do FQBN para o layout de FS, ou None se não houver equivalente."""
    if chip == "esp8266":
        ld = layout.rsplit(" - ", 1)[-1]
        return f"eesz={ESP8266_EESZ[ld]}" if ld in ESP8266_EESZ else None
    return ESP32_SCHEMES.get(layout)
LAYOUT_FROM_FILE = "(do arquivo de partições)"

ESP_BAUDS = ["115200", "230400", "460800", "921600", "1500000"]
AVR_BAUDS = ["19200", "57600", "115200"]


def decode(data):
    """esptool fala UTF-8; avrdude usa a página de código do Windows."""
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


# --------------------------------------------------------------------------
# Detecção de projeto PlatformIO
# --------------------------------------------------------------------------

def read_platformio_ini(project_dir):
    """Lê o platformio.ini. Retorna ({env: opções}, opções de [platformio]).

    As opções de cada env já vêm mescladas com [env] e com "extends".
    """
    sections, current = {}, None
    path = os.path.join(project_dir, "platformio.ini")
    if not os.path.isfile(path):
        return {}, {}
    with open(path, encoding="utf-8", errors="ignore") as f:
        for raw in f:
            line = raw.split(";")[0].rstrip()
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            m = re.match(r"^\[([^\]]+)\]\s*$", line.strip())
            if m:
                current = m.group(1).strip()
                sections.setdefault(current, {})
            elif current and "=" in line and not line[0].isspace():
                k, v = line.split("=", 1)
                sections[current][k.strip()] = v.strip()

    def resolve(name, seen=()):
        opts = {}
        own = sections.get(name, {})
        for parent in filter(None, (x.strip() for x in own.get("extends", "").split(","))):
            if parent not in seen:
                opts.update(resolve(parent, seen + (name,)))
        opts.update(own)
        return opts

    envs = {}
    for name in sections:
        if name.startswith("env:"):
            envs[name[4:].strip()] = dict(sections.get("env", {}), **resolve(name))
    return envs, sections.get("platformio", {})


def find_board_json(board_id):
    """Procura a definição da placa nas plataformas instaladas do PlatformIO."""
    platforms = os.path.join(PIO_HOME, "platforms")
    if not board_id or not os.path.isdir(platforms):
        return None
    for plat in os.listdir(platforms):
        p = os.path.join(platforms, plat, "boards", board_id + ".json")
        if os.path.isfile(p):
            try:
                with open(p, encoding="utf-8") as f:
                    return json.load(f)
            except (OSError, ValueError):
                return None
    return None


def board_from_pio(env_cfg, build_dir):
    """Monta a configuração de gravação a partir do env do PlatformIO."""
    bj = find_board_json(env_cfg.get("board", "")) or {}
    build, upload = bj.get("build", {}), bj.get("upload", {})
    mcu = build.get("mcu", "").lower()
    has_hex = os.path.isfile(os.path.join(build_dir, "firmware.hex"))
    has_bin = os.path.isfile(os.path.join(build_dir, "firmware.bin"))

    if has_hex:
        cfg = dict(kind="avr", mcu=mcu or "atmega328p",
                   protocol=env_cfg.get("upload_protocol", upload.get("protocol", "arduino")),
                   baud=int(env_cfg.get("upload_speed", upload.get("speed", 115200))),
                   touch1200=bool(upload.get("use_1200bps_touch")))
        return cfg
    if has_bin:
        chip = mcu if mcu.startswith("esp") else ("esp8266" if "8266" in env_cfg.get("platform", "") else "esp32")
        return dict(kind="esp", chip=chip,
                    baud=int(env_cfg.get("upload_speed", upload.get("speed", 921600))))
    return None


def pio_chip(env_cfg):
    """Chip ESP de um env do PlatformIO (ou None se não for ESP)."""
    bj = find_board_json(env_cfg.get("board", "")) or {}
    mcu = bj.get("build", {}).get("mcu", "").lower()
    if mcu.startswith("esp"):
        return mcu
    plat = env_cfg.get("platform", "")
    if "8266" in plat:
        return "esp8266"
    if "espressif32" in plat:
        return "esp32"
    return None


def parse_size(text):
    """Aceita 0x290000, 1441792, 1408K, 1.5M."""
    t = str(text).strip().upper()
    if not t:
        raise ValueError("vazio")
    mult = 1
    if t.endswith("K"):
        mult, t = 1024, t[:-1]
    elif t.endswith("M"):
        mult, t = 1024 * 1024, t[:-1]
    if t.startswith("0X"):
        return int(t, 16) * mult
    return int(float(t) * mult)


FS_SUBTYPES = {0x81: "fat", 0x82: "spiffs", 0x83: "littlefs"}


def read_partitions(path):
    """Lê partitions.bin ou .csv. Retorna [(nome, tipo, subtipo, offset, tamanho)]."""
    parts = []
    if path.lower().endswith(".bin"):
        with open(path, "rb") as f:
            data = f.read()
        for i in range(0, len(data) - 31, 32):
            magic, ptype, sub, off, size, label, _ = struct.unpack_from("<HBBII16sI", data, i)
            if magic != 0x50AA:
                break
            name = label.split(b"\0")[0].decode("ascii", "replace")
            t = {0: "app", 1: "data"}.get(ptype, str(ptype))
            st = FS_SUBTYPES.get(sub, str(sub)) if ptype == 1 else str(sub)
            parts.append((name, t, st, off, size))
        return parts

    # CSV no formato do ESP-IDF; offsets vazios são calculados automaticamente
    nxt = 0x9000
    with open(path, encoding="utf-8", errors="ignore") as f:
        for raw in f:
            line = raw.split("#")[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) < 5:
                continue
            name, t, st, off, size = cols[:5]
            t, st = t.lower(), st.lower()
            align = 0x10000 if t in ("app", "0") else 0x1000
            off = parse_size(off) if off else (nxt + align - 1) // align * align
            size = parse_size(size)
            parts.append((name, t, st, off, size))
            nxt = off + size
    return parts


def fs_partition(path):
    """(offset, tamanho, subtipo) da última partição de sistema de arquivos."""
    fs = None
    for name, t, st, off, size in read_partitions(path):
        if t in ("data", "1") and st in ("spiffs", "littlefs", "fat"):
            fs = (off, size, st)
    return fs


def esp_files_from_build(build_dir, chip):
    """Lista [(endereço, arquivo)] para gravar um build ESP do PlatformIO."""
    fw = os.path.join(build_dir, "firmware.bin")
    if chip == "esp8266":
        return [("0x0", fw)]
    files = []
    boot = os.path.join(build_dir, "bootloader.bin")
    part = os.path.join(build_dir, "partitions.bin")
    if os.path.isfile(boot):
        files.append((ESP_BOOTLOADER_ADDR.get(chip, "0x0"), boot))
    if os.path.isfile(part):
        files.append(("0x8000", part))
        files.append(("0xe000", BOOT_APP0))
    files.append(("0x10000", fw))
    return files


# --------------------------------------------------------------------------
# Interface
# --------------------------------------------------------------------------

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_NAME)
        self.geometry("900x820")
        self.minsize(720, 640)

        self.proc = None
        self.log_queue = queue.Queue()
        self.pio_project = None   # pasta do projeto PlatformIO (modo projeto)
        self.pio_envs = {}
        self.pio_opts = {}
        self._fs_family = None    # "esp32" / "esp8266": layouts mostrados no momento
        self.cancelled = False
        self.missing_headers = []
        self.lib_win = None

        self.var_mode = tk.StringVar(value="file")
        self.var_file = tk.StringVar()
        self.var_ino = tk.StringVar()
        self.var_project = tk.StringVar()
        self.var_env = tk.StringVar()
        self.var_board = tk.StringVar(value="Arduino Uno")
        self.var_port = tk.StringVar()
        self.var_baud = tk.StringVar()
        self.var_addr = tk.StringVar(value="0x10000")
        self.var_erase = tk.BooleanVar(value=False)
        self.var_status = tk.StringVar(value="Pronto")
        self.var_fs_type = tk.StringVar(value="littlefs")
        self.var_fs_src = tk.StringVar()
        self.var_fs_layout = tk.StringVar()
        self.var_fs_offset = tk.StringVar()
        self.var_fs_size = tk.StringVar()
        self.var_fs_block = tk.StringVar()

        self._build_ui()
        saved = self._load_config()
        self.refresh_ports()
        self._on_board()
        self._on_mode()
        # Restaura os campos de FS salvos (os eventos acima preenchem valores padrão)
        if self._manual_board():
            for key, var in (("fs_type", self.var_fs_type), ("fs_src", self.var_fs_src),
                             ("fs_layout", self.var_fs_layout), ("fs_offset", self.var_fs_offset),
                             ("fs_size", self.var_fs_size), ("fs_block", self.var_fs_block)):
                if saved.get(key):
                    var.set(saved[key])
        self.after(100, self._drain_log)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------------- layout ----------------
    def _build_ui(self):
        pad = dict(padx=6, pady=4)
        main = ttk.Frame(self, padding=10)
        main.pack(fill="both", expand=True)
        main.columnconfigure(1, weight=1)

        # Origem do firmware
        src = ttk.LabelFrame(main, text="Firmware", padding=8)
        src.grid(row=0, column=0, columnspan=3, sticky="ew")
        src.columnconfigure(1, weight=1)

        ttk.Radiobutton(src, text="Arquivo (.hex / .bin)", variable=self.var_mode,
                        value="file", command=self._on_mode).grid(row=0, column=0, sticky="w", **pad)
        self.ent_file = ttk.Entry(src, textvariable=self.var_file)
        self.ent_file.grid(row=0, column=1, sticky="ew", **pad)
        self.btn_file = ttk.Button(src, text="Procurar...", command=self.pick_file)
        self.btn_file.grid(row=0, column=2, sticky="w", **pad)

        ttk.Radiobutton(src, text="Sketch Arduino (.ino)", variable=self.var_mode,
                        value="ino", command=self._on_mode).grid(row=1, column=0, sticky="w", **pad)
        self.ent_ino = ttk.Entry(src, textvariable=self.var_ino)
        self.ent_ino.grid(row=1, column=1, sticky="ew", **pad)
        ino_btns = ttk.Frame(src)
        ino_btns.grid(row=1, column=2, sticky="w")
        self.btn_ino = ttk.Button(ino_btns, text="Procurar...", command=self.pick_ino)
        self.btn_ino.pack(side="left", padx=6, pady=4)
        self.btn_libs = ttk.Button(ino_btns, text="Bibliotecas...", command=self.open_libraries)
        self.btn_libs.pack(side="left", padx=(0, 6))

        ttk.Radiobutton(src, text="Projeto PlatformIO", variable=self.var_mode,
                        value="pio", command=self._on_mode).grid(row=2, column=0, sticky="w", **pad)
        self.ent_project = ttk.Entry(src, textvariable=self.var_project)
        self.ent_project.grid(row=2, column=1, sticky="ew", **pad)
        self.btn_project = ttk.Button(src, text="Procurar...", command=self.pick_project)
        self.btn_project.grid(row=2, column=2, sticky="w", **pad)

        ttk.Label(src, text="Ambiente (env):").grid(row=3, column=0, sticky="e", **pad)
        self.cmb_env = ttk.Combobox(src, textvariable=self.var_env, state="readonly")
        self.cmb_env.grid(row=3, column=1, sticky="ew", **pad)
        self.cmb_env.bind("<<ComboboxSelected>>", lambda e: self._on_env())

        # Placa e porta
        tgt = ttk.LabelFrame(main, text="Placa e conexão", padding=8)
        tgt.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        tgt.columnconfigure(1, weight=1)

        ttk.Label(tgt, text="Placa:").grid(row=0, column=0, sticky="e", **pad)
        self.cmb_board = ttk.Combobox(tgt, textvariable=self.var_board,
                                      values=list(BOARDS), state="readonly")
        self.cmb_board.grid(row=0, column=1, sticky="ew", **pad)
        self.cmb_board.bind("<<ComboboxSelected>>", lambda e: self._on_board())

        ttk.Label(tgt, text="Porta:").grid(row=1, column=0, sticky="e", **pad)
        self.cmb_port = ttk.Combobox(tgt, textvariable=self.var_port)
        self.cmb_port.grid(row=1, column=1, sticky="ew", **pad)
        ttk.Button(tgt, text="Atualizar", command=self.refresh_ports).grid(row=1, column=2, **pad)

        ttk.Label(tgt, text="Velocidade:").grid(row=2, column=0, sticky="e", **pad)
        self.cmb_baud = ttk.Combobox(tgt, textvariable=self.var_baud, width=12)
        self.cmb_baud.grid(row=2, column=1, sticky="w", **pad)

        self.lbl_addr = ttk.Label(tgt, text="Endereço (.bin):")
        self.lbl_addr.grid(row=3, column=0, sticky="e", **pad)
        addr_row = ttk.Frame(tgt)
        addr_row.grid(row=3, column=1, sticky="w")
        self.cmb_addr = ttk.Combobox(addr_row, textvariable=self.var_addr, width=12,
                                     values=["0x10000", "0x0", "0x1000"])
        self.cmb_addr.pack(side="left", padx=6)
        self.lbl_addr_hint = ttk.Label(addr_row, foreground="#666",
                                       text="0x10000 = só a aplicação  |  0x0 = .bin mesclado / ESP8266")
        self.lbl_addr_hint.pack(side="left")
        self.chk_erase = ttk.Checkbutton(tgt, text="Apagar toda a flash antes (ESP)",
                                         variable=self.var_erase)
        self.chk_erase.grid(row=4, column=1, sticky="w", **pad)

        # Sistema de arquivos
        fsf = ttk.LabelFrame(main, text="Sistema de arquivos (LittleFS / SPIFFS) - somente ESP", padding=8)
        fsf.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        fsf.columnconfigure(1, weight=1)

        ttk.Label(fsf, text="Tipo:").grid(row=0, column=0, sticky="e", **pad)
        row0 = ttk.Frame(fsf)
        row0.grid(row=0, column=1, columnspan=3, sticky="ew")
        row0.columnconfigure(3, weight=1)
        cmb_type = ttk.Combobox(row0, textvariable=self.var_fs_type, values=["littlefs", "spiffs"],
                                state="readonly", width=10)
        cmb_type.grid(row=0, column=0, padx=6, pady=4)
        ttk.Label(row0, text="Layout:").grid(row=0, column=2, padx=(12, 0))
        self.cmb_layout = ttk.Combobox(row0, textvariable=self.var_fs_layout, state="readonly")
        self.cmb_layout.grid(row=0, column=3, sticky="ew", padx=6)
        self.cmb_layout.bind("<<ComboboxSelected>>", lambda e: self._on_layout())
        btn_part = ttk.Button(row0, text="Ler partições...", command=self.pick_partitions)
        btn_part.grid(row=0, column=4, padx=6)

        ttk.Label(fsf, text="Origem:").grid(row=1, column=0, sticky="e", **pad)
        ent_src = ttk.Entry(fsf, textvariable=self.var_fs_src)
        ent_src.grid(row=1, column=1, sticky="ew", **pad)
        btn_dir = ttk.Button(fsf, text="Pasta data...", command=self.pick_fs_dir)
        btn_dir.grid(row=1, column=2, **pad)
        btn_img = ttk.Button(fsf, text="Imagem .bin...", command=self.pick_fs_image)
        btn_img.grid(row=1, column=3, **pad)

        ttk.Label(fsf, text="Offset:").grid(row=2, column=0, sticky="e", **pad)
        row2 = ttk.Frame(fsf)
        row2.grid(row=2, column=1, columnspan=3, sticky="w")
        ent_off = ttk.Entry(row2, textvariable=self.var_fs_offset, width=12)
        ent_off.pack(side="left", padx=6)
        ttk.Label(row2, text="Tamanho:").pack(side="left", padx=(12, 0))
        ent_size = ttk.Entry(row2, textvariable=self.var_fs_size, width=12)
        ent_size.pack(side="left", padx=6)
        ttk.Label(row2, text="Bloco:").pack(side="left", padx=(12, 0))
        ent_block = ttk.Entry(row2, textvariable=self.var_fs_block, width=8)
        ent_block.pack(side="left", padx=6)
        ttk.Label(fsf, foreground="#666",
                  text="Use o mesmo layout/partições com que o firmware foi compilado, senão o "
                       "ESP não encontra os arquivos.").grid(row=3, column=1, columnspan=3, sticky="w", padx=6)
        self.fs_widgets = [cmb_type, self.cmb_layout, btn_part, ent_src, btn_dir, btn_img,
                           ent_off, ent_size, ent_block]

        # Botões
        btns = ttk.Frame(main)
        btns.grid(row=3, column=0, columnspan=3, sticky="ew", pady=8)
        self.btn_compile = ttk.Button(btns, text="✔  Compilar", command=self.start_compile)
        self.btn_compile.pack(side="left", padx=(0, 6))
        self.btn_upload = ttk.Button(btns, text="⬆  Gravar firmware", command=self.start_upload)
        self.btn_upload.pack(side="left")
        self.btn_upload_fs = ttk.Button(btns, text="⬆  Gravar FS", command=self.start_upload_fs)
        self.btn_upload_fs.pack(side="left", padx=(6, 0))
        self.btn_erase_fs = ttk.Button(btns, text="🗑  Apagar FS", command=self.start_erase_fs)
        self.btn_erase_fs.pack(side="left", padx=(6, 0))
        self.btn_stop = ttk.Button(btns, text="Cancelar", command=self.stop, state="disabled")
        self.btn_stop.pack(side="left", padx=6)
        ttk.Button(btns, text="Limpar log", command=lambda: self.txt.delete("1.0", "end")).pack(side="right")
        self.progress = ttk.Progressbar(btns, mode="determinate", maximum=100)
        self.progress.pack(side="left", fill="x", expand=True, padx=10)

        # Log
        logf = ttk.Frame(main)
        logf.grid(row=4, column=0, columnspan=3, sticky="nsew")
        main.rowconfigure(4, weight=1)
        self.txt = tk.Text(logf, bg="#1e1e1e", fg="#d4d4d4", insertbackground="white",
                           font=("Consolas", 9), wrap="word", height=10)
        sb = ttk.Scrollbar(logf, command=self.txt.yview)
        self.txt.configure(yscrollcommand=sb.set)
        self.txt.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        self.txt.tag_config("ok", foreground="#6a9955")
        self.txt.tag_config("err", foreground="#f44747")
        self.txt.tag_config("info", foreground="#569cd6")

        ttk.Label(main, textvariable=self.var_status, anchor="w").grid(
            row=5, column=0, columnspan=3, sticky="ew", pady=(4, 0))

    # ---------------- eventos ----------------
    def _manual_board(self):
        """True quando a placa vem da lista (modos arquivo e sketch)."""
        return self.var_mode.get() != "pio"

    def _on_mode(self):
        mode = self.var_mode.get()
        for widgets, active in (((self.ent_file, self.btn_file), mode == "file"),
                                ((self.ent_ino, self.btn_ino, self.btn_libs), mode == "ino"),
                                ((self.ent_project, self.btn_project), mode == "pio")):
            for w in widgets:
                w.configure(state="normal" if active else "disabled")
        self.cmb_env.configure(state="readonly" if mode == "pio" else "disabled")
        self.cmb_board.configure(state="disabled" if mode == "pio" else "readonly")
        if mode == "pio" and self.var_project.get():
            self.load_project(self.var_project.get(), quiet=True)
        elif mode != "pio":
            self._on_board()
        self._update_addr_state()

    def _on_board(self):
        cfg = BOARDS.get(self.var_board.get())
        if not cfg:
            return
        self.cmb_baud["values"] = ESP_BAUDS if cfg["kind"] == "esp" else AVR_BAUDS
        self.var_baud.set(str(cfg["baud"]))
        if cfg["kind"] == "esp":
            self.var_addr.set("0x0" if cfg["chip"] == "esp8266" else "0x10000")
        if self._manual_board():
            self._set_fs_family(cfg.get("chip"))
        self._update_addr_state()

    def _on_env(self):
        cfg = self._pio_board_cfg()
        if cfg:
            self.cmb_baud["values"] = ESP_BAUDS if cfg["kind"] == "esp" else AVR_BAUDS
            self.var_baud.set(str(cfg["baud"]))
            desc = cfg.get("chip") or cfg.get("mcu")
            self.var_status.set(f"Env '{self.var_env.get()}': {desc} ({cfg['kind'].upper()})")
            port = self.pio_envs.get(self.var_env.get(), {}).get("upload_port")
            if port:
                self.var_port.set(port)
        else:
            self.var_status.set("Firmware não encontrado nesse env — compile no PlatformIO primeiro.")
        self._fill_fs_from_project()
        self._update_addr_state()

    def _update_addr_state(self):
        file_mode = self.var_mode.get() == "file"
        manual = self._manual_board()
        is_bin = self.var_file.get().lower().endswith(".bin")
        cfg = BOARDS.get(self.var_board.get(), {}) if manual else (self._pio_board_cfg() or {})
        is_esp = cfg.get("kind") == "esp"
        self.cmb_addr.configure(state="normal" if (file_mode and is_bin) else "disabled")
        self.chk_erase.configure(state="normal" if is_esp else "disabled")
        busy = self.proc is not None or self.btn_stop["state"] == "normal"
        self.btn_compile.configure(
            state="normal" if (self.var_mode.get() == "ino" and not busy) else "disabled")
        if not manual and not is_esp:
            is_esp = self._pio_chip() is not None
        for w in self.fs_widgets:
            ro = isinstance(w, ttk.Combobox)
            w.configure(state=("readonly" if ro else "normal") if is_esp else "disabled")
        fs_state = "normal" if (is_esp and not busy) else "disabled"
        self.btn_upload_fs.configure(state=fs_state)
        self.btn_erase_fs.configure(state=fs_state)

    # ---------------- sistema de arquivos ----------------
    def _set_fs_family(self, chip):
        """Mostra os layouts do ESP8266 ou do ESP32 conforme o chip escolhido."""
        family = None if not chip else ("esp8266" if chip == "esp8266" else "esp32")
        if family == self._fs_family:
            return
        self._fs_family = family
        layouts = ESP8266_LAYOUTS if family == "esp8266" else ESP32_LAYOUTS if family else {}
        self.cmb_layout["values"] = list(layouts)
        if family:
            self.var_fs_layout.set(DEFAULT_LAYOUT[family])
            self._on_layout()

    def _on_layout(self):
        layouts = ESP8266_LAYOUTS if self._fs_family == "esp8266" else ESP32_LAYOUTS
        val = layouts.get(self.var_fs_layout.get())
        if val:
            off, size, block = val
            self.var_fs_offset.set(hex(off))
            self.var_fs_size.set(hex(size))
            self.var_fs_block.set(hex(block))

    def _apply_partitions(self, path, label):
        fs = fs_partition(path)
        if not fs:
            return False
        off, size, sub = fs
        if sub == "fat":
            self.log(f"Aviso: a partição de FS em {os.path.basename(path)} é FAT (não suportada).\n", "err")
            return False
        self.var_fs_offset.set(hex(off))
        self.var_fs_size.set(hex(size))
        self.var_fs_block.set("0x1000")
        self.var_fs_layout.set(label)
        return True

    def _pio_chip(self):
        if not self.pio_project or not self.var_env.get():
            return None
        return pio_chip(self.pio_envs.get(self.var_env.get(), {}))

    def _fill_fs_from_project(self):
        """Preenche tipo, pasta data, offset e tamanho a partir do projeto PlatformIO."""
        chip = self._pio_chip()
        self._set_fs_family(chip)
        if not chip:
            return
        env = self.var_env.get()
        env_cfg = self.pio_envs.get(env, {})
        bj = find_board_json(env_cfg.get("board", "")) or {}
        build = bj.get("build", {})

        fs = env_cfg.get("board_build.filesystem") or build.get("filesystem") or "spiffs"
        if fs in ("littlefs", "spiffs"):
            self.var_fs_type.set(fs)

        data_dir = self.pio_opts.get("data_dir", "data").replace("${PROJECT_DIR}", self.pio_project)
        if not os.path.isabs(data_dir):
            data_dir = os.path.join(self.pio_project, data_dir)
        self.var_fs_src.set(os.path.normpath(data_dir))

        build_dir = os.path.join(self.pio_project, ".pio", "build", env)
        if chip == "esp8266":
            ld = env_cfg.get("board_build.ldscript") or build.get("arduino", {}).get("ldscript", "")
            ld = os.path.basename(ld)
            if ld in ESP8266_LD:
                self.var_fs_layout.set(_esp8266_layout_name(ld))
                self._on_layout()
        else:
            part = os.path.join(build_dir, "partitions.bin")
            if not (os.path.isfile(part) and self._apply_partitions(part, LAYOUT_FROM_PROJECT)):
                csv = env_cfg.get("board_build.partitions", "")
                if csv and os.path.isfile(os.path.join(self.pio_project, csv)):
                    self._apply_partitions(os.path.join(self.pio_project, csv), LAYOUT_FROM_PROJECT)

    def pick_partitions(self):
        path = filedialog.askopenfilename(
            title="Tabela de partições (ESP32)",
            filetypes=[("Partições", "*.csv *.bin"), ("Todos", "*.*")])
        if not path:
            return
        try:
            ok = self._apply_partitions(path, LAYOUT_FROM_FILE)
        except (OSError, ValueError, struct.error) as e:
            messagebox.showerror(APP_NAME, f"Não consegui ler o arquivo:\n{e}")
            return
        if not ok:
            messagebox.showerror(APP_NAME, "Nenhuma partição SPIFFS/LittleFS nesse arquivo.")

    def pick_fs_dir(self):
        path = filedialog.askdirectory(title="Pasta com os arquivos (normalmente 'data')")
        if path:
            self.var_fs_src.set(os.path.normpath(path))

    def pick_fs_image(self):
        path = filedialog.askopenfilename(
            title="Imagem do sistema de arquivos",
            filetypes=[("Imagem", "*.bin"), ("Todos", "*.*")])
        if path:
            self.var_fs_src.set(os.path.normpath(path))

    def pick_file(self):
        path = filedialog.askopenfilename(
            title="Escolha o firmware",
            filetypes=[("Firmware", "*.hex *.bin"), ("Intel HEX (Arduino)", "*.hex"),
                       ("Binário (ESP)", "*.bin"), ("Todos", "*.*")])
        if not path:
            return
        self.var_file.set(path)
        # Sugere uma placa compatível com a extensão
        cur = BOARDS.get(self.var_board.get(), {})
        if path.lower().endswith(".hex") and cur.get("kind") != "avr":
            self.var_board.set("Arduino Uno")
            self._on_board()
        elif path.lower().endswith(".bin") and cur.get("kind") != "esp":
            self.var_board.set("ESP32")
            self._on_board()
        self._update_addr_state()

    def pick_ino(self):
        path = filedialog.askopenfilename(
            title="Escolha o sketch", filetypes=[("Sketch Arduino", "*.ino"), ("Todos", "*.*")])
        if path:
            self.var_ino.set(os.path.normpath(path))

    def open_libraries(self):
        if self.lib_win and self.lib_win.winfo_exists():
            self.lib_win.lift()
            return
        self.lib_win = LibraryDialog(self)

    def pick_project(self):
        path = filedialog.askdirectory(title="Pasta do projeto PlatformIO (onde está o platformio.ini)")
        if path:
            self.var_project.set(path)
            self.load_project(path)

    def load_project(self, path, quiet=False):
        envs, opts = read_platformio_ini(path)
        if not envs:
            if not quiet:
                messagebox.showerror(APP_NAME, "Não encontrei platformio.ini com [env:...] nessa pasta.")
            return
        self.pio_project, self.pio_envs, self.pio_opts = path, envs, opts
        names = list(envs)
        self.cmb_env["values"] = names
        if self.var_env.get() not in names:
            self.var_env.set(names[0])
        self._on_env()

    def _pio_board_cfg(self):
        if not self.pio_project or not self.var_env.get():
            return None
        env = self.var_env.get()
        build_dir = os.path.join(self.pio_project, ".pio", "build", env)
        return board_from_pio(self.pio_envs.get(env, {}), build_dir)

    def refresh_ports(self):
        ports = sorted(serial.tools.list_ports.comports(), key=lambda p: p.device)
        items = [f"{p.device} - {p.description}" for p in ports]
        self.cmb_port["values"] = items
        cur = self.var_port.get().split(" ")[0]
        match = [i for i in items if i.split(" ")[0] == cur]
        if match:
            self.var_port.set(match[0])
        elif items:
            self.var_port.set(items[0])
        else:
            self.var_port.set("")
        self.var_status.set(f"{len(items)} porta(s) encontrada(s)")

    # ---------------- log ----------------
    def log(self, text, tag=None):
        self.log_queue.put((text, tag))

    def _drain_log(self):
        try:
            while True:
                text, tag = self.log_queue.get_nowait()
                if tag == "progress":
                    self.progress["value"] = text
                    continue
                if tag == "done":
                    self._finish(text)
                    continue
                if tag == "fs_fields":
                    self.var_fs_offset.set(hex(text[0]))
                    self.var_fs_size.set(hex(text[1]))
                    self.var_fs_layout.set("(do sketch compilado)")
                    continue
                self.txt.insert("end", text, tag)
                self.txt.see("end")
        except queue.Empty:
            pass
        self.after(50, self._drain_log)

    # ---------------- gravação ----------------
    def _port_and_baud(self):
        port = self.var_port.get().split(" ")[0].strip()
        if not port:
            raise ValueError("Escolha a porta serial.")
        try:
            baud = int(self.var_baud.get())
        except ValueError:
            raise ValueError("Velocidade inválida.")
        return port, baud

    def _build_job(self):
        """Valida a tela e retorna (cfg, arquivos) ou lança ValueError."""
        if self.var_mode.get() == "ino":
            raise ValueError("internal: sketch usa _build_ino_job")
        if self.var_mode.get() == "file":
            path = self.var_file.get().strip()
            if not os.path.isfile(path):
                raise ValueError("Escolha um arquivo de firmware válido.")
            cfg = dict(BOARDS[self.var_board.get()])
            if cfg["kind"] == "avr":
                if not path.lower().endswith(".hex"):
                    raise ValueError("Para Arduino (AVR) o firmware deve ser .hex.")
                files = [(None, path)]
                cfg["hex"] = path
            else:
                if not path.lower().endswith(".bin"):
                    raise ValueError("Para ESP o firmware deve ser .bin.")
                addr = self.var_addr.get().strip() or "0x10000"
                files = [(addr, path)]
                # Se o .bin do PlatformIO estiver na pasta de build, grava tudo junto
                folder = os.path.dirname(path)
                if (addr.lower() == "0x10000" and os.path.basename(path) == "firmware.bin"
                        and os.path.isfile(os.path.join(folder, "bootloader.bin"))):
                    files = esp_files_from_build(folder, cfg["chip"])
        else:
            if not self.pio_project:
                raise ValueError("Escolha a pasta do projeto PlatformIO.")
            env = self.var_env.get()
            build_dir = os.path.join(self.pio_project, ".pio", "build", env)
            cfg = self._pio_board_cfg()
            if not cfg:
                raise ValueError(f"Nenhum firmware em {build_dir}.\nCompile o env '{env}' no PlatformIO primeiro.")
            if cfg["kind"] == "avr":
                files = [(None, os.path.join(build_dir, "firmware.hex"))]
                cfg["hex"] = files[0][1]
            else:
                files = esp_files_from_build(build_dir, cfg["chip"])

        cfg["port"], cfg["baud"] = self._port_and_baud()
        cfg["erase"] = self.var_erase.get()
        return cfg, files

    def _esptool_write(self, chip, port, baud, files, erase=False):
        cmd = self._self_cmd() + ["--chip", chip, "--port", port, "--baud", str(baud),
                                  "--before", "default-reset", "--after", "hard-reset",
                                  "write-flash", "-z"]
        if erase:
            cmd.append("--erase-all")
        for addr, path in files:
            cmd += [addr, path]
        return cmd

    def _fs_target(self):
        """Valida chip, porta e partição de FS. Retorna (chip, porta, baud, offset, tamanho, bloco)."""
        if self._manual_board():
            chip = BOARDS[self.var_board.get()].get("chip")
        else:
            chip = self._pio_chip()
        if not chip:
            raise ValueError("Sistema de arquivos só é suportado em ESP8266/ESP32.")
        port, baud = self._port_and_baud()
        try:
            offset = parse_size(self.var_fs_offset.get())
            size = parse_size(self.var_fs_size.get())
            block = parse_size(self.var_fs_block.get())
        except ValueError:
            raise ValueError("Offset, tamanho e bloco precisam ser números (ex.: 0x290000).")
        if offset % 0x1000 or size <= 0 or block <= 0:
            raise ValueError("Offset deve ser múltiplo de 0x1000; tamanho e bloco maiores que zero.")
        return chip, port, baud, offset, size, block

    def _build_fs_erase_job(self):
        """Passos para apagar a partição de FS (deixa a área em branco: 0xFF)."""
        chip, port, baud, offset, size, _ = self._fs_target()
        if size % 0x1000:
            raise ValueError("Para apagar, o tamanho deve ser múltiplo de 0x1000 (4 KB).")
        cmd = self._self_cmd() + ["--chip", chip, "--port", port, "--baud", str(baud),
                                  "--before", "default-reset", "--after", "hard-reset",
                                  "erase-region", hex(offset), hex(size)]
        return [(f"Apagando sistema de arquivos em {hex(offset)} ({size // 1024} KB)", cmd)], offset, size

    def _build_fs_job(self):
        """Valida a seção de FS e retorna (passos, pasta temporária ou None)."""
        chip, port, baud, offset, size, block = self._fs_target()
        src = self.var_fs_src.get().strip()

        steps, tmp = [], None
        if os.path.isdir(src):
            if not any(os.scandir(src)):
                raise ValueError(f"A pasta {src} está vazia.")
            fs = self.var_fs_type.get()
            if fs == "littlefs":
                tool = os.path.join(FS_TOOLS, "mklittlefs.exe")
            elif chip == "esp8266":
                tool = os.path.join(FS_TOOLS, "mkspiffs_espressif8266_arduino.exe")
            else:
                tool = os.path.join(FS_TOOLS, "mkspiffs_espressif32_arduino.exe")
            tmp = tempfile.mkdtemp(prefix="gravador_fs_")
            image = os.path.join(tmp, f"{fs}.bin")
            steps.append((f"Gerando imagem {fs.upper()} a partir de {src}",
                          [tool, "-c", src, "-p", "256", "-b", str(block), "-s", str(size), image]))
        elif os.path.isfile(src) and src.lower().endswith(".bin"):
            image = src
            if os.path.getsize(src) > size:
                raise ValueError(f"A imagem ({os.path.getsize(src)} bytes) é maior que a partição "
                                 f"({size} bytes).")
        else:
            raise ValueError("Escolha a pasta 'data' ou uma imagem .bin do sistema de arquivos.")

        steps.append((f"Gravando sistema de arquivos em {hex(offset)} ({size // 1024} KB)",
                      self._esptool_write(chip, port, baud, [(hex(offset), image)])))
        return steps, tmp

    # ---------------- sketch .ino (arduino-cli) ----------------
    def _cli(self, *args):
        return [ARDUINO_CLI, *args, "--no-color", "--additional-urls", BOARD_URLS]

    def _build_ino_job(self, upload):
        """Passos para compilar (e opcionalmente gravar) o sketch. Retorna (passos, tmp)."""
        ino = self.var_ino.get().strip()
        if not (os.path.isfile(ino) and ino.lower().endswith(".ino")):
            raise ValueError("Escolha um arquivo .ino válido.")
        if not os.path.isfile(ARDUINO_CLI):
            raise ValueError(f"arduino-cli não encontrado em {ARDUINO_CLI}")
        board = BOARDS[self.var_board.get()]
        cfg = dict(board)
        if upload:
            cfg["port"], cfg["baud"] = self._port_and_baud()
            cfg["erase"] = self.var_erase.get()

        # FQBN: no ESP inclui a tabela de partições do layout de FS escolhido
        fqbn = board["fqbn"]
        fs_expected = None
        if board["kind"] == "esp":
            opt = fqbn_fs_option(board["chip"], self.var_fs_layout.get())
            if opt:
                fqbn += ":" + opt
            try:
                fs_expected = (parse_size(self.var_fs_offset.get()), parse_size(self.var_fs_size.get()))
            except ValueError:
                pass

        # O arduino-cli exige que a pasta tenha o mesmo nome do .ino
        sketch_dir, name = os.path.dirname(ino), os.path.splitext(os.path.basename(ino))[0]
        tmp = None
        if os.path.basename(sketch_dir) != name:
            tmp = tempfile.mkdtemp(prefix="gravador_ino_")
            dst = os.path.join(tmp, name)
            os.makedirs(dst)
            for f in os.listdir(sketch_dir):
                src = os.path.join(sketch_dir, f)
                ext = os.path.splitext(f)[1].lower()
                if os.path.isfile(src) and ext in (".ino", ".h", ".hpp", ".c", ".cpp", ".s"):
                    if ext == ".ino" and f != os.path.basename(ino):
                        continue  # outros .ino da pasta seriam outras abas/sketches
                    shutil.copy2(src, dst)
            if os.path.isdir(os.path.join(sketch_dir, "src")):
                shutil.copytree(os.path.join(sketch_dir, "src"), os.path.join(dst, "src"))
            sketch_dir = dst

        key = hashlib.md5((os.path.abspath(ino) + "|" + fqbn).encode()).hexdigest()[:12]
        build_path = os.path.join(BUILD_CACHE, f"{name}_{key}")
        core = ":".join(fqbn.split(":")[:2])

        steps = [
            (f"Verificando core {core}", lambda: self._ensure_core(core)),
            (f"Compilando {name}.ino para {fqbn}",
             self._cli("compile", "--fqbn", fqbn, "--build-path", build_path, sketch_dir)),
        ]
        if board["kind"] == "esp" and fs_expected:
            steps.append(("Conferindo partições", lambda: self._check_partitions(build_path, name, fs_expected)))
        if upload:
            steps.append(("Preparando gravação", lambda: self._ino_upload_steps(cfg, build_path, name)))
        return steps, tmp

    def _ensure_core(self, core):
        """Instala o core da placa se ainda não estiver instalado."""
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        r = subprocess.run(self._cli("core", "list", "--format", "json"), capture_output=True,
                           creationflags=flags)
        installed = set()
        try:
            for plat in json.loads(r.stdout.decode("utf-8", "replace") or "{}").get("platforms", []):
                if plat.get("installed_version"):
                    installed.add(plat.get("id"))
        except ValueError:
            pass
        if core in installed:
            self.log(f"Core {core} já instalado.\n\n")
            return []
        self.log(f"Core {core} não instalado; será baixado (pode levar vários minutos).\n\n", "info")
        return [("Atualizando índice de placas", self._cli("core", "update-index")),
                (f"Instalando core {core}", self._cli("core", "install", core))]

    def _check_partitions(self, build_path, name, expected):
        part = os.path.join(build_path, f"{name}.ino.partitions.bin")
        if os.path.isfile(part):
            fs = fs_partition(part)
            if fs and (fs[0], fs[1]) != expected:
                self.log(f"Aviso: o firmware compilado tem o FS em {hex(fs[0])} ({fs[1] // 1024} KB), "
                         f"diferente do configurado na seção de FS.\n", "err")
                self.log((fs[0], fs[1]), "fs_fields")
        return []

    def _ino_upload_steps(self, cfg, build_path, name):
        base = os.path.join(build_path, f"{name}.ino")
        if cfg["kind"] == "avr":
            cfg["hex"] = base + ".hex"
            return [("Gravando firmware (avrdude)", cfg)]
        chip = cfg["chip"]
        if chip == "esp8266":
            files = [("0x0", base + ".bin")]
        else:
            files = []
            if os.path.isfile(base + ".bootloader.bin"):
                files.append((ESP_BOOTLOADER_ADDR.get(chip, "0x0"), base + ".bootloader.bin"))
            if os.path.isfile(base + ".partitions.bin"):
                files += [("0x8000", base + ".partitions.bin"), ("0xe000", BOOT_APP0)]
            files.append(("0x10000", base + ".bin"))
        return [("Gravando firmware (esptool)",
                 self._esptool_write(chip, cfg["port"], cfg["baud"], files, cfg["erase"]))]

    def start_compile(self):
        if self.proc:
            return
        try:
            steps, tmp = self._build_ino_job(upload=False)
        except (ValueError, OSError) as e:
            messagebox.showerror(APP_NAME, str(e))
            return
        self._start(steps, tmp)

    def run_cli_steps(self, steps):
        """Usado pelo diálogo de bibliotecas para rodar comandos com log."""
        if self.btn_stop["state"] == "normal":
            messagebox.showinfo(APP_NAME, "Aguarde a operação atual terminar.")
            return False
        self._start(steps, None)
        return True

    def start_upload(self):
        if self.proc:
            return
        if self.var_mode.get() == "ino":
            try:
                steps, tmp = self._build_ino_job(upload=True)
            except (ValueError, OSError) as e:
                messagebox.showerror(APP_NAME, str(e))
                return
            self._start(steps, tmp)
            return
        try:
            cfg, files = self._build_job()
        except ValueError as e:
            messagebox.showerror(APP_NAME, str(e))
            return
        if cfg["kind"] == "avr":
            # O comando do avrdude é montado no worker: o reset 1200 bps pode trocar a porta
            steps = [("Gravando firmware (avrdude)", cfg)]
        else:
            steps = [("Gravando firmware (esptool)",
                      self._esptool_write(cfg["chip"], cfg["port"], cfg["baud"], files, cfg["erase"]))]
        self._start(steps, None)

    def start_upload_fs(self):
        if self.proc:
            return
        try:
            steps, tmp = self._build_fs_job()
        except (ValueError, OSError) as e:
            messagebox.showerror(APP_NAME, str(e))
            return
        self._start(steps, tmp)

    def start_erase_fs(self):
        if self.proc:
            return
        try:
            steps, offset, size = self._build_fs_erase_job()
        except ValueError as e:
            messagebox.showerror(APP_NAME, str(e))
            return
        if not messagebox.askyesno(
                APP_NAME, f"Apagar o sistema de arquivos do ESP?\n\n"
                          f"Região: {hex(offset)} a {hex(offset + size)} ({size // 1024} KB)\n"
                          f"Todos os arquivos dessa área serão perdidos. O firmware não é afetado.",
                icon="warning"):
            return
        self._start(steps, None)

    def _start(self, steps, tmp):
        self._save_config()
        for b in (self.btn_compile, self.btn_upload, self.btn_upload_fs, self.btn_erase_fs):
            b.configure(state="disabled")
        self.btn_stop.configure(state="normal")
        self.progress["value"] = 0
        self.var_status.set("Executando...")
        self.cancelled = False
        self.missing_headers = []
        threading.Thread(target=self._worker, args=(steps, tmp), daemon=True).start()

    def _avr_cmd(self, cfg):
        port = cfg["port"]
        if cfg.get("touch1200"):
            port = self._touch_1200(port)
        return [AVRDUDE, "-C", AVRDUDE_CONF, "-v", "-p", cfg["mcu"], "-c", cfg["protocol"],
                "-P", port, "-b", str(cfg["baud"]), "-D", "-U", f"flash:w:{cfg['hex']}:i"]

    def _worker(self, steps, tmp):
        start = time.time()
        ok = True
        steps = deque(steps)
        try:
            while steps:
                desc, cmd = steps.popleft()
                if callable(cmd):
                    # Passo em Python: pode gerar novos passos (ex.: instalar core, gravar)
                    steps.extendleft(reversed(cmd() or []))
                    continue
                if isinstance(cmd, dict):
                    cmd = self._avr_cmd(cmd)
                self.log(f"== {desc}\n", "info")
                self.log("> " + " ".join(f'"{c}"' if " " in c else c for c in cmd) + "\n\n", "info")
                self.log(0, "progress")
                ok = self._run(cmd) and not self.cancelled
                if not ok:
                    break
                self.log("\n")
            secs = time.time() - start
            if ok:
                self.log(f"✔ Concluído com sucesso ({secs:.1f}s)\n", "ok")
                self.log(100, "progress")
                self.log("Sucesso", "done")
            else:
                self.log(f"\n✘ Falha na operação ({secs:.1f}s)\n", "err")
                for h in dict.fromkeys(self.missing_headers):
                    self.log(f"Biblioteca faltando: {h} - clique em 'Bibliotecas...' e busque por "
                             f"'{os.path.splitext(h)[0]}'.\n", "info")
                self.log("Falhou", "done")
        except Exception as e:  # noqa: BLE001 - mostrar qualquer erro ao usuário
            self.log(f"\n✘ Erro: {e}\n", "err")
            self.log("Falhou", "done")
        finally:
            if tmp:
                shutil.rmtree(tmp, ignore_errors=True)

    def _self_cmd(self):
        """Comando para rodar o esptool embutido (funciona como .py e como .exe)."""
        if getattr(sys, "frozen", False):
            return [sys.executable, "--esptool"]
        return [sys.executable, os.path.abspath(__file__), "--esptool"]

    def _touch_1200(self, port):
        """Reset do ATmega32u4: abre a 1200 bps e espera a porta do bootloader."""
        self.log(f"Resetando {port} (toque 1200 bps)...\n", "info")
        before = {p.device for p in serial.tools.list_ports.comports()}
        try:
            s = serial.Serial(port, 1200)
            s.dtr = False
            s.close()
        except serial.SerialException as e:
            self.log(f"Aviso: {e}\n", "err")
        deadline = time.time() + 8
        time.sleep(0.5)
        while time.time() < deadline:
            now = {p.device for p in serial.tools.list_ports.comports()}
            new = now - before
            if new:
                p = sorted(new)[0]
                self.log(f"Bootloader em {p}\n", "info")
                time.sleep(0.5)
                return p
            before &= now  # porta original sumiu; a próxima que surgir é o bootloader
            time.sleep(0.2)
        self.log(f"Nova porta não apareceu; tentando em {port}\n", "info")
        return port

    def _run(self, cmd):
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        env = dict(os.environ, PYTHONUNBUFFERED="1", PYTHONIOENCODING="utf-8",
                   ARDUINO_LIBRARY_ENABLE_UNSAFE_INSTALL="true")
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     creationflags=flags, env=env)
        buf = b""
        pct_re = re.compile(rb"(\d{1,3}(?:\.\d+)?)\s*%")
        while True:
            ch = self.proc.stdout.read(1)
            if not ch:
                break
            buf += ch
            if ch not in (b"\n", b"\r"):
                continue
            line = decode(buf)
            buf = b""
            # Progresso: esptool imprime "... (45 %)"; avrdude imprime barras "####"
            m = pct_re.search(line.encode())
            if m:
                self.log(min(100, float(m.group(1))), "progress")
            if ch == b"\r" and m:
                continue  # não poluir o log com cada atualização de progresso
            mh = re.search(r"fatal error: ([\w./-]+\.h(?:pp)?): No such file", line)
            if mh:
                self.missing_headers.append(mh.group(1))
            if line.strip():
                low = line.lower()
                tag = "err" if ("error" in low or "erro" in low or "not in sync" in low
                                or "failed" in low) else None
                self.log(line.rstrip("\r\n") + "\n", tag)
        if buf:
            self.log(decode(buf) + "\n")
        code = self.proc.wait()
        self.proc = None
        return code == 0

    def stop(self):
        if self.proc:
            self.cancelled = True
            self.proc.kill()
            self.log("\nCancelado pelo usuário.\n", "err")

    def _finish(self, status):
        self.btn_upload.configure(state="normal")
        self.btn_stop.configure(state="disabled")
        self._update_addr_state()
        self.var_status.set(status)

    # ---------------- config ----------------
    def _load_config(self):
        try:
            with open(CONFIG_FILE, encoding="utf-8") as f:
                c = json.load(f)
        except (OSError, ValueError):
            return {}
        self.var_mode.set(c.get("mode", "file"))
        self.var_file.set(c.get("file", ""))
        self.var_ino.set(c.get("ino", ""))
        self.var_project.set(c.get("project", ""))
        self.var_env.set(c.get("env", ""))
        if c.get("board") in BOARDS:
            self.var_board.set(c["board"])
        self.var_port.set(c.get("port", ""))
        return c

    def _save_config(self):
        c = dict(mode=self.var_mode.get(), file=self.var_file.get(), ino=self.var_ino.get(),
                 project=self.var_project.get(),
                 env=self.var_env.get(), board=self.var_board.get(),
                 port=self.var_port.get().split(" ")[0],
                 fs_type=self.var_fs_type.get(), fs_src=self.var_fs_src.get(),
                 fs_layout=self.var_fs_layout.get(), fs_offset=self.var_fs_offset.get(),
                 fs_size=self.var_fs_size.get(), fs_block=self.var_fs_block.get())
        try:
            with open(CONFIG_FILE, "w", encoding="utf-8") as f:
                json.dump(c, f, indent=2)
        except OSError:
            pass

    def _on_close(self):
        self._save_config()
        if self.proc:
            self.proc.kill()
        self.destroy()


class LibraryDialog(tk.Toplevel):
    """Busca e instala bibliotecas Arduino pelo arduino-cli."""

    def __init__(self, app):
        super().__init__(app)
        self.app = app
        self.title("Bibliotecas Arduino")
        self.geometry("760x460")
        self.transient(app)
        self.var_q = tk.StringVar()
        self.results = []

        top = ttk.Frame(self, padding=8)
        top.pack(fill="x")
        ttk.Label(top, text="Buscar:").pack(side="left")
        ent = ttk.Entry(top, textvariable=self.var_q)
        ent.pack(side="left", fill="x", expand=True, padx=6)
        ent.bind("<Return>", lambda e: self.search())
        ent.focus_set()
        self.btn_search = ttk.Button(top, text="Buscar", command=self.search)
        self.btn_search.pack(side="left")

        mid = ttk.Frame(self, padding=(8, 0))
        mid.pack(fill="both", expand=True)
        cols = ("nome", "versao", "descricao")
        self.tree = ttk.Treeview(mid, columns=cols, show="headings", selectmode="browse")
        for c, w, t in (("nome", 200, "Nome"), ("versao", 70, "Versão"), ("descricao", 440, "Descrição")):
            self.tree.heading(c, text=t)
            self.tree.column(c, width=w, stretch=(c == "descricao"))
        sb = ttk.Scrollbar(mid, command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        self.tree.bind("<Double-1>", lambda e: self.install())

        bot = ttk.Frame(self, padding=8)
        bot.pack(fill="x")
        ttk.Button(bot, text="Instalar selecionada", command=self.install).pack(side="left")
        ttk.Button(bot, text="Instalar .zip...", command=self.install_zip).pack(side="left", padx=6)
        ttk.Button(bot, text="Listar instaladas (no log)", command=self.list_installed).pack(side="left")
        self.var_info = tk.StringVar(value="Digite o nome (ou parte) da biblioteca e clique em Buscar.")
        ttk.Label(bot, textvariable=self.var_info, foreground="#666").pack(side="right")

    def search(self):
        q = self.var_q.get().strip()
        if not q:
            return
        self.btn_search.configure(state="disabled")
        self.var_info.set("Buscando...")
        threading.Thread(target=self._search_worker, args=(q,), daemon=True).start()

    def _search_worker(self, q):
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        try:
            r = subprocess.run([ARDUINO_CLI, "lib", "search", *q.split(), "--format", "json",
                                "--omit-releases-details", "--no-color"],
                               capture_output=True, creationflags=flags, timeout=180)
            data = json.loads(r.stdout.decode("utf-8", "replace") or "{}")
            libs = data.get("libraries") or []
            err = None if r.returncode == 0 else decode(r.stderr).strip()
        except (OSError, ValueError, subprocess.TimeoutExpired) as e:
            libs, err = [], str(e)
        self.after(0, lambda: self._show(libs, err))

    def _show(self, libs, err):
        if not self.winfo_exists():
            return
        self.btn_search.configure(state="normal")
        self.tree.delete(*self.tree.get_children())
        self.results = []
        for lib in libs:
            latest = lib.get("latest") or {}
            self.results.append(lib.get("name", ""))
            self.tree.insert("", "end", values=(lib.get("name", ""), latest.get("version", ""),
                                                latest.get("sentence", "")))
        self.var_info.set(f"Erro: {err[:80]}" if err else f"{len(libs)} resultado(s)")

    def install(self):
        sel = self.tree.selection()
        if not sel:
            messagebox.showinfo(APP_NAME, "Selecione uma biblioteca na lista.", parent=self)
            return
        name = self.tree.item(sel[0], "values")[0]
        self.app.run_cli_steps([(f"Instalando biblioteca {name}",
                                 [ARDUINO_CLI, "lib", "install", name, "--no-color"])])

    def install_zip(self):
        path = filedialog.askopenfilename(parent=self, title="Biblioteca em .zip",
                                          filetypes=[("ZIP", "*.zip")])
        if path:
            self.app.run_cli_steps([(f"Instalando {os.path.basename(path)}",
                                     [ARDUINO_CLI, "lib", "install", "--zip-path", path, "--no-color"])])

    def list_installed(self):
        self.app.run_cli_steps([("Bibliotecas instaladas", [ARDUINO_CLI, "lib", "list", "--no-color"])])


def run_esptool(argv):
    import esptool
    try:
        esptool.main(argv)
    except SystemExit as e:
        return e.code or 0
    except Exception as e:  # noqa: BLE001
        print(f"A fatal error occurred: {e}")
        return 2
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--esptool":
        sys.exit(run_esptool(sys.argv[2:]))
    App().mainloop()
