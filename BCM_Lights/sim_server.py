"""ESP32 de mentira: serve a interface web (firmware/data) com a lógica Python.

Fala o mesmo protocolo WebSocket do firmware, para desenvolver e testar a
página no PC sem gravar a placa. Só usa a biblioteca padrão.

    python sim_server.py            -> http://localhost:8080
"""
from __future__ import annotations

import argparse
import asyncio
import base64
import dataclasses
import hashlib
import json
import math
import re
import struct
import time
from pathlib import Path

from bcm import BCM, BCMConfig, Ignition, Inputs, LightSwitch

ROOT = Path(__file__).resolve().parent
DATA_DIR = ROOT / "firmware" / "data"
LOGIC_CPP = ROOT / "firmware" / "lib" / "bcm_logic" / "src" / "bcm_logic.cpp"

STEP_S = 0.02
STATE_PERIOD_S = 0.1
LOG_SYNC_COUNT = 40
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CONTENT_TYPES = {".html": "text/html; charset=utf-8", ".js": "application/javascript; charset=utf-8",
                 ".css": "text/css; charset=utf-8", ".svg": "image/svg+xml", ".ico": "image/x-icon",
                 ".png": "image/png", ".json": "application/json"}


def load_config_fields() -> list[dict]:
    """Lê a tabela kConfigFields do C++ (fonte única de tipos e limites)."""
    src = LOGIC_CPP.read_text(encoding="utf-8")
    fields = []
    for kind, args in re.findall(r"\b(NUM_F|BOOL_F|INT_F)\(([^)]*)\)", src):
        parts = [p.strip() for p in args.split(",")]
        if parts[0] == "name":  # definição da macro
            continue
        kind = {"NUM_F": "num", "BOOL_F": "bool", "INT_F": "int"}[kind]
        lo, hi = (0.0, 1.0) if kind == "bool" else (float(parts[1]), float(parts[2]))
        fields.append({"k": parts[0], "t": kind, "min": lo, "max": hi})
    known = {f.name for f in dataclasses.fields(BCMConfig)}
    missing = known - {f["k"] for f in fields}
    if missing:
        raise RuntimeError(f"campos sem limite no C++: {missing}")
    return fields


