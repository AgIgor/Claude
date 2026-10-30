# Internet Clock v6

Relógio com ESP-01 (1 MB), matriz de 17×5 LEDs WS2812 (85 LEDs), AHT10 (temperatura/umidade) e BH1750 (luz), com página web de configuração e atualização OTA.

## Hardware (igual ao v5)

| Sinal        | GPIO | Observação                              |
|--------------|------|-----------------------------------------|
| LEDs (DIN)   | 0    | matriz 17 × 5 (85 LEDs), bit-bang       |
| LEDs (DIN)   | 3/RX | alternativa com DMA, sem glitches       |
| I²C SDA      | 1    | pino TX — a Serial não é usada          |
| I²C SCL      | 2    |                                         |

Matriz: olhando o relógio de frente, o primeiro LED fica **embaixo à direita**, de baixo para cima, em **zigue-zague**. Os números usam uma fonte de 3×5 pixels; "12:34" ocupa exatamente as 17 colunas.

Se a fita estiver montada de outro jeito, ajuste em **Matriz** na página (primeiro LED à esquerda/em cima, zigue-zague, fita em colunas) e confira com o **Teste de orientação**.

### Piscadas aleatórias (glitches)

No GPIO0 o sinal da fita é gerado pela CPU. As interrupções do Wi-Fi (que não podem ser desligadas) às vezes corrompem bits, e alguns LEDs piscam com cor aleatória. Para resolver:

1. **Mova o fio de dados do GPIO0 para o GPIO3 (RX)** e, na página, em **Matriz → Pino de dados**, escolha *GPIO3 / RX (DMA)*. O sinal passa a ser gerado pelo hardware (I2S/DMA), imune ao Wi-Fi.
2. Se ainda houver falhas, o problema é o nível do sinal (3,3 V numa fita de 5 V). Use um resistor de 330 Ω em série perto do primeiro LED e um capacitor de 1000 µF na alimentação da fita. Se não bastar, coloque um conversor de nível, como o 74AHCT125. O GND do ESP e o da fita precisam estar ligados.

Consumo: com 85 LEDs, use uma fonte de 5 V de pelo menos 2 A. O brilho máximo padrão (150) já limita a corrente.

## Primeira gravação

```
pio run -t upload            # via adaptador serial
```

1. Ao ligar sem Wi-Fi configurado, o display mostra **AP** e o relógio cria a rede **Relogio-XXXX**.
2. Conecte-se a ela. A página de configuração abre sozinha; se não abrir, acesse http://192.168.4.1.
3. Em **Wi-Fi**, clique em *Procurar redes*, escolha a sua e clique em *Conectar*.
4. Depois disso, acesse **http://relogio.local** (ou o IP mostrado na página).

Se o roteador ficar fora do ar por mais de 30 s, o portal abre de novo. Enquanto ninguém estiver conectado ao portal, o relógio tenta voltar ao roteador a cada 60 s.

O que o display mostra enquanto não há hora: cometa **vermelho** contornando a matriz = conectando ao Wi-Fi; **azul** = aguardando o NTP; **AP** = portal de configuração aberto.

## Página web

- **Matriz**: orientação da fita e testes.
- **Telas**: tempo de relógio, temperatura e umidade; transição (fade/varredura); 12/24 h; dois-pontos piscando; casa decimal (`23.5°`, `65.4%`); °F.
- **Cores**: cor fixa por tela, arco-íris ou arco-íris em degradê.
- **Brilho**: automático pelo BH1750 com mínimo/máximo e faixa de lux (curva logarítmica e transição suave), ou manual.
- **Modo noturno**: pelo sensor de luz ou por horário, com cor e brilho próprios e opção de mostrar só o relógio.
- **Hora**: fuso horário com horário de verão automático (strings POSIX) e servidor NTP.
- **Sensores**: calibração de temperatura e umidade.
- **MQTT**: opcional.
- **Sistema**: nome na rede, senha de administrador, OTA, reiniciar, restaurar padrão.

As mudanças são pré-visualizadas no display na hora. Elas só ficam gravadas depois de clicar em **Salvar**.

## Atualização OTA

- **Pela página**: Sistema → escolha `.pio/build/esp01_1m/firmware.bin` → *Enviar firmware*. O progresso aparece nos LEDs.
- **Pelo PlatformIO**: `pio run -e ota -t upload`. Se definir senha de administrador, descomente `upload_flags` no `platformio.ini`.

Limite: o firmware precisa ter menos de ~500 KB para caber o OTA na flash de 1 MB. Hoje ele tem ~418 KB.

## Precisão da temperatura

O AHT10 lê acima da temperatura ambiente porque fica perto do ESP-01 (o rádio Wi-Fi aquece) e dos LEDs. O firmware faz o seguinte:

1. Lê o sensor a cada 10 s (configurável), em vez de continuamente, para reduzir o autoaquecimento.
2. Aplica média móvel nas leituras.
3. Aplica um ajuste de calibração em °C.
4. **Recalcula a umidade para a temperatura corrigida** (fórmula de Magnus). Se o sensor está 3 °C mais quente que o ar, a umidade relativa lida fica vários pontos abaixo da real, e o simples offset de temperatura não corrigiria isso.

Para calibrar: deixe o relógio ligado por 30 min ou mais, meça a temperatura com um termômetro confiável perto do relógio, digite o valor em **Sensores → Temperatura real** e clique em *Calibrar*, depois em *Salvar*. Faça o mesmo com a umidade se tiver um higrômetro de referência.

A melhor correção é física: afaste o AHT10 do ESP-01 e do regulador de tensão, de preferência fora da caixa ou numa área ventilada separada, com furos.

## MQTT

Com o tópico base `relogio`:

- `relogio/status`: `online` / `offline` (retido)
- `relogio/state`: `{"temp":24.1,"humi":55.2,"lux":120,"bright":96,"night":false,"screen":"clock","rssi":-60,"uptime":3600}`
- `relogio/set`: recebe as mesmas chaves da página, por exemplo:
  - `{"bAuto":false,"bMan":40}`
  - `{"cMode":0,"cClock":"#FF0000"}`
  - `{"next":true}` troca de tela
  - `{"test":true}` testa os LEDs
  - `{"save":true}` grava na flash

Wi-Fi, senhas e configurações de MQTT não podem ser alteradas via MQTT.

## Estrutura

```
src/
  main.cpp          setup/loop (sem delay)
  settings.*        configurações na EEPROM com CRC, conversão JSON
  display.*         fonte 3x5, matriz 17x5, telas, cores, brilho, transições
  sensors.*         drivers não bloqueantes AHT10/AHT20 e BH1750
  network.*         Wi-Fi, portal cativo, NTP, mDNS, ArduinoOTA
  mqtt_client.*     MQTT opcional
  web.*             servidor HTTP, API e upload de firmware
  web_page.h        página de configuração (embutida no firmware)
```
