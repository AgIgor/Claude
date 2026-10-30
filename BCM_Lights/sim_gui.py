"""Simulador do BCM de iluminação: painel de controle, carro animado e gráficos."""
from __future__ import annotations

import bisect
import itertools
import math
import time
import tkinter as tk
from collections import deque
from tkinter import ttk

import matplotlib

matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg  # noqa: E402
from matplotlib.figure import Figure  # noqa: E402

from bcm import BCM, BCMConfig, Ignition, Inputs, LightSwitch, Mode  # noqa: E402

SIM_STEP = 0.02        # passo da lógica (s de simulação) — como uma tarefa de 20 ms
SAMPLE_PERIOD = 0.1    # amostragem para os gráficos (s de simulação)
GUI_PERIOD_MS = 40     # atualização da tela
PLOT_PERIOD = 0.15     # redesenho dos gráficos (s reais)
HISTORY_MAX = 3200     # 320 s de histórico

MODE_LABELS = {
    Mode.OFF: "Desligado",
    Mode.LEAVING_HOME: "Leaving Home (boas-vindas)",
    Mode.COMING_HOME: "Coming Home (acompanhamento)",
    Mode.NORMAL: "Normal (ignição ligada)",
}
MODE_LEVEL = {Mode.OFF: 0, Mode.LEAVING_HOME: 1, Mode.COMING_HOME: 2, Mode.NORMAL: 3}

DOOR_NAMES = ["Motorista", "Passageiro", "Traseira esq.", "Traseira dir."]

OUTPUT_LANES = [("Farol baixo", "low", "#e0a800"), ("Lanterna", "park", "#d62728"),
                ("Luz de chão", "puddle", "#1f77b4"), ("Luz interna", "interior", "#ff7f0e"),
                ("Piscas", "ind", "#ffb000")]
INPUT_LANES = [("Ignição", "ign", "#2ca02c"), ("Travado", "locked", "#9467bd"),
               ("Porta aberta", "door", "#8c564b"), ("Escuro", "dark", "#17344f")]

NUM_FIELDS = [
    ("coming_home_time_s", "Coming Home (s)", 0, 300, 5),
    ("leaving_home_time_s", "Leaving Home (s)", 0, 300, 5),
    ("coming_home_flash_window_s", "Janela p/ lampejo (s)", 0, 60, 1),
    ("auto_relock_time_s", "Re-travamento automático (s)", 5, 300, 5),
    ("interior_delay_s", "Retardo luz interna (s)", 0, 120, 1),
    ("interior_unlock_time_s", "Luz interna ao destravar (s)", 0, 120, 5),
    ("interior_fade_s", "Fade luz interna (s)", 0, 10, 0.5),
    ("exterior_fade_out_s", "Fade faróis (s)", 0, 10, 0.5),
    ("battery_saver_time_s", "Proteção de bateria (s)", 10, 3600, 10),
    ("dark_threshold_lux", "Limiar escuro (lux)", 1, 5000, 10),
    ("light_threshold_lux", "Limiar claro (lux)", 1, 5000, 10),
    ("dark_debounce_s", "Filtro do sensor (s)", 0, 30, 0.5),
    ("low_battery_v", "Bateria mínima (V)", 10, 13, 0.1),
]
BOOL_FIELDS = [
    ("coming_home_auto", "Coming Home automático (AUTO + escuro)"),
    ("coming_home_start_on_door_close", "Coming Home conta só com portas fechadas"),
    ("coming_home_restart_on_lock", "Travar reinicia o Coming Home"),
    ("leaving_home_enabled", "Leaving Home habilitado"),
]


