"""Lógica do BCM de iluminação (Coming Home / Leaving Home).

Módulo puro, sem dependência de interface gráfica: avança em passos de tempo
discretos (`step`), do mesmo jeito que uma tarefa periódica em um
microcontrolador. Isso facilita portar depois para C.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, IntEnum

from .config import BCMConfig


class Ignition(IntEnum):
    OFF = 0
    ACC = 1
    ON = 2


class LightSwitch(Enum):
    OFF = "OFF"
    PARK = "PARK"   # lanterna
    LOW = "LOW"     # farol baixo
    AUTO = "AUTO"


class Mode(Enum):
    OFF = "OFF"
    LEAVING_HOME = "LEAVING_HOME"
    COMING_HOME = "COMING_HOME"
    NORMAL = "NORMAL"


@dataclass
class Inputs:
    ignition: Ignition = Ignition.OFF
    light_switch: LightSwitch = LightSwitch.AUTO
    ambient_lux: float = 1000.0
    doors: list[bool] = field(default_factory=lambda: [False] * 4)
    battery_v: float = 12.6


@dataclass
class Outputs:
    """Níveis de saída 0..1 (equivale ao duty cycle do PWM do driver)."""
    low_beam: float = 0.0
    park_lamps: float = 0.0
    puddle: float = 0.0
    interior: float = 0.0
    indicators: bool = False


def _ramp(current: float, target: float, dt: float, rise_s: float, fall_s: float) -> float:
    if target > current:
        return min(target, current + (dt / rise_s if rise_s > 0 else 1.0))
    if target < current:
        return max(target, current - (dt / fall_s if fall_s > 0 else 1.0))
    return current


class BCM:
    def __init__(self, config: BCMConfig | None = None):
        self.cfg = config or BCMConfig()
        self.reset()

    def reset(self) -> None:
        self.t = 0.0
        self.mode = Mode.OFF
        self.mode_timer = 0.0
        self.locked = True
        self.dark = False
        self._dark_counter = 0.0
        self._initialized = False

        self._prev_ign_on = False
        self._prev_door_open = False
        self._prev_switch: LightSwitch | None = None
        self._t_ign_off: float | None = None

        self.relock_timer = 0.0      # 0 = inativo
        self.interior_timer = 0.0
        self._door_open_time = 0.0
        self.interior_saver_tripped = False
        self._saver_timer = 0.0
        self.saver_tripped = False

        self._flash_elapsed = 0.0
        self._flash_end = 0.0

        self.out = Outputs()
        self._events: list[str] = []
        self._log: list[tuple[float, str]] = []

    # ------------------------------------------------------------------ eventos
    def press_lock(self) -> None:
        self._events.append("lock")

    def press_unlock(self) -> None:
        self._events.append("unlock")

    def flash_high_beam(self) -> None:
        self._events.append("flash")

    def drain_log(self) -> list[tuple[float, str]]:
        log, self._log = self._log, []
        return log

    # ------------------------------------------------------------------ passo
    def step(self, dt: float, inp: Inputs) -> Outputs:
        cfg = self.cfg
        self.t += dt
        ign_on = inp.ignition == Ignition.ON
        door_open = any(inp.doors)
        battery_ok = inp.battery_v >= cfg.low_battery_v

        self._update_darkness(dt, inp.ambient_lux)

        ign_on_edge = ign_on and not self._prev_ign_on
        ign_off_edge = self._prev_ign_on and not ign_on
        door_close_edge = self._prev_door_open and not door_open
        switch_changed = inp.light_switch != self._prev_switch

        # --- ignição
        if ign_on_edge:
            self._set_mode(Mode.NORMAL, "Ignição ligada")
            self.relock_timer = 0.0
            self.interior_timer = 0.0
        if ign_off_edge:
            self._t_ign_off = self.t
            if (cfg.coming_home_auto and inp.light_switch == LightSwitch.AUTO
                    and self.dark and battery_ok):
                self._start_coming_home("ignição desligada no escuro (AUTO)")
            else:
                self._set_mode(Mode.OFF, "Ignição desligada")

        # --- eventos da chave / alavanca
        for ev in self._events:
            self._handle_event(ev, ign_on, door_open, battery_ok)
        self._events.clear()

        # --- temporizadores de cortesia
        if self.mode in (Mode.COMING_HOME, Mode.LEAVING_HOME):
            coming = self.mode == Mode.COMING_HOME
            full = cfg.coming_home_time_s if coming else cfg.leaving_home_time_s
            hold = door_open and (cfg.coming_home_start_on_door_close if coming else True)
            if hold:
                self.mode_timer = full
            else:
                self.mode_timer -= dt
                if self.mode_timer <= 0:
                    self.mode_timer = 0.0
                    name = "Coming Home" if coming else "Leaving Home"
                    self._set_mode(Mode.OFF, f"{name} encerrado (tempo esgotado)")

        # --- re-travamento automático
        if self.relock_timer > 0:
            if ign_on or door_open:
                self.relock_timer = 0.0
                if door_open:
                    self._emit("Re-travamento automático cancelado (porta aberta)")
            else:
                self.relock_timer -= dt
                if self.relock_timer <= 0:
                    self.relock_timer = 0.0
                    self.locked = True
                    self.interior_timer = 0.0
                    self._start_flashes(cfg.lock_flashes)
                    self._emit("Re-travamento automático (nenhuma porta foi aberta)")
                    if self.mode == Mode.LEAVING_HOME:
                        self._set_mode(Mode.OFF, "Leaving Home cancelado pelo re-travamento")

        # --- luz interna
        if door_close_edge and not ign_on:
            self.interior_timer = max(self.interior_timer, cfg.interior_delay_s)
        if self.interior_timer > 0:
            self.interior_timer = max(0.0, self.interior_timer - dt)
        if door_open and not ign_on:
            self._door_open_time += dt
            if self._door_open_time >= cfg.battery_saver_time_s and not self.interior_saver_tripped:
                self.interior_saver_tripped = True
                self._emit("Proteção de bateria: luz interna desligada (porta aberta)")
        else:
            self._door_open_time = 0.0
            self.interior_saver_tripped = False
        door_light = door_open and not self.interior_saver_tripped
        interior_target = 1.0 if door_light or (not ign_on and self.interior_timer > 0) else 0.0

        # --- faróis e lanternas
        low = park = puddle = 0.0
        sw = inp.light_switch
        if ign_on:
            self._saver_timer = 0.0
            self.saver_tripped = False
            if sw == LightSwitch.PARK:
                park = 1.0
            elif sw == LightSwitch.LOW or (sw == LightSwitch.AUTO and self.dark):
                park = low = 1.0
        else:
            if switch_changed:
                self._saver_timer = 0.0
                self.saver_tripped = False
            if sw in (LightSwitch.PARK, LightSwitch.LOW) and not self.saver_tripped:
                self._saver_timer += dt
                if self._saver_timer >= cfg.battery_saver_time_s:
                    self.saver_tripped = True
                    self._emit("Proteção de bateria: luzes externas desligadas")
                else:
                    park = 1.0
                    low = 1.0 if sw == LightSwitch.LOW else 0.0

        if self.mode in (Mode.COMING_HOME, Mode.LEAVING_HOME):
            low = park = puddle = 1.0
        if not ign_on and door_open and self.dark:
            puddle = 1.0

        # --- rampas (fade via PWM)
        o = self.out
        o.low_beam = _ramp(o.low_beam, low, dt, cfg.exterior_fade_in_s, cfg.exterior_fade_out_s)
        o.park_lamps = _ramp(o.park_lamps, park, dt, cfg.exterior_fade_in_s, cfg.exterior_fade_out_s)
        o.puddle = _ramp(o.puddle, puddle, dt, cfg.exterior_fade_in_s, cfg.exterior_fade_out_s)
        o.interior = _ramp(o.interior, interior_target, dt, cfg.interior_fade_in_s, cfg.interior_fade_s)

        # --- piscas de confirmação
        if self._flash_elapsed < self._flash_end:
            period = cfg.indicator_period_s
            o.indicators = (self._flash_elapsed % period) < period / 2
            self._flash_elapsed += dt
        else:
            o.indicators = False

        self._prev_ign_on = ign_on
        self._prev_door_open = door_open
        self._prev_switch = sw
        return o

    # ------------------------------------------------------------------ internos
    def _handle_event(self, ev: str, ign_on: bool, door_open: bool, battery_ok: bool) -> None:
        cfg = self.cfg
        if ev == "unlock":
            self.locked = False
            self._start_flashes(cfg.unlock_flashes)
            self._emit("Destravado")
            if not ign_on:
                self.relock_timer = cfg.auto_relock_time_s
                self.interior_timer = cfg.interior_unlock_time_s
                if (cfg.leaving_home_enabled and self.dark and battery_ok
                        and self.mode != Mode.COMING_HOME):
                    self.mode_timer = cfg.leaving_home_time_s
                    self._set_mode(Mode.LEAVING_HOME, "Leaving Home ativado (destravado no escuro)")
        elif ev == "lock":
            if door_open:
                self._emit("Travamento recusado: porta aberta")
                return
            self.locked = True
            self.relock_timer = 0.0
            self.interior_timer = 0.0
            self._start_flashes(cfg.lock_flashes)
            self._emit("Travado")
            if self.mode == Mode.LEAVING_HOME:
                self._set_mode(Mode.OFF, "Leaving Home cancelado pelo travamento")
            elif self.mode == Mode.COMING_HOME and cfg.coming_home_restart_on_lock:
                self.mode_timer = cfg.coming_home_time_s
                self._emit("Coming Home reiniciado pelo travamento")
        elif ev == "flash":
            if ign_on:
                self._emit("Lampejo do farol alto")
            elif (self._t_ign_off is not None and battery_ok
                  and self.t - self._t_ign_off <= cfg.coming_home_flash_window_s):
                self._start_coming_home("lampejo do farol alto")
            else:
                self._emit("Lampejo ignorado (fora da janela após desligar a ignição)")

    def _update_darkness(self, dt: float, lux: float) -> None:
        cfg = self.cfg
        if not self._initialized:
            self.dark = lux < cfg.dark_threshold_lux
            self._initialized = True
            return
        crossing = lux > cfg.light_threshold_lux if self.dark else lux < cfg.dark_threshold_lux
        if crossing:
            self._dark_counter += dt
            if self._dark_counter >= cfg.dark_debounce_s:
                self.dark = not self.dark
                self._dark_counter = 0.0
                self._emit("Sensor de luz: " + ("ESCURO" if self.dark else "CLARO"))
        else:
            self._dark_counter = 0.0

    def _start_coming_home(self, reason: str) -> None:
        self.mode_timer = self.cfg.coming_home_time_s
        if self.mode == Mode.COMING_HOME:
            self._emit(f"Coming Home reiniciado ({reason})")
        else:
            self._set_mode(Mode.COMING_HOME, f"Coming Home ativado ({reason})")

    def _start_flashes(self, n: int) -> None:
        self._flash_elapsed = 0.0
        self._flash_end = n * self.cfg.indicator_period_s

    def _set_mode(self, mode: Mode, reason: str) -> None:
        if mode != self.mode:
            self.mode = mode
            self._emit(reason)

    def _emit(self, msg: str) -> None:
        self._log.append((self.t, msg))
