"""Compara a lógica C++ do firmware com a referência em Python, bit a bit.

Gera cenários aleatórios (entradas, eventos e configurações), executa os dois
lados e compara o estado completo a cada passo, incluindo as mensagens de log.

    python tools/check_parity.py --seeds 20 --steps 50000
"""
from __future__ import annotations

import argparse
import random
import struct
import subprocess
import sys
from dataclasses import fields
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from bcm import BCM, BCMConfig, Ignition, Inputs, LightSwitch  # noqa: E402

FW = ROOT / "firmware"
GXX = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin/g++.exe"
EXE = FW / "build_host" / "bcm_trace.exe"
SOURCES = [FW / "host_test/bcm_trace.cpp", FW / "lib/bcm_logic/src/bcm_logic.cpp"]
HEADERS = [FW / "lib/bcm_logic/src/bcm_logic.h"]

LUX_VALUES = [1.0, 5.0, 30.0, 49.9, 50.0, 50.1, 100.0, 149.9, 150.0, 150.1, 400.0, 2000.0]
SWITCHES = list(LightSwitch)


def hexd(x: float) -> str:
    return struct.pack(">d", float(x)).hex()


def build() -> None:
    newest_src = max(p.stat().st_mtime for p in SOURCES + HEADERS)
    if EXE.exists() and EXE.stat().st_mtime > newest_src:
        return
    EXE.parent.mkdir(exist_ok=True)
    cmd = [str(GXX), "-std=c++11", "-O2", "-msse2", "-mfpmath=sse", "-Wall", "-Wextra",
           "-I", str(FW / "lib/bcm_logic/src"), *map(str, SOURCES), "-o", str(EXE), "-static"]
    print("compilando:", " ".join(Path(c).name if i == 0 else c for i, c in enumerate(cmd)))
    subprocess.run(cmd, check=True, env={"PATH": str(GXX.parent)})


def random_config(rng: random.Random) -> BCMConfig:
    cfg = BCMConfig()
    if rng.random() < 0.15:
        return cfg
    cfg.dark_threshold_lux = rng.uniform(10, 100)
    cfg.light_threshold_lux = cfg.dark_threshold_lux + rng.uniform(0, 150)
    cfg.dark_debounce_s = rng.choice([0.0, rng.uniform(0, 5)])
    cfg.coming_home_auto = rng.random() < 0.8
    cfg.coming_home_time_s = rng.uniform(1, 40)
    cfg.coming_home_flash_window_s = rng.uniform(0, 20)
    cfg.coming_home_start_on_door_close = rng.random() < 0.7
    cfg.coming_home_restart_on_lock = rng.random() < 0.7
    cfg.leaving_home_enabled = rng.random() < 0.85
    cfg.leaving_home_time_s = rng.uniform(1, 40)
    cfg.auto_relock_time_s = rng.uniform(5, 60)
    cfg.lock_flashes = rng.randint(0, 3)
    cfg.unlock_flashes = rng.randint(0, 3)
    cfg.indicator_period_s = rng.uniform(0.2, 1.2)
    cfg.interior_delay_s = rng.uniform(0, 30)
    cfg.interior_unlock_time_s = rng.uniform(0, 40)
    cfg.interior_fade_in_s = rng.choice([0.0, rng.uniform(0, 3)])
    cfg.interior_fade_s = rng.choice([0.0, rng.uniform(0, 4)])
    cfg.exterior_fade_in_s = rng.choice([0.0, rng.uniform(0, 2)])
    cfg.exterior_fade_out_s = rng.choice([0.0, rng.uniform(0, 4)])
    cfg.battery_saver_time_s = rng.uniform(10, 80)
    return cfg


def cfg_line(cfg: BCMConfig) -> str:
    parts = []
    for f in fields(cfg):
        v = getattr(cfg, f.name)
        if isinstance(v, bool):
            parts.append(f"{f.name}={int(v)}")
        elif isinstance(v, int):
            parts.append(f"{f.name}={v}")
        else:
            parts.append(f"{f.name}={float(v).hex()}")
    return "CFG " + " ".join(parts)


