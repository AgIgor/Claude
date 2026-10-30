# Esp_BCM — módulo de carroceria sem fio (ESP32 + ESP-NOW)

Protótipo de BCM automotivo: um ESP32 **central** lê os botões do painel e manda comandos
sem fio para um ESP32 em **cada porta**. A porta executa a ação e responde com **ACK**.
Os módulos são pareados com um botão, sem digitar endereço MAC.

| Porta | Vidro | Trava | Seta | Espelho (ajuste) | Rebatimento | Desembaçador |
|---|:-:|:-:|:-:|:-:|:-:|:-:|
| Dianteira esq. (DE) / dir. (DD) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| Traseira esq. (TE) / dir. (TD) | ✔ | ✔ | – | – | – | – |

Os espelhos são comandados pelo botão resistivo original da VW Tiguan 2015 (joystick +
seletor), lido pela central com calibração salva na flash.

## Estrutura

```
platformio.ini          2 ambientes: central e door
lib/BcmLink/            código compartilhado
  BcmProtocol.h         formato do pacote, comandos, status
  BcmLink.*             ESP-NOW + fila RX + HMAC
  Button.h              debounce / tempo pressionado
  Motor.h               ponte H com modo "segurar" e "pulso", tempo máx. e corrente
  MirrorCommon.h        espelho de 3 fios (comum) com 3 relés SPDT
src/central/            firmware da central  (config.h = pinos)
  MirrorSwitch.*        leitura e calibração do botão resistivo de espelhos
src/door/               firmware da porta, igual nas 4 portas (config.h = pinos)
```

Gravação (PlatformIO):

```
pio run -e central -t upload
pio run -e door    -t upload      # repita em cada um dos 4 ESP32 de porta
pio device monitor                # 115200
```

## Como cada porta sabe quem é

Os 4 módulos de porta usam **o mesmo firmware**. A posição vem de 2 jumpers ao GND,
como a "codificação por pino" dos chicotes de carro de verdade:

| GPIO33 (CODE1) | GPIO32 (CODE0) | Posição |
|---|---|---|
| aberto | aberto | DE — dianteira esquerda |
| aberto | GND | DD — dianteira direita |
| GND | aberto | TE — traseira esquerda |
| GND | GND | TD — traseira direita |

## Usar só algumas portas (ex.: só as traseiras)

Em `src/central/config.h`:

```cpp
constexpr bool DOOR_INSTALLED[4] = {false, false, true, true};  // DE, DD, TE, TD
```

Grave a central de novo. As portas usam o firmware de sempre, com os jumpers de TE e TD.
Com essa opção:

- os botões das portas desativadas não são lidos e os pinos ficam livres para outro uso;
- sem dianteiras, seta, pisca-alerta, espelho e rebatimento também ficam desligados,
  e os GPIO 34–39 dispensam os resistores de pull-up;
- a central recusa pareamento de uma posição desativada e avisa no serial (jumper errado);
- o pareamento termina sozinho quando as portas instaladas estiverem pareadas;
- trava/destrava continua indo para todas as portas instaladas.

Para montar só as traseiras na central, ligue apenas PAIR, LED, vidros TE (18/19),
vidros TD (21/22), trava (23) e destrava (25).

## Pareamento (sem saber endereços)

1. **Central**: segure o botão **BOOT (PAIR)** por 3 s e solte. O LED pisca rápido e ela
   fica 60 s em modo pareamento, anunciando-se por broadcast.
2. **Porta**: um módulo novo já entra em pareamento sozinho ao ligar. Se já estiver
   pareado, segure o BOOT dele por 3 s.
3. A porta responde "sou a DD", a central grava o MAC dela nesta posição e devolve o ID da
   rede + a chave. O LED da porta fica **aceso fixo** e o terminal da central mostra `[DD] PAREADA`.
4. Repita para as outras portas dentro dos 60 s (ou todas ao mesmo tempo). Com todas as instaladas pareadas,
   a central sai do modo sozinha.

Trocar um módulo é só parear o novo: ele substitui o anterior naquela posição.
O pareamento fica salvo na flash (NVS) e sobrevive a desligar a energia.

| Botão PAIR | Central | Porta |
|---|---|---|
| clique | imprime status | imprime status |
| 3 s | liga/desliga o modo pareamento | procura a central por 60 s |
| 10 s (LED fica aceso fixo) | **apaga todas as portas** e cria rede/chave nova | esquece a central |

