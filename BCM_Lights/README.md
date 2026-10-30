# BCM Lights: simulador de Coming Home / Leaving Home

Simulação em Python da lógica de iluminação de cortesia de um BCM (Body Control Module).

## Executar

```
pip install -r requirements.txt
python sim_gui.py
```

Testes da lógica:

```
python -m unittest discover -s tests -v
```

Interface web no PC, com a lógica Python fingindo ser o ESP32 (mesma página e mesmo protocolo do firmware):

```
python sim_server.py          # http://localhost:8080
```

Firmware do ESP32 (gravação, ligações da bancada e protocolo): veja [firmware/README.md](firmware/README.md).

Conferir se o C++ do firmware dá exatamente o mesmo resultado que o Python:

```
python tools/check_parity.py --seeds 20 --steps 50000
```

## Estrutura

| Arquivo | Conteúdo |
|---|---|
| `bcm/config.py` | Parâmetros configuráveis (tempos, limiares, habilitações): a "codificação" do módulo |
| `bcm/logic.py` | Máquina de estados pura, avançada por `step(dt, inputs)`. **Referência** para o firmware |
| `sim_gui.py` | Painel de controle desktop (Tkinter + Matplotlib) |
| `sim_server.py` | Servidor que imita o ESP32 para desenvolver a interface web no PC |
| `firmware/` | Projeto PlatformIO do ESP32 (lógica C++, Wi-Fi, servidor web, PWM, GPIO, interface em `data/`) |
| `tools/check_parity.py` | Compara C++ e Python bit a bit em cenários aleatórios |
| `tests/test_logic.py` | Testes dos cenários principais |

Ao mudar a lógica, altere primeiro o Python, rode os testes, depois porte para `firmware/lib/bcm_logic` e rode o `check_parity.py`.

## Funções simuladas

- **Coming Home**: ao desligar a ignição com o comando em AUTO e escuro, ou com lampejo do farol alto até 10 s depois. O timer fica parado enquanto há porta aberta, e travar o carro reinicia o tempo.
- **Leaving Home**: ao destravar no escuro, acende farol baixo, lanterna e luz de chão. Apaga ao travar, ao ligar a ignição (passa para o modo normal) ou quando o tempo acaba.
- **Re-travamento automático**: destravou e nenhuma porta foi aberta, o carro trava sozinho.
- **Luz interna**: retardo após fechar a porta, acende ao destravar, fade (theater dimming).
- **Piscas de confirmação**: 1 piscada ao travar e 2 ao destravar. Travar com porta aberta é recusado.
- **Sensor de luz** com histerese (50/150 lux) e filtro de tempo.
- **Proteção de bateria**: corta lanterna, farol ou luz interna esquecidos, e bloqueia as funções de cortesia com bateria baixa.

## Exemplo de cenário

1. Velocidade 10x, luminosidade em **Noite**, ignição **ON**, comando em **AUTO**.
2. Ignição **OFF**: entra em Coming Home.
3. Abra e feche a porta do motorista e clique em **Travar**: o timer reinicia e os piscas confirmam.
4. Espere apagar e clique em **Destravar**: entra em Leaving Home.