class Simulator:
    def __init__(self) -> None:
        self.fields = load_config_fields()
        self.field_by_key = {f["k"]: f for f in self.fields}
        self.cfg = BCMConfig()
        self.bcm = BCM(self.cfg)
        self.web = {"ign": 0, "sw": "AUTO", "lux": 1000.0, "doors": [False] * 4, "batt": 12.6}
        self.io = {"lux_src": "web", "phys": True}
        self.speed = 1
        self.paused = False
        self.ind_seen = False
        self.logs: list[tuple[float, str]] = []
        self.clients: set[asyncio.StreamWriter] = set()
        self.started = time.monotonic()

    # ------------------------------------------------------------ comandos
    def handle(self, msg: dict) -> None:
        cmd = msg.get("cmd")
        if cmd == "lock":
            self.bcm.press_lock()
        elif cmd == "unlock":
            self.bcm.press_unlock()
        elif cmd == "flash":
            self.bcm.flash_high_beam()
        elif cmd == "reset":
            self.bcm.reset()
            self.logs = (self.logs + [(0.0, "BCM reiniciado")])[-64:]
            self.broadcast(self.logs_msg([(0.0, "BCM reiniciado")]))
        elif cmd == "defaults":
            self.cfg = BCMConfig()
            self.bcm.cfg = self.cfg
            self.io = {"lux_src": "web", "phys": True}
            self.broadcast(self.config_msg())
        elif cmd == "input":
            if "ign" in msg:
                self.web["ign"] = max(0, min(2, int(msg["ign"])))
            if msg.get("sw") in {s.value for s in LightSwitch}:
                self.web["sw"] = msg["sw"]
            if "lux" in msg:
                self.web["lux"] = max(0.0, min(100000.0, float(msg["lux"])))
            if "batt" in msg:
                self.web["batt"] = max(0.0, min(20.0, float(msg["batt"])))
            if "door" in msg:
                i, v = msg["door"]
                if 0 <= int(i) < 4:
                    self.web["doors"][int(i)] = bool(v)
        elif cmd == "config":
            f = self.field_by_key.get(msg.get("key"))
            if f:
                v = max(f["min"], min(f["max"], float(msg["value"])))
                v = bool(v) if f["t"] == "bool" else round(v) if f["t"] == "int" else v
                setattr(self.cfg, f["k"], v)
                self.broadcast(self.config_msg())
        elif cmd == "sim":
            if "speed" in msg:
                self.speed = max(1, min(50, int(msg["speed"])))
            if "paused" in msg:
                self.paused = bool(msg["paused"])
        elif cmd == "io":
            if "lux_src" in msg:
                self.io["lux_src"] = "ldr" if msg["lux_src"] == "ldr" else "web"
            if "phys" in msg:
                self.io["phys"] = bool(msg["phys"])
            self.broadcast(self.config_msg())

    # ------------------------------------------------------------ simulação
    def fake_ldr(self) -> float:
        # Sem LDR de verdade: ciclo dia/noite de 4 minutos para testar o modo "Sensor LDR".
        return 10 ** (2.0 + 2.0 * math.sin((time.monotonic() - self.started) * 2 * math.pi / 240))

    def inputs(self) -> Inputs:
        lux = self.fake_ldr() if self.io["lux_src"] == "ldr" else self.web["lux"]
        return Inputs(ignition=Ignition(self.web["ign"]), light_switch=LightSwitch(self.web["sw"]),
                      ambient_lux=lux, doors=list(self.web["doors"]), battery_v=self.web["batt"])

    def tick(self) -> None:
        if self.paused:
            return
        inp = self.inputs()
        for _ in range(self.speed):
            self.bcm.step(STEP_S, inp)
            self.ind_seen |= self.bcm.out.indicators
        new = self.bcm.drain_log()
        if new:
            self.logs = (self.logs + new)[-64:]
            self.broadcast(self.logs_msg(new))

    # ------------------------------------------------------------ mensagens
    def config_msg(self) -> dict:
        fields = []
        for f in self.fields:
            v = getattr(self.cfg, f["k"])
            fields.append({**f, "v": int(v) if isinstance(v, bool) else round(v, 3)})
        return {"type": "config", "fields": fields, "io": dict(self.io)}

    @staticmethod
    def logs_msg(items) -> dict:
        return {"type": "logs", "items": [[round(t, 3), m] for t, m in items]}

    def state_msg(self) -> dict:
        b, o, inp = self.bcm, self.bcm.out, self.inputs()
        r = lambda v: round(v, 3)  # noqa: E731
        msg = {
            "type": "state", "t": r(b.t), "mode": b.mode.value, "timer": r(b.mode_timer),
            "locked": b.locked, "dark": b.dark, "relock": r(b.relock_timer), "itimer": r(b.interior_timer),
            "saver": b.saver_tripped or b.interior_saver_tripped,
            "out": {"low": r(o.low_beam), "park": r(o.park_lamps), "puddle": r(o.puddle),
                    "int": r(o.interior), "ind": o.indicators or self.ind_seen},
            "in": {"ign": int(inp.ignition), "sw": inp.light_switch.value, "lux": r(inp.ambient_lux),
                   "doors": [int(d) for d in inp.doors], "batt": r(inp.battery_v)},
            "web": {**self.web, "doors": [int(d) for d in self.web["doors"]]},
            "phys": {"ign": 0, "lux": r(self.fake_ldr()), "doors": [0, 0, 0, 0]},
            "sim": {"speed": self.speed, "paused": self.paused},
            "sys": {"heap": 0, "up": int(time.monotonic() - self.started), "clients": len(self.clients)},
        }
        self.ind_seen = False
        return msg

    def broadcast(self, msg: dict) -> None:
        frame = ws_frame(json.dumps(msg, ensure_ascii=False).encode())
        for w in list(self.clients):
            try:
                w.write(frame)
            except Exception:
                self.clients.discard(w)

    async def run(self) -> None:
        next_tick = time.monotonic()
        next_state = next_tick
        while True:
            next_tick += STEP_S
            self.tick()
            now = time.monotonic()
            if now >= next_state:
                next_state = now + STATE_PERIOD_S
                if self.clients:
                    self.broadcast(self.state_msg())
            await asyncio.sleep(max(0.0, next_tick - time.monotonic()))