def lerp_color(c1: str, c2: str, t: float) -> str:
    t = max(0.0, min(1.0, t))
    a = [int(c1[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(c2[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02x%02x%02x" % tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def round_rect(cv: tk.Canvas, x1, y1, x2, y2, r, **kw):
    pts = [x1 + r, y1, x2 - r, y1, x2, y1, x2, y1 + r, x2, y2 - r, x2, y2,
           x2 - r, y2, x1 + r, y2, x1, y2, x1, y2 - r, x1, y1 + r, x1, y1]
    return cv.create_polygon(pts, smooth=True, **kw)


class SimulatorApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("Simulador BCM: Coming Home / Leaving Home")
        root.geometry("1450x920")
        root.minsize(1200, 760)

        self.cfg = BCMConfig()
        self.bcm = BCM(self.cfg)
        self.inp = Inputs()

        self.ign_var = tk.IntVar(value=int(Ignition.OFF))
        self.switch_var = tk.StringVar(value=LightSwitch.AUTO.value)
        self.lux_pos = tk.DoubleVar(value=3.0)          # lux = 10**pos
        self.batt_var = tk.DoubleVar(value=12.6)
        self.door_vars = [tk.BooleanVar(value=False) for _ in DOOR_NAMES]
        self.speed_var = tk.StringVar(value="1x")
        self.window_var = tk.StringVar(value="120 s")
        self.paused = False

        self.hist = {k: deque(maxlen=HISTORY_MAX) for k in
                     ("t", "lux", "mode", "low", "park", "puddle", "interior", "ind",
                      "ign", "locked", "door", "dark")}
        self._next_sample = 0.0
        self._acc = 0.0
        self._ind_seen = False

        self._build_ui()
        self._last = time.perf_counter()
        self._last_plot = 0.0
        self._after_id = root.after(GUI_PERIOD_MS, self._tick)
        root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ================================================================== UI
    def _build_ui(self):
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Big.TButton", font=("Segoe UI", 10, "bold"), padding=6)
        style.configure("Status.TLabel", font=("Segoe UI", 10))
        style.configure("Mode.TLabel", font=("Segoe UI", 12, "bold"))

        self.root.columnconfigure(1, weight=1)
        self.root.rowconfigure(0, weight=1)

        nb = ttk.Notebook(self.root, width=310)
        nb.grid(row=0, column=0, sticky="ns", padx=(8, 4), pady=8)
        ctrl = ttk.Frame(nb, padding=8)
        conf = ttk.Frame(nb, padding=8)
        nb.add(ctrl, text="Controles")
        nb.add(conf, text="Configuração")
        self._build_controls(ctrl)
        self._build_config(conf)

        right = ttk.Frame(self.root)
        right.grid(row=0, column=1, sticky="nsew", padx=(4, 8), pady=8)
        right.columnconfigure(0, weight=1)
        right.rowconfigure(1, weight=1)

        top = ttk.Frame(right)
        top.grid(row=0, column=0, sticky="ew")
        top.columnconfigure(2, weight=1)

        self.car = tk.Canvas(top, width=320, height=330, highlightthickness=1,
                             highlightbackground="#999")
        self.car.grid(row=0, column=0, sticky="n")
        self._build_status(top)
        self._build_log(top)

        self._build_plots(right)

    def _build_controls(self, f: ttk.Frame):
        f.columnconfigure(0, weight=1)

        g = ttk.LabelFrame(f, text="Ignição", padding=6)
        g.grid(sticky="ew", pady=4)
        for i, (txt, val) in enumerate([("OFF", Ignition.OFF), ("ACC", Ignition.ACC), ("ON", Ignition.ON)]):
            ttk.Radiobutton(g, text=txt, value=int(val), variable=self.ign_var).grid(row=0, column=i, padx=8)

        g = ttk.LabelFrame(f, text="Comando de luz", padding=6)
        g.grid(sticky="ew", pady=4)
        for i, (txt, sw) in enumerate([("OFF", LightSwitch.OFF), ("Lanterna", LightSwitch.PARK),
                                       ("Farol", LightSwitch.LOW), ("AUTO", LightSwitch.AUTO)]):
            ttk.Radiobutton(g, text=txt, value=sw.value, variable=self.switch_var).grid(row=0, column=i, padx=4)

        g = ttk.LabelFrame(f, text="Chave / alavanca", padding=6)
        g.grid(sticky="ew", pady=4)
        g.columnconfigure((0, 1), weight=1)
        ttk.Button(g, text="Travar", style="Big.TButton",
                   command=self.bcm.press_lock).grid(row=0, column=0, sticky="ew", padx=2)
        ttk.Button(g, text="Destravar", style="Big.TButton",
                   command=self.bcm.press_unlock).grid(row=0, column=1, sticky="ew", padx=2)
        ttk.Button(g, text="Lampejo do farol alto",
                   command=self.bcm.flash_high_beam).grid(row=1, column=0, columnspan=2, sticky="ew", pady=(4, 0))

        g = ttk.LabelFrame(f, text="Portas", padding=6)
        g.grid(sticky="ew", pady=4)
        for i, (name, var) in enumerate(zip(DOOR_NAMES, self.door_vars)):
            ttk.Checkbutton(g, text=name, variable=var).grid(row=i // 2, column=i % 2, sticky="w", padx=4)

        g = ttk.LabelFrame(f, text="Luminosidade externa", padding=6)
        g.grid(sticky="ew", pady=4)
        g.columnconfigure(0, weight=1)
        self.lux_label = ttk.Label(g, text="")
        self.lux_label.grid(row=0, column=0, columnspan=3, sticky="w")
        ttk.Scale(g, from_=0.0, to=4.0, variable=self.lux_pos,
                  orient="horizontal").grid(row=1, column=0, columnspan=3, sticky="ew")
        for i, (txt, lux) in enumerate([("Noite", 5), ("Crepúsculo", 100), ("Dia", 2000)]):
            ttk.Button(g, text=txt, command=lambda l=lux: self.lux_pos.set(math.log10(l))).grid(
                row=2, column=i, sticky="ew", padx=1, pady=(4, 0))

        g = ttk.LabelFrame(f, text="Bateria", padding=6)
        g.grid(sticky="ew", pady=4)
        g.columnconfigure(0, weight=1)
        self.batt_label = ttk.Label(g, text="")
        self.batt_label.grid(row=0, column=0, sticky="w")
        ttk.Scale(g, from_=10.5, to=14.5, variable=self.batt_var,
                  orient="horizontal").grid(row=1, column=0, sticky="ew")

        g = ttk.LabelFrame(f, text="Simulação", padding=6)
        g.grid(sticky="ew", pady=4)
        ttk.Label(g, text="Velocidade").grid(row=0, column=0, sticky="w")
        ttk.Combobox(g, textvariable=self.speed_var, width=6, state="readonly",
                     values=["1x", "2x", "5x", "10x", "20x"]).grid(row=0, column=1, sticky="w", padx=4)
        ttk.Label(g, text="Janela").grid(row=1, column=0, sticky="w")
        ttk.Combobox(g, textvariable=self.window_var, width=6, state="readonly",
                     values=["30 s", "60 s", "120 s", "300 s"]).grid(row=1, column=1, sticky="w", padx=4)
        self.pause_btn = ttk.Button(g, text="Pausar", command=self._toggle_pause)
        self.pause_btn.grid(row=2, column=0, sticky="ew", pady=(6, 0), padx=(0, 2))
        ttk.Button(g, text="Reiniciar", command=self._reset).grid(row=2, column=1, sticky="ew", pady=(6, 0))

    def _build_config(self, f: ttk.Frame):
        self._conf_vars = []  # mantém referências vivas
        for r, (attr, label, lo, hi, inc) in enumerate(NUM_FIELDS):
            ttk.Label(f, text=label).grid(row=r, column=0, sticky="w", pady=2)
            var = tk.StringVar(value=str(getattr(self.cfg, attr)))
            tk.Spinbox(f, from_=lo, to=hi, increment=inc, width=8,
                       textvariable=var).grid(row=r, column=1, sticky="e", padx=4)
            var.trace_add("write", lambda *_, a=attr, v=var: self._set_num(a, v))
            self._conf_vars.append(var)
        base = len(NUM_FIELDS)
        ttk.Separator(f).grid(row=base, column=0, columnspan=2, sticky="ew", pady=8)
        for i, (attr, label) in enumerate(BOOL_FIELDS):
            var = tk.BooleanVar(value=getattr(self.cfg, attr))
            ttk.Checkbutton(f, text=label, variable=var,
                            command=lambda a=attr, v=var: setattr(self.cfg, a, v.get())).grid(
                row=base + 1 + i, column=0, columnspan=2, sticky="w", pady=2)
            self._conf_vars.append(var)
        ttk.Label(f, text="As alterações valem imediatamente.", foreground="#666").grid(
            row=base + 1 + len(BOOL_FIELDS), column=0, columnspan=2, sticky="w", pady=(10, 0))

    def _set_num(self, attr: str, var: tk.StringVar):
        try:
            setattr(self.cfg, attr, float(var.get().replace(",", ".")))
        except ValueError:
            pass

    def _build_status(self, parent):
        f = ttk.LabelFrame(parent, text="Estado do BCM", padding=8)
        f.grid(row=0, column=1, sticky="nsw", padx=8)
        self.mode_label = ttk.Label(f, text="", style="Mode.TLabel", width=30)
        self.mode_label.grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 6))
        self.status_vars = {}
        rows = [("timer", "Tempo restante"), ("locks", "Travas"), ("relock", "Re-travamento auto."),
                ("interior", "Timer luz interna"), ("sensor", "Sensor de luz"),
                ("saver", "Proteção bateria"), ("outputs", "Saídas"), ("simt", "Tempo de simulação")]
        for i, (key, label) in enumerate(rows, start=1):
            ttk.Label(f, text=label + ":", style="Status.TLabel").grid(row=i, column=0, sticky="w")
            var = tk.StringVar()
            ttk.Label(f, textvariable=var, style="Status.TLabel").grid(row=i, column=1, sticky="w", padx=6)
            self.status_vars[key] = var

    def _build_log(self, parent):
        f = ttk.LabelFrame(parent, text="Eventos", padding=4)
        f.grid(row=0, column=2, sticky="nsew")
        f.rowconfigure(0, weight=1)
        f.columnconfigure(0, weight=1)
        self.log_list = tk.Listbox(f, height=16, font=("Consolas", 9), activestyle="none")
        self.log_list.grid(row=0, column=0, sticky="nsew")
        sb = ttk.Scrollbar(f, orient="vertical", command=self.log_list.yview)
        sb.grid(row=0, column=1, sticky="ns")
        self.log_list.configure(yscrollcommand=sb.set)

    def _build_plots(self, parent):
        self.fig = Figure(figsize=(10, 5.5), dpi=90)
        self.fig.subplots_adjust(left=0.1, right=0.98, top=0.95, bottom=0.08, hspace=0.45)
        gs = self.fig.add_gridspec(4, 1, height_ratios=[1.0, 0.9, 1.9, 1.5])
        ax_lux = self.fig.add_subplot(gs[0])
        ax_mode = self.fig.add_subplot(gs[1], sharex=ax_lux)
        ax_out = self.fig.add_subplot(gs[2], sharex=ax_lux)
        ax_in = self.fig.add_subplot(gs[3], sharex=ax_lux)
        self.axes = (ax_lux, ax_mode, ax_out, ax_in)

        ax_lux.set_yscale("log")
        ax_lux.set_ylim(1, 10000)
        ax_lux.set_ylabel("lux")
        ax_lux.set_title("Luminosidade externa", loc="left", fontsize=10)
        self.l_lux, = ax_lux.plot([], [], color="#444", animated=True)
        self.l_dark_th = ax_lux.axhline(self.cfg.dark_threshold_lux, color="#17344f", ls="--", lw=1,
                                        label="limiar escuro", animated=True)
        self.l_light_th = ax_lux.axhline(self.cfg.light_threshold_lux, color="#e0a800", ls="--", lw=1,
                                         label="limiar claro", animated=True)
        ax_lux.legend(loc="upper left", fontsize=8)

        ax_mode.set_title("Modo do BCM", loc="left", fontsize=10)
        ax_mode.set_ylim(-0.4, 3.4)
        ax_mode.set_yticks(range(4), ["OFF", "LEAVING", "COMING", "NORMAL"], fontsize=8)
        self.l_mode, = ax_mode.plot([], [], drawstyle="steps-post", color="#6a3d9a", lw=2, animated=True)

        self.out_lines = self._make_lanes(ax_out, OUTPUT_LANES, "Saídas (nível PWM)")
        self.in_lines = self._make_lanes(ax_in, INPUT_LANES, "Entradas")
        ax_in.set_xlabel("tempo (s) relativo ao instante atual (0 = agora)")
        self.animated = [self.l_lux, self.l_dark_th, self.l_light_th, self.l_mode] + [
            line for lines in (self.out_lines, self.in_lines) for line, _ in lines.values()]

        for ax in self.axes:
            ax.grid(True, alpha=0.3)
        for ax in self.axes[:-1]:
            ax.tick_params(labelbottom=False)

        # Eixo X fixo (-janela..0): o fundo não muda e só as curvas são redesenhadas (blitting).
        self._plot_bg = None
        self._apply_window()
        self.plot_canvas = FigureCanvasTkAgg(self.fig, master=parent)
        self.plot_canvas.get_tk_widget().grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        self.plot_canvas.mpl_connect("draw_event", self._on_draw)
        self.window_var.trace_add("write", lambda *_: self._apply_window(redraw=True))

    @property
    def window(self) -> float:
        return float(self.window_var.get().split()[0])

    def _apply_window(self, redraw: bool = False):
        self.axes[0].set_xlim(-self.window, 0)
        if redraw:
            self.plot_canvas.draw()

    def _on_draw(self, _event):
        self._plot_bg = self.plot_canvas.copy_from_bbox(self.fig.bbox)
        self._blit_lines()

    def _blit_lines(self):
        for artist in self.animated:
            artist.axes.draw_artist(artist)
        self.plot_canvas.blit(self.fig.bbox)

    @staticmethod
    def _make_lanes(ax, lanes, title):
        n = len(lanes)
        lines = {}
        ticks = []
        for i, (label, key, color) in enumerate(lanes):
            off = (n - 1 - i) * 1.2
            ax.axhline(off, color="#ccc", lw=0.6)
            line, = ax.plot([], [], color=color, lw=1.8, drawstyle="steps-post", animated=True)
            lines[key] = (line, off)
            ticks.append((off + 0.45, label))
        ax.set_yticks([t for t, _ in ticks], [l for _, l in ticks], fontsize=8)
        ax.set_ylim(-0.2, n * 1.2)
        ax.set_title(title, loc="left", fontsize=10)
        return lines

    # ================================================================== loop
    def _tick(self):
        now = time.perf_counter()
        real_dt = min(now - self._last, 0.25)
        self._last = now

        self._read_inputs()
        self._ind_seen = False
        if not self.paused:
            self._acc += real_dt * self.speed
            while self._acc >= SIM_STEP:
                self._acc -= SIM_STEP
                out = self.bcm.step(SIM_STEP, self.inp)
                self._ind_seen |= out.indicators
                while self.bcm.t >= self._next_sample:
                    self._sample()
                    self._next_sample += SAMPLE_PERIOD

        self._drain_log()
        self._draw_car()
        self._update_status()
        if now - self._last_plot >= PLOT_PERIOD:
            self._last_plot = now
            self._update_plots()
        self._after_id = self.root.after(GUI_PERIOD_MS, self._tick)

    @property
    def speed(self) -> float:
        return float(self.speed_var.get().rstrip("x"))

    def _read_inputs(self):
        self.inp.ignition = Ignition(self.ign_var.get())
        self.inp.light_switch = LightSwitch(self.switch_var.get())
        self.inp.ambient_lux = 10 ** self.lux_pos.get()
        self.inp.doors = [v.get() for v in self.door_vars]
        self.inp.battery_v = self.batt_var.get()
        self.lux_label.config(text=f"{self.inp.ambient_lux:,.0f} lux".replace(",", "."))
        self.batt_label.config(text=f"{self.inp.battery_v:.1f} V")

    def _sample(self):
        b, o, h = self.bcm, self.bcm.out, self.hist
        h["t"].append(b.t)
        h["lux"].append(self.inp.ambient_lux)
        h["mode"].append(MODE_LEVEL[b.mode])
        h["low"].append(o.low_beam)
        h["park"].append(o.park_lamps)
        h["puddle"].append(o.puddle)
        h["interior"].append(o.interior)
        h["ind"].append(1.0 if o.indicators else 0.0)
        h["ign"].append(int(self.inp.ignition) / 2)
        h["locked"].append(1.0 if b.locked else 0.0)
        h["door"].append(1.0 if any(self.inp.doors) else 0.0)
        h["dark"].append(1.0 if b.dark else 0.0)

    def _drain_log(self):
        for t, msg in self.bcm.drain_log():
            self.log_list.insert("end", f"[{t:7.1f} s] {msg}")
        if self.log_list.size() > 500:
            self.log_list.delete(0, self.log_list.size() - 500)
        self.log_list.yview_moveto(1.0)

    def _update_status(self):
        b, o, s = self.bcm, self.bcm.out, self.status_vars
        self.mode_label.config(text=MODE_LABELS[b.mode])
        s["timer"].set(f"{b.mode_timer:5.1f} s" if b.mode in (Mode.COMING_HOME, Mode.LEAVING_HOME) else "—")
        s["locks"].set("TRAVADO" if b.locked else "DESTRAVADO")
        s["relock"].set(f"{b.relock_timer:5.1f} s" if b.relock_timer > 0 else "—")
        s["interior"].set(f"{b.interior_timer:5.1f} s" if b.interior_timer > 0 else "—")
        s["sensor"].set(("ESCURO" if b.dark else "CLARO") + f"  ({self.inp.ambient_lux:.0f} lux)")
        s["saver"].set("ATUOU" if (b.saver_tripped or b.interior_saver_tripped) else "ok")
        s["outputs"].set(f"farol {o.low_beam:.0%}  lant. {o.park_lamps:.0%}  "
                         f"chão {o.puddle:.0%}  int. {o.interior:.0%}")
        s["simt"].set(f"{b.t:7.1f} s" + ("  (PAUSADO)" if self.paused else ""))

    def _update_plots(self):
        h = self.hist
        t_all = list(h["t"])
        now = self.bcm.t
        i0 = max(0, bisect.bisect_left(t_all, now - self.window) - 1)
        x = [ti - now for ti in t_all[i0:]]

        def series(key):
            return list(itertools.islice(h[key], i0, None))

        self.l_lux.set_data(x, series("lux"))
        self.l_dark_th.set_ydata([self.cfg.dark_threshold_lux] * 2)
        self.l_light_th.set_ydata([self.cfg.light_threshold_lux] * 2)
        self.l_mode.set_data(x, series("mode"))
        for lines in (self.out_lines, self.in_lines):
            for key, (line, off) in lines.items():
                line.set_data(x, [off + v * 0.9 for v in series(key)])

        if self._plot_bg is None:
            self.plot_canvas.draw()  # primeiro desenho completo; _on_draw salva o fundo
        else:
            self.plot_canvas.restore_region(self._plot_bg)
            self._blit_lines()

    # ================================================================== carro
    def _draw_car(self):
        cv, o, b = self.car, self.bcm.out, self.bcm
        cv.delete("all")
        W, H = 320, 330
        daylight = min(1.0, max(0.0, math.log10(max(self.inp.ambient_lux, 1)) / 3.3))
        bg = lerp_color("#070b16", "#a9bccc", daylight)
        cv.create_rectangle(0, 0, W, H, fill=bg, outline="")

        x1, x2, y1, y2 = 105, 215, 60, 290
        cx = (x1 + x2) / 2

        # facho do farol
        if o.low_beam > 0.01:
            beam = lerp_color(bg, "#fff3b0", o.low_beam * 0.55)
            for hx in (x1 + 18, x2 - 18):
                cv.create_polygon(hx - 10, y1 + 6, hx + 10, y1 + 6, hx + 45, 0, hx - 45, 0,
                                  fill=beam, outline="")
        # luz de chão (retrovisores)
        if o.puddle > 0.01:
            pc = lerp_color(bg, "#cfe0ff", o.puddle * 0.6)
            cv.create_oval(x1 - 75, 115, x1 - 15, 165, fill=pc, outline="")
            cv.create_oval(x2 + 15, 115, x2 + 75, 165, fill=pc, outline="")

        body = "#3b4b5e"
        round_rect(cv, x1, y1, x2, y2, 28, fill=body, outline="#1b2430", width=2)

        # cabine (vidros) iluminada pela luz interna
        cabin = lerp_color("#1a2230", "#ffd27a", o.interior)
        round_rect(cv, x1 + 12, 115, x2 - 12, 245, 14, fill=cabin, outline="#11161f")
        cv.create_line(x1 + 14, 180, x2 - 14, 180, fill="#11161f", width=2)  # coluna B

        # retrovisores
        cv.create_rectangle(x1 - 12, 120, x1, 132, fill=body, outline="#1b2430")
        cv.create_rectangle(x2, 120, x2 + 12, 132, fill=body, outline="#1b2430")

        # portas: (lado, y_dobradiça, y_fim)
        doors = [(-1, 122, 178), (1, 122, 178), (-1, 182, 238), (1, 182, 238)]
        ang = math.radians(55)
        for open_, (side, ya, yb) in zip(self.inp.doors, doors):
            hx = x1 if side < 0 else x2
            L = yb - ya
            if open_:
                ex, ey = hx + side * L * math.sin(ang), ya + L * math.cos(ang)
                cv.create_line(hx, ya, ex, ey, fill="#5b6f86", width=7, capstyle="round")
            else:
                cv.create_line(hx, ya, hx, yb, fill="#1b2430", width=2)

        # faróis
        head = lerp_color("#56606a", "#fffbe0", max(o.low_beam, o.park_lamps * 0.35))
        cv.create_oval(x1 + 6, y1 + 4, x1 + 34, y1 + 16, fill=head, outline="#222")
        cv.create_oval(x2 - 34, y1 + 4, x2 - 6, y1 + 16, fill=head, outline="#222")
        # lanternas traseiras
        tail = lerp_color("#4a1c1c", "#ff2a2a", o.park_lamps)
        cv.create_rectangle(x1 + 6, y2 - 12, x1 + 34, y2 - 4, fill=tail, outline="#222")
        cv.create_rectangle(x2 - 34, y2 - 12, x2 - 6, y2 - 4, fill=tail, outline="#222")

        # piscas (cantos + retrovisores)
        ind = "#ffb000" if (o.indicators or self._ind_seen) else "#5a4a20"
        for px, py in [(x1 + 2, y1 + 22), (x2 - 2, y1 + 22), (x1 + 2, y2 - 22), (x2 - 2, y2 - 22),
                       (x1 - 6, 126), (x2 + 6, 126)]:
            cv.create_oval(px - 5, py - 5, px + 5, py + 5, fill=ind, outline="#222")

        fg = "#ffffff" if daylight < 0.5 else "#111111"
        cv.create_text(cx, y2 + 22, fill=fg, font=("Segoe UI", 11, "bold"),
                       text=("TRAVADO" if b.locked else "DESTRAVADO"))
        cv.create_text(8, 8, anchor="nw", fill=fg, font=("Segoe UI", 9),
                       text=f"Ignição: {Ignition(self.ign_var.get()).name}   "
                            f"Comando: {self.switch_var.get()}")

    # ================================================================== ações
    def _toggle_pause(self):
        self.paused = not self.paused
        self.pause_btn.config(text="Continuar" if self.paused else "Pausar")

    def _reset(self):
        self.bcm.reset()
        for d in self.hist.values():
            d.clear()
        self._next_sample = 0.0
        self._acc = 0.0
        self.log_list.delete(0, "end")
        self._update_plots()

    def _on_close(self):
        self.root.after_cancel(self._after_id)
        self.root.destroy()


def main():
    root = tk.Tk()
    SimulatorApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
