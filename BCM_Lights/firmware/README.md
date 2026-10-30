# Firmware ESP32: BCM Lights

O ESP32 roda a lógica do BCM (porte em C++ de `bcm/logic.py`) a cada 20 ms. Ele cria a própria rede Wi-Fi e serve a interface de controle pelo navegador.

## Gravar a placa

Rode na pasta `firmware/`, com o ESP32 DevKit ligado na USB:

```
pio run -t upload       # 1) firmware
pio run -t uploadfs     # 2) interface web (pasta data/) na LittleFS
pio device monitor      # serial a 115200 (opcional, mostra os eventos)
```

Se o upload não começar sozinho, segure o botão **BOOT** da placa quando aparecer `Connecting...`.

Os dois passos são independentes. Se você mudar só a página (`data/`), basta o `uploadfs`. Se mudar só o código C++, basta o `upload`.

## Usar

1. No celular ou PC, conecte na rede **`BCM-Lights`**, senha **`bcm12345`**.
2. A página deve abrir sozinha (portal cativo). Se não abrir, acesse **http://192.168.4.1** ou **http://bcm.local**.
3. Se o celular avisar "rede sem internet", escolha **manter conectado**.

Vários aparelhos podem abrir a página ao mesmo tempo, e todos ficam sincronizados. A configuração é gravada na flash e continua lá depois de desligar a placa.

Nome e senha da rede ficam em `src/web.h`.

## Ligações da bancada

### Saídas (LED + resistor de 220–330 Ω até o GND)

| GPIO | Função | LED sugerido |
|---|---|---|
| 25 | Farol baixo (PWM) | branco |
| 26 | Lanternas (PWM) | vermelho |
| 27 | Luz de chão (PWM) | azul ou branco |
| 14 | Luz interna (PWM) | amarelo |
| 32 | Pisca esquerdo | âmbar |
| 33 | Pisca direito | âmbar |
| 2 | LED da placa: aceso = página conectada, piscando = ninguém conectado | (já vem na placa) |

```
GPIO ──[330 Ω]──►|── GND
                LED (perna comprida = anodo, do lado do resistor)
```

O PWM tem 12 bits a 5 kHz com correção de gama, então os fades parecem lineares a olho nu.

### Entradas (chave ou botão entre o GPIO e o GND)

Os pinos usam o pull-up interno. **Fechado com o GND = ativo.** Pino sem nada ligado = inativo, então dá para montar aos poucos.

| GPIO | Função | Tipo |
|---|---|---|
| 16 | Porta do motorista | chave (fechada = porta aberta, como o interruptor de porta real) |
| 17 | Porta do passageiro | chave |
| 18 | Porta traseira esquerda | chave |
| 19 | Porta traseira direita | chave |
| 21 | Ignição ON | chave |
| 22 | Ignição ACC | chave |
| 23 | Travar | botão |
| 13 | Destravar | botão |
| 4 | Lampejo do farol alto | botão |

Entradas físicas e a página trabalham juntas:
- **Portas:** abertas se estiverem abertas na página **ou** na chave.
- **Ignição:** vale a posição mais alta entre as duas.
- **Botões:** funcionam como os da página.

Para ignorar os GPIOs, desmarque "Ler entradas físicas" na aba Configuração.

### Sensor de luz (opcional)

```
3V3 ──[LDR]──┬── GPIO 34
             └──[10 kΩ]── GND
```

Na página, em "Luminosidade externa", escolha **Sensor LDR**. A conversão para lux é aproximada (curva típica de um GL5528). Para calibrar, ajuste `LDR_R10_OHMS` e `LDR_GAMMA` em `src/pins.h`.

> Pinos evitados de propósito: 0, 5, 12 e 15 (definem o modo de boot), 6 a 11 (flash interna) e os do ADC2, que não funcionam com o Wi-Fi ligado.

## Estrutura

| Caminho | Conteúdo |
|---|---|
| `lib/bcm_logic/` | Lógica do BCM em C++11 puro, sem Arduino. Mesma máquina de estados do Python |
| `src/main.cpp` | Tarefa do BCM (20 ms, núcleo 1), fila de comandos, gravação na NVS |
| `src/hw_io.cpp` | GPIOs com debounce, LDR, PWM (LEDC) |
| `src/web.cpp` | Wi-Fi AP, portal cativo, mDNS, HTTP e WebSocket |
| `src/pins.h` | Mapa de pinos |
| `data/` | Interface web (HTML/CSS/JS sem dependências externas) |
| `host_test/` | Executa a lógica C++ no PC, para comparar com o Python |

## Protocolo WebSocket (`/ws`)

**ESP32 → página:**
- `{"type":"state", ...}` 10 vezes por segundo: modo, timers, saídas e entradas (efetivas, da página e físicas).
- `{"type":"config","fields":[{k,t,min,max,v}],"io":{...}}` ao conectar e a cada mudança de configuração.
- `{"type":"logs","items":[[t,"mensagem"],...]}` com os eventos do BCM (ao conectar, vêm os últimos 40).

**Página → ESP32:**

| Mensagem | Efeito |
|---|---|
| `{"cmd":"lock"}`, `"unlock"`, `"flash"`, `"reset"`, `"defaults"` | ações |
| `{"cmd":"input","ign":0-2}` / `"sw":"OFF|PARK|LOW|AUTO"` / `"lux":5` / `"batt":12.6` / `"door":[i,true]` | entradas virtuais |
| `{"cmd":"config","key":"coming_home_time_s","value":60}` | parâmetro do BCM |
| `{"cmd":"sim","speed":5}` / `{"cmd":"sim","paused":true}` | acelerar ou pausar os timers |
| `{"cmd":"io","lux_src":"web|ldr"}` / `{"cmd":"io","phys":false}` | origem das entradas |
