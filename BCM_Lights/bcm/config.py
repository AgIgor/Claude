"""Parâmetros configuráveis do BCM (equivalente à "codificação" do módulo)."""
from dataclasses import dataclass


@dataclass
class BCMConfig:
    # Sensor de luz (com histerese e filtro de tempo)
    dark_threshold_lux: float = 50.0     # abaixo disso -> escuro
    light_threshold_lux: float = 150.0   # acima disso  -> claro
    dark_debounce_s: float = 2.0         # tempo contínuo para trocar de estado

    # Coming Home (acompanhamento ao sair do carro)
    coming_home_auto: bool = True              # dispara ao desligar ignição em AUTO + escuro
    coming_home_time_s: float = 60.0
    coming_home_flash_window_s: float = 10.0   # janela p/ disparo por lampejo do farol alto
    coming_home_start_on_door_close: bool = True  # timer só corre com todas as portas fechadas
    coming_home_restart_on_lock: bool = True      # travar o carro reinicia o timer

    # Leaving Home (boas-vindas ao destravar)
    leaving_home_enabled: bool = True
    leaving_home_time_s: float = 30.0

    # Travas
    auto_relock_time_s: float = 45.0   # destravou e não abriu porta -> trava sozinho
    lock_flashes: int = 1
    unlock_flashes: int = 2
    indicator_period_s: float = 0.6    # um ciclo aceso+apagado do pisca

    # Luz interna
    interior_delay_s: float = 15.0        # retardo após fechar a porta
    interior_unlock_time_s: float = 30.0  # acende ao destravar
    interior_fade_in_s: float = 0.5
    interior_fade_s: float = 2.0          # "theater dimming"

    # Faróis / lanternas
    exterior_fade_in_s: float = 0.3
    exterior_fade_out_s: float = 1.5

    # Proteção de bateria
    battery_saver_time_s: float = 600.0  # corta luzes esquecidas com ignição desligada
    low_battery_v: float = 11.8          # abaixo disso não liga funções de cortesia
