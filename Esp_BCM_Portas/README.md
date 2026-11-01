# Esp_BCM — módulo de carroceria sem fio (ESP32 + ESP-NOW)

Protótipo de BCM automotivo: um ESP32 **central** lê os botões do painel e manda comandos
sem fio para um ESP32 em **cada porta**. A porta executa a ação e responde com **ACK**.
Os módulos são pareados com um botão, sem digitar endereço MAC.

| Porta | Vidro | Trava | Seta | Espelho (ajuste) | Rebatimento |
|---|:-:|:-:|:-:|:-:|:-:|
| Dianteira esq. (DE) / dir. (DD) | ✔ | ✔ | ✔ | ✔ | ✔ |
| Traseira esq. (TE) / dir. (TD) | ✔ | ✔ | – | – | – |

## Estrutura

```
platformio.ini          2 ambientes: central e door
lib/BcmLink/            código compartilhado
  BcmProtocol.h         formato do pacote, comandos, status
  BcmLink.*             ESP-NOW + fila RX + HMAC
  Button.h              debounce / tempo pressionado
  Motor.h               ponte H com modo "segurar" e "pulso", tempo máx. e corrente
src/central/            firmware da central  (config.h = pinos)
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
| Seletor espelho (aberto=E, GND=D) | 33 | | Rebater/abrir (pulso) | 4 |
| Espelho cima/baixo | 34 / 35 ⚠ | | Espelho esq./dir. | 36 / 39 ⚠ |
| Relé do 12 V das portas (saída) | 12 ⚠ | | Ignição (ligada = GND) | 15 |

⚠ GPIO 34, 35, 36 e 39 são somente entrada e **não têm pull-up interno**: coloque um
resistor de 10 kΩ de cada um até o 3V3.

⚠ GPIO 12 precisa estar em nível baixo no boot: acione o relé por MOSFET, com diodo de roda
livre na bobina e pull-down de 10 kΩ no gate. Assim o relé também abre se a central resetar.

Ignição: isole o 12 V com optoacoplador ou transistor; ignição ligada coloca o GPIO 15 em GND.
Sem esse sinal, use `PIN_IGNITION = -1`, mas aí o desligamento depende só da inatividade.

## Ligações da porta

| Função | GPIO | Driver sugerido |
|---|---|---|
| Vidro (A=sobe, B=desce) | 25 / 26 | BTS7960 (motor de vidro puxa 5–15 A, pico > 20 A) |
| Trava (A=trava, B=destrava) | 27 / 13 | BTS7960, DRV8871 ou 2 relés |
| Espelho vertical (A=cima, B=baixo) | 16 / 17 | DRV8871 / L298N |
| Espelho horizontal (A=dir., B=esq.) | 18 / 19 | DRV8871 / L298N |
| Rebatimento (A=rebate, B=abre) | 21 / 22 | DRV8871 / BTS7960 |
| Seta (ativo HIGH) | 23 | MOSFET lógico (IRLZ44N) ou relé |
| Sensor de corrente do vidro (opcional) | 36 | pino IS do BTS7960 / ACS712 |
| Sensor de corrente do rebatimento (opcional) | 39 | idem |
| Jumpers de posição | 32 / 33 | para o GND |

BTS7960: ligue R_EN e L_EN em 5 V, RPWM = pino A, LPWM = pino B.
Espelhos de carro costumam ter 3 fios com um comum entre os dois motores. Neste protótipo
cada motor usa uma ponte H própria; para usar o comum, troque `mirV/mirH` por meia-pontes.

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

## Economia de energia (carro parado)

A central fica ligada direto no 12 V e controla o 12 V das portas por um relé.

- **Desligar:** após `DOOR_OFF_IDLE_MS` (5 min) sem atividade, a central envia `CMD_SLEEP`.
  Cada porta grava o contador anti-replay e responde. Depois disso, ou ao esgotar as
  tentativas, a central abre o relé, desliga o rádio e entra em light sleep.
- **Fica acordada com:** ignição ligada, pisca-alerta, seta, pareamento, qualquer botão
  pressionado ou vidro/espelho em movimento.
- **Acordar:** qualquer botão, a ignição ou o terminal serial. A central fecha o relé, envia
  ping a cada 50 ms e guarda os comandos até cada porta responder (até 2 s). O comando que
  acordou o carro não se perde, mas o primeiro comando sofre o atraso do boot das portas.
- O seletor do espelho não acorda a central, porque fica fechado na posição "direito".

Use um relé automotivo dimensionado pela soma medida dos motores e um fusível por porta.
Mantenha pull-down nas entradas das pontes H: elas flutuam durante o boot da porta.
O terminal aceita `dormir` para testar sem esperar os 5 min.

## Testar sem montar os botões

No monitor serial da central (115200, fim de linha LF):

```
status                     parear / reset
travar                     destravar
vidro dd sobe 3000         vidro te desce          vidro de para
seta e   seta d   seta off alerta
espelho e cima 500         espelho d esq
rebater                    abrir
dormir
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