**LED da central:** fixo = todas as portas online · pisca lento = alguma offline ·
pisca rápido = pareando · piscada curta a cada 2 s = nada pareado.
**LED da porta:** fixo = falando com a central · lento = sem sinal · rápido = pareando.

## Ligações da central (entradas ativas em nível baixo: botão para o GND)

| Função | GPIO | | Função | GPIO |
|---|---|---|---|---|
| PAIR (BOOT) | 0 | | Trava | 23 |
| LED status | 2 | | Destrava | 25 |
| Vidro DE sobe/desce | 13 / 14 | | Seta esq. (chave) | 26 |
| Vidro DD sobe/desce | 16 / 17 | | Seta dir. (chave) | 27 |
| Vidro TE sobe/desce | 18 / 19 | | Pisca-alerta (pulso) | 32 |
| Vidro TD sobe/desce | 21 / 22 | | Saída indicador do painel | 5 |
| Botão espelhos: joystick | 34 ⚠ | | Botão espelhos: seletor | 35 ⚠ |
| Desembaçador (GND na posição) | 33 | | Jumper de calibração (GND ao ligar) | 4 |

⚠ Joystick e seletor são saídas resistivas: cada uma vira um divisor de tensão

```
3V3 ──[ pull-up ]──┬── GPIO34 / GPIO35
                   ├── 100 nF ── GND
                   └── saída do botão      (comum do botão no GND)
```

- Use **3V3**, nunca 5 V. Mesmo pull-up que funcionou no Arduino (o divisor é proporcional).
- Só pinos do **ADC1** (32–39): o ADC2 não funciona com o ESP-NOW ligado.

## Botão de espelhos (Tiguan): calibração

A central precisa conhecer o valor do ADC em cada posição. A calibração fica na flash (NVS).
Sem calibração, o botão é ignorado e a central avisa no serial.

Para calibrar: envie `calibrar` no monitor serial **ou** ligue a central com o GPIO4 em GND.
O LED pisca o número do passo e o serial mostra as leituras ao vivo (`x` cancela):

1. Joystick solto (NEUTRO), depois segurar CIMA, BAIXO, ESQUERDA, DIREITA. Cada nível é
   gravado sozinho após 1,5 s parado.
2. Seletor em ESPELHO ESQ, CENTRO, ESPELHO DIR, REBATER: em cada posição, toque o joystick
   para confirmar.

`chave` mostra a calibração e `monitor` liga a leitura contínua (valor bruto → posição).

Funcionamento:

- Seletor em ESQ/DIR: o joystick ajusta aquele espelho. Em CENTRO não faz nada.
- Seletor em REBATER: rebate os dois espelhos. Sair dessa posição abre. Com o seletor em
  REBATER, destravar as portas **não** abre os espelhos.
- Desembaçador: ligado enquanto o botão estiver na posição, desliga após `HEAT_MAX_MS`
  (10 min). A porta desliga sozinha se perder o sinal da central por 3,5 s; a central reenvia
  o estado quando a porta volta.

## Ligações da porta

| Função | GPIO | Driver sugerido |
|---|---|---|
| Vidro (A=sobe, B=desce) | 25 / 26 | BTS7960 (motor de vidro puxa 5–15 A, pico > 20 A) |
| Trava (A=trava, B=destrava) | 27 / 14 | BTS7960, DRV8871 ou 2 relés |
| Espelho 3 fios: comum / vertical / horizontal | 16 / 17 / 18 | 3 relés SPDT (NF=GND, NA=+12 V) |
| Desembaçador do espelho | 13 | relé (~2–3 A por espelho) |
| Rebatimento (A=rebate, B=abre) | 21 / 22 | DRV8871 / BTS7960 |
| Seta (ativo HIGH) | 23 | MOSFET lógico (IRLZ44N) ou relé |
| Sensor de corrente do vidro (opcional) | 36 | pino IS do BTS7960 / ACS712 |
| Sensor de corrente do rebatimento (opcional) | 39 | idem |
| Jumpers de posição | 32 / 33 | para o GND |