def random_scenario(rng: random.Random, steps: int) -> list[tuple]:
    dt = rng.choice([0.02, 0.02, 0.04, 0.1, 0.2])
    inp = Inputs(ignition=Ignition(rng.randint(0, 2)), light_switch=rng.choice(SWITCHES),
                 ambient_lux=rng.choice(LUX_VALUES))
    rows = []
    for _ in range(steps):
        if rng.random() < 0.003:
            inp.ignition = Ignition(rng.choice([0, 0, 1, 2, 2]))
        if rng.random() < 0.001:
            inp.light_switch = rng.choice(SWITCHES)
        if rng.random() < 0.002:
            inp.ambient_lux = rng.choice(LUX_VALUES + [rng.uniform(1, 3000)])
        for i in range(4):
            if rng.random() < (0.003 if i == 0 else 0.0008):
                inp.doors[i] = not inp.doors[i]
        if rng.random() < 0.0004:
            inp.battery_v = rng.choice([11.0, 11.8, 11.79, 12.6, 14.2])
        ev = "".join(c for c, p in (("U", 0.0025), ("L", 0.0025), ("F", 0.002)) if rng.random() < p)
        rows.append((dt, int(inp.ignition), inp.light_switch.value, inp.ambient_lux,
                     "".join("1" if d else "0" for d in inp.doors), inp.battery_v, ev or "-"))
    return rows


def run_python(cfg: BCMConfig, rows: list[tuple]) -> list[str]:
    bcm = BCM(cfg)
    inp = Inputs()
    out_lines = []
    for dt, ign, sw, lux, doors, batt, ev in rows:
        inp.ignition = Ignition(ign)
        inp.light_switch = LightSwitch(sw)
        inp.ambient_lux = lux
        inp.doors = [c == "1" for c in doors]
        inp.battery_v = batt
        for c in ev:
            {"L": bcm.press_lock, "U": bcm.press_unlock, "F": bcm.flash_high_beam}.get(c, lambda: None)()
        o = bcm.step(dt, inp)
        for t, msg in bcm.drain_log():
            out_lines.append(f"L {hexd(t)} {msg}")
        out_lines.append(" ".join([
            "O", hexd(bcm.t), bcm.mode.value, hexd(bcm.mode_timer), str(int(bcm.locked)), str(int(bcm.dark)),
            hexd(o.low_beam), hexd(o.park_lamps), hexd(o.puddle), hexd(o.interior), str(int(o.indicators)),
            hexd(bcm.relock_timer), hexd(bcm.interior_timer),
            str(int(bcm.saver_tripped)), str(int(bcm.interior_saver_tripped))]))
    return out_lines


def run_cpp(cfg: BCMConfig, rows: list[tuple]) -> list[str]:
    lines = [cfg_line(cfg)]
    for dt, ign, sw, lux, doors, batt, ev in rows:
        lines.append(f"S {float(dt).hex()} {ign} {sw} {float(lux).hex()} {doors} {float(batt).hex()} {ev}")
    res = subprocess.run([str(EXE)], input="\n".join(lines).encode(), capture_output=True, check=True)
    return res.stdout.decode("utf-8").splitlines()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=20)
    ap.add_argument("--steps", type=int, default=50000)
    ap.add_argument("--first-seed", type=int, default=0)
    args = ap.parse_args()

    build()
    failures = 0
    total_logs = 0
    for seed in range(args.first_seed, args.first_seed + args.seeds):
        rng = random.Random(seed)
        cfg = random_config(rng)
        rows = random_scenario(rng, args.steps)
        py = run_python(cfg, rows)
        cpp = run_cpp(cfg, rows)
        logs = sum(1 for line in py if line.startswith("L"))
        total_logs += logs
        if py == cpp:
            print(f"seed {seed:3d}: OK  ({len(rows)} passos, dt={rows[0][0]}, {logs} eventos de log)")
            continue
        failures += 1
        idx = next((i for i, (a, b) in enumerate(zip(py, cpp)) if a != b), min(len(py), len(cpp)))
        print(f"seed {seed:3d}: DIVERGÊNCIA na linha {idx} (py={len(py)} linhas, c++={len(cpp)})")
        for i in range(max(0, idx - 2), idx + 1):
            print("   py :", py[i] if i < len(py) else "<fim>")
            print("   c++:", cpp[i] if i < len(cpp) else "<fim>")
    print(f"\n{args.seeds - failures}/{args.seeds} cenários idênticos, {total_logs} eventos de log comparados")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
