# CLP ESP32

CLP para automação residencial baseado em ESP32, programado em **Ladder pelo navegador**.

- 10 entradas digitais (I0–I9), 6 saídas digitais (Q0–Q5), 5 entradas analógicas (AI0–AI4)
- Scan de 10 ms no núcleo 1; Wi-Fi e servidor web no núcleo 0
- Editor Ladder com monitoramento ao vivo (fluxo de energia em verde)
- Forçamento de entradas/saídas para testar sem fiação
- Bits internos M, registradores D (retentivos), temporizadores TON/TOF/TP, contadores CTU/CTD, comparação e MOV

## Gravando

Requer [PlatformIO](https://platformio.org/) (extensão do VS Code ou CLI).

```sh
pio run -t upload      # firmware
pio run -t uploadfs    # interface web (pasta data/)
pio device monitor     # log serial, 115200
```

Nos primeiros boots, sem Wi-Fi configurado, o CLP abre o ponto de acesso **CLP-ESP32** (senha `clp12345`).
Conecte nele e acesse **http://192.168.4.1**. Na aba **Configuração**, informe a rede da casa. Depois disso
ele fica acessível em **http://clp.local** (ou pelo IP mostrado no monitor serial).

## Pinagem (ESP32 DevKit / WROOM-32)

| Sinal | GPIO |
|---|---|
| I0–I9 | 4, 5, 13, 14, 16, 17, 18, 19, 21, 22 |
| Q0–Q5 | 23, 25, 26, 27, 33, 2 (Q5 = LED da placa) |
| AI0–AI4 | 36 (VP), 39 (VN), 34, 35, 32 |

Para mudar, edite `src/config.h`. Analógicas precisam ficar no ADC1 (GPIO 32–39), porque o ADC2 não funciona com o Wi-Fi ligado.

### Ligação para testes (3,3 V)

- **Entrada digital**: botão entre o GPIO e **3,3 V**. O pull-down interno vem ativado por padrão.
- **Saída digital**: LED + resistor de 330 Ω para o GND, ou módulo relé com entrada compatível com 3,3 V.
- **Analógica**: potenciômetro de 10 kΩ entre 3,3 V e GND, cursor no GPIO. Faixa útil de ~0 a 3100 mV.

> ⚠️ **Nunca ligue 24 V direto no ESP32.** As entradas de 24 V precisam de optoacoplador (ex.: PC817 com
> resistor de ~2,2 kΩ). Nesse caso o opto normalmente puxa o pino para GND: marque **pull-up** e **inverter
> entrada** na aba Configuração.

## Endereços

| Endereço | Significado |
|---|---|
| `I0`… / `Q0`… | entradas / saídas digitais |
| `M0`…`M127` | bits internos |
| `S0` / `S1` / `S2` | 1º scan / pisca 1 Hz / sempre ligado |
| `AI0`… | entrada analógica em mV (filtrada) |
| `D0`…`D31` | registradores inteiros; os valores alterados pela web ficam gravados na flash |
| `T0`… / `T0.ET` | saída do temporizador / tempo decorrido (ms) |
| `C0`… / `C0.CV` | saída do contador (CV ≥ PV) / valor atual |

Um programa de exemplo com selo, minuteria de escada, termostato e pulsador liga/desliga está em
`examples/exemplo-residencial.json` (importe pela aba Ladder).

## Estrutura

```
src/config.h      pinagem e tamanhos de memória
src/io_hal.*      acesso ao hardware (troque aqui para expansores I2C na placa própria)
src/plc.*         compilador Ladder (JSON → bytecode) e execução do scan
src/web.*         API REST + WebSocket
src/settings.*    configuração e retentivos na NVS
data/             interface web (HTML/CSS/JS sem dependências)
```

API: `GET/POST /api/program`, `GET/POST /api/config`, `GET /api/info`, WebSocket `/ws` (estado a cada
200 ms; comandos `run`, `stop`, `force`, `setd`, `clrmax`).

## Limitações atuais

- Sem autenticação na interface web. Use só na rede local, sem expor na internet.
- Bits M, temporizadores e contadores não são retentivos (zeram ao reiniciar ou ao ir para RUN).
- Não use para funções de segurança de pessoas.