BTS7960: ligue R_EN e L_EN em 5 V, RPWM = pino A, LPWM = pino B.
Espelhos de carro têm 3 fios com um comum entre os dois motores (`MIRROR_COMMON_WIRE = true`).
O motor parado recebe o mesmo potencial do comum para não girar junto. Para usar uma ponte H
por motor, coloque `false`: vertical em 16/17 e horizontal em 18/19.
Se um espelho andar ao contrário, ajuste `MIRROR_INVERT_V` / `MIRROR_INVERT_H` (DE, DD).

Relés: módulos de 5 V acionados em LOW nem sempre desligam com os 3,3 V do ESP32. Prefira
módulos com jumper JD-VCC (alimente o lado do relé com 5 V e o VCC do opto com 3V3) ou com
entrada para 3,3 V.

## Comportamento

- **Vidros e ajuste do espelho: "segurar para mover".** Enquanto o botão está pressionado,
  a central reenvia o comando a cada 100 ms. Se a porta ficar **350 ms sem receber**
  (central desligou, rádio caiu), o motor para sozinho. Tem também tempo máximo
  (8 s no vidro) e, com o sensor ligado, parada por corrente (fim de curso/obstáculo).
  Depois de parar por limite, o motor só volta a girar quando o botão for solto.
- **Trava/destrava:** vai para todas as portas pareadas. Com `AUTO_FOLD_ON_LOCK`, travar
  rebate os espelhos e destravar abre.
- **Setas:** quem marca o ritmo do pisca é a central, então no pisca-alerta as duas portas
  piscam juntas. A porta apaga a seta se ficar 1 s sem notícias.
- **Respostas:** cada comando com confirmação é reenviado até 4 vezes (a cada 80 ms) até
  chegar o ACK. A central imprime, por exemplo:

```
>> Travando portas
[DE] TRAVA TRAVAR -> OK
[DD] TRAVA TRAVAR -> OK
[TE] TRAVA TRAVAR -> OK
[TD] TRAVA TRAVAR -> SEM RESPOSTA (4 tentativas)
[DE] TRAVA TRAVAR -> CONCLUIDO          <- RESULT: o pulso do atuador terminou
[TE] ESPELHO CIMA -> NAO SUPORTADO
```

`OK` = recebeu e executou (ou começou a executar). Ações com duração (trava, rebatimento) e
paradas automáticas do vidro também mandam um `RESULT` quando terminam.

## Testar sem montar os botões

No monitor serial da central (115200, fim de linha LF):

```
status                     parear / reset
travar                     destravar
vidro dd sobe 3000         vidro te desce          vidro de para
seta e   seta d   seta off alerta
espelho e cima 500         espelho d esq
rebater                    abrir
desemb on                  desemb off
calibrar                   chave        monitor
```

## Protocolo e segurança

- ESP-NOW no canal Wi-Fi 1 (`bcm::kChannel`), sem roteador. Latência típica < 5 ms.
- Pacote de 44 bytes: ID da rede, contador, reqId, comando, argumento, status e estado da porta.
- Todo comando é autenticado com **HMAC-SHA256** (truncado a 8 bytes) usando uma chave de
  128 bits gerada pela central. Um ESP32 de outro carro ou um "clonador" não consegue destravar.
- **Anti-replay:** contador crescente salvo na flash. Gravar um "destravar" e retransmitir
  depois não funciona.

Limitações do protótipo (resolver antes de ir para um carro):

- A chave vai em texto puro durante os 60 s de pareamento. Em produção, use troca de chaves
  ECDH (mbedTLS já vem no ESP32) ou pareamento por cabo/NFC.
- Não existe antiesmagamento de verdade: o vidro só para por corrente se o sensor estiver
  ligado e calibrado. Subida automática ("one-touch") **só** com antiesmagamento.
- Alimentação automotiva: use um conversor buck 12 V → 5 V com proteção contra
  *load dump*/transientes (ISO 7637), TVS e fusível por motor. O 3V3 do DevKit não
  aguenta ruído de motor; separe o GND de potência do GND de sinal.
- Portas de lata atenuam bastante o 2,4 GHz. Se o alcance for curto, use antena externa
  (ESP32-WROOM-32U) ou ative o modo Long Range (`WIFI_PROTOCOL_LR`) nos dois lados.
- Para funções críticas, um BCM real mantém fio/CAN de reserva. Aqui, a perda do rádio só
  garante parada segura (motores param, seta apaga).
