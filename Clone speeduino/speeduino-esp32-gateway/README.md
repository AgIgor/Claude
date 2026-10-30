# Speeduino Live: gateway WiFi com ESP32

O ESP32 lê os dados em tempo real do Speeduino pela **serial secundária** e serve um painel web no estilo TunerStudio, acessível pelo celular, tablet ou notebook. Não precisa de internet nem de app.

```
Speeduino (serial secundária) --UART 115200--> ESP32 --WiFi/WebSocket--> navegador
```

## O que o painel mostra

- **Painel:** 8 mostradores analógicos (o mesmo front page do TunerStudio: RPM, TPS, tempo de injeção, duty cycle, MAP, temp. do ar, temp. do motor e enriquecimento total) e 12 mostradores secundários (barra ou digital). Clique em qualquer um para trocar o canal ou o estilo; há mais de 80 opções.
- **Indicadores:** todos os do `[FrontPage]` do `.ini` (sincronismo, partida, ASE, WUE, DFCO, limitadores, proteções do motor, ventoinha, A/C, saídas programáveis, SD etc.).
- **Gráfico:** 4 canais quaisquer em tempo real, com janela de 10 a 120 s e pausa.
- **Canais:** todos os 130 bytes decodificados (181 canais, incluindo bits e canais calculados), com filtro.
- **Datalog:** grava na memória do navegador e exporta em `.msl` (MegaLogViewer) ou `.csv`.
- **Status:** dados do gateway e o último quadro bruto em hexadecimal.
- **Modo demo:** simula um motor, para testar sem a ECU. Abra `data/index.html` direto no navegador ou use `http://speeduino.local/?demo`.

## Ligação

| Speeduino (Mega 2560) | ESP32 |
|---|---|
| TX3, pino 14 | GPIO16 (RX2), **com divisor de tensão** |
| RX3, pino 15 | GPIO17 (TX2) |
| GND | GND |

- O Mega trabalha em 5 V e o ESP32 em 3,3 V. No sinal TX3 → GPIO16 use um divisor (por exemplo 1 kΩ em série e 2 kΩ para o GND) ou um conversor de nível.
- O sentido GPIO17 → RX3 costuma funcionar direto, mas um conversor de nível bidirecional é a opção mais segura.
- Em STM32 e Teensy a serial secundária é a `Serial2` da ECU, que já trabalha em 3,3 V.
- Confira no esquema da sua placa se os pinos 14 e 15 estão livres e acessíveis.
- Alimente o ESP32 com 5 V de um regulador step-down ligado aos 12 V do carro, e não direto da bateria.

## Configuração no TunerStudio

Menu **Accessories → Canbus/Secondary Serial IO Interface**:

1. **Enable Secondary Serial:** `Enable`
2. **Secondary Serial Protocol:** `Generic (ini File)`. `Generic (Fixed List)` também funciona.
3. Grave na ECU (Burn) e desligue e ligue a ECU.

> Não use os protocolos `Tuner Studio` ou `msDroid` nessa porta: com eles a ECU não devolve o cabeçalho `r 0x30` que o gateway espera.

O gateway envia o mesmo pedido que o TunerStudio usa (`ochGetCommand = "r\$tsCanId\x30%2o%2c"`, 130 bytes) a cerca de 25 Hz. O TunerStudio continua funcionando normalmente pela USB ao mesmo tempo.

## Compilar e gravar

Requer o PlatformIO (extensão do VS Code ou `pio` na linha de comando).

```bash
pio run -e esp32dev -t upload      # firmware
pio run -e esp32dev -t uploadfs    # página web (pasta data/ -> LittleFS)
pio device monitor                 # opcional: log serial
```

Para ESP32-S3 use `-e esp32-s3`.

## Acesso

1. Conecte na rede WiFi **`Speeduino-Dash`**. A senha é **`speeduino`**.
2. Abra **http://192.168.4.1** ou http://speeduino.local.

Para trocar o nome e a senha da rede, os pinos ou a taxa de polling, edite `src/config.h`. Preenchendo `STA_SSID`/`STA_PASSWORD`, o ESP32 também entra na sua rede de casa ou da oficina, sem desligar o próprio ponto de acesso.

## Configurações do painel

Pelo botão ⚙ você ajusta a unidade de temperatura (°C/°F), o AFR estequiométrico, motor 2 tempos, as faixas do conta-giros e do MAP e os limites da bateria e do flood clear. Esses valores vivem na configuração da ECU e não vêm no fluxo de dados, então copie os mesmos números do seu tune. O layout e as configurações ficam salvos no navegador.

## Arquivos

```
platformio.ini     ambientes esp32dev e esp32-s3
src/config.h       WiFi, pinos e temporização
src/main.cpp       polling da ECU, WebSocket (/ws) e API (/api/status)
data/index.html    painel completo (HTML/CSS/JS sem dependências externas)
```

A decodificação dos canais na página segue o `[OutputChannels]` do `reference/speeduino.ini` da versão 202501. Se você atualizar o firmware da ECU e o layout mudar, atualize as tabelas `SCALARS` e `BITS` em `data/index.html`.