# ================================================================ HTTP / WebSocket
def ws_frame(payload: bytes, opcode: int = 0x1) -> bytes:
    n = len(payload)
    if n < 126:
        header = struct.pack("!BB", 0x80 | opcode, n)
    elif n < 65536:
        header = struct.pack("!BBH", 0x80 | opcode, 126, n)
    else:
        header = struct.pack("!BBQ", 0x80 | opcode, 127, n)
    return header + payload


async def ws_read(reader: asyncio.StreamReader) -> tuple[int, bytes]:
    b1, b2 = await reader.readexactly(2)
    opcode, n = b1 & 0x0F, b2 & 0x7F
    if n == 126:
        (n,) = struct.unpack("!H", await reader.readexactly(2))
    elif n == 127:
        (n,) = struct.unpack("!Q", await reader.readexactly(8))
    mask = await reader.readexactly(4) if b2 & 0x80 else b"\0\0\0\0"
    data = bytearray(await reader.readexactly(n))
    for i in range(n):
        data[i] ^= mask[i % 4]
    return opcode, bytes(data)


async def serve_ws(sim: Simulator, reader, writer, headers: dict) -> None:
    accept = base64.b64encode(hashlib.sha1((headers["sec-websocket-key"] + WS_GUID).encode()).digest()).decode()
    writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
    writer.write(ws_frame(json.dumps(sim.config_msg(), ensure_ascii=False).encode()))
    writer.write(ws_frame(json.dumps(sim.logs_msg(sim.logs[-LOG_SYNC_COUNT:]), ensure_ascii=False).encode()))
    sim.clients.add(writer)
    try:
        while True:
            opcode, data = await ws_read(reader)
            if opcode == 0x8:
                break
            if opcode == 0x9:
                writer.write(ws_frame(data, 0xA))
            elif opcode == 0x1:
                try:
                    sim.handle(json.loads(data))
                except (ValueError, KeyError, TypeError) as e:
                    print("comando inválido:", data[:200], e)
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        sim.clients.discard(writer)


async def handle_conn(sim: Simulator, reader, writer) -> None:
    try:
        request = await reader.readline()
        parts = request.decode("latin-1").split()
        headers = {}
        while (line := await reader.readline()) not in (b"\r\n", b"\n", b""):
            k, _, v = line.decode("latin-1").partition(":")
            headers[k.strip().lower()] = v.strip()
        if len(parts) < 2:
            return
        path = parts[1].split("?")[0]
        if path == "/ws" and headers.get("upgrade", "").lower() == "websocket":
            await serve_ws(sim, reader, writer, headers)
            return
        rel = "index.html" if path == "/" else path.lstrip("/")
        file = (DATA_DIR / rel).resolve()
        if DATA_DIR.resolve() in file.parents and file.is_file():
            body = file.read_bytes()
            ctype = CONTENT_TYPES.get(file.suffix, "application/octet-stream")
            writer.write(f"HTTP/1.1 200 OK\r\nContent-Type: {ctype}\r\nContent-Length: {len(body)}\r\n"
                         "Cache-Control: no-cache\r\nConnection: close\r\n\r\n".encode() + body)
        else:
            writer.write(b"HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        await writer.drain()
    except (ConnectionError, asyncio.IncompleteReadError):
        pass
    finally:
        writer.close()


async def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8080)
    args = ap.parse_args()

    sim = Simulator()
    server = await asyncio.start_server(lambda r, w: handle_conn(sim, r, w), args.host, args.port)
    print(f"Simulador do ESP32 em http://localhost:{args.port}  (Ctrl+C para sair)")
    async with server:
        await asyncio.gather(server.serve_forever(), sim.run())


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
