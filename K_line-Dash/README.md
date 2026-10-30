# Fiesta Dash: OBD-II CAN → ESP32 + MCP2515 → Web

Dashboard web em tempo real para **Ford Fiesta 2004 1.0 (Zetec Rocam)** com ECU
**Magneti Marelli IAW 4AFR.BR** (Ford 5S65-12A650-AC).

O conector OBD do carro (`conector.jpg`) tem terminais nos pinos **4, 5, 6, 7, 9, 14 e 16**:
há **CAN** (pinos 6 e 14) **e K-line** (pino 7). Este firmware usa o CAN (ISO 15765-4) por um
módulo **MCP2515**. O ESP32 cria uma rede Wi-Fi própria e serve o painel no navegador do
celular.

> A versão por **K-line** (pino 7) está em [`backup_kline/`](backup_kline/), um projeto
> PlatformIO completo e independente. Qual das duas a ECU do motor usa depende do carro:
> veja "Qual versão usar" abaixo.

### Qual versão usar

Com a ignição ligada, meça em relação ao pino 4 ou 5 (GND):

| Medição | Indica |
|---|---|
| Pinos 6 e 14 com **~2,5 V** cada | rede CAN ativa |
| Pino 7 com **~12 V** (sem nada ligado) | K-line presente, com pull-up da ECU |

Comece por esta versão CAN: a detecção só **escuta** o barramento, sem risco para o carro, e
o log mostra se há tráfego e se alguma ECU responde ao OBD. Se o CAN tiver tráfego mas
nenhuma resposta em 7DF/7E0, a ECU do motor provavelmente está só na K-line. Nesse caso,
use o firmware de `backup_kline/`.

## Modo escuta: rede ECU ↔ painel de instrumentos

Neste Fiesta, o CAN do conector OBD (pinos 6 e 14) **vai só até o painel de instrumentos**, e o
painel tem **outra rede CAN, direto com a ECU**. A K-line do OBD também não chega à ECU por ali.
O painel funciona como intermediário (gateway). Por isso o OBD pelo conector recebe sempre a
mesma resposta, e quem responde é o painel, não a ECU.

A ECU manda para o painel, sozinha e o tempo todo, as mensagens que ele mostra (rotação,
temperatura, velocidade, luzes, e os dados do modo de teste do painel). O **modo escuta** lê
essas mensagens **sem transmitir nada**: o MCP2515 fica em *listen-only* e não manda nem o ACK.
Assim não há risco de atrapalhar a comunicação entre ECU e painel.

### Ligação

1. Ache, no conector do painel, o **par trançado que vem da ECU** (não o que vem do OBD).
2. Com a ignição ligada, cada fio deve marcar **~2,5 V** em relação ao GND. Com tráfego, o
   CAN-H fica um pouco acima (2,6 a 3,5 V) e o CAN-L um pouco abaixo (1,5 a 2,4 V).
3. Ligue **em paralelo**, sem cortar: CAN H do módulo no CAN-H e CAN L no CAN-L. Use
   derivações curtas (de preferência menos de 30 cm, com os fios trançados) e solda ou
   conector derivador. **Sem o jumper J1** (120 Ω).
4. Se não aparecer tráfego, inverta CAN H e CAN L: no modo escuta isso não causa problema.

### Uso

1. Diagnóstico → Modo **"Escuta da rede (só ouve)"** → Aplicar. Ou, na aba **Rede**,
   **Ativar modo escuta**.
2. A aba **Rede** mostra todos os IDs que circulam, com a frequência (Hz) e os 8 bytes de
   cada um. **Fundo colorido** = o byte mudou agora. **Apagado** = nunca mudou.
3. **Para achar um sinal, mude uma coisa de cada vez:**
   - **Rotação:** acelere e solte. Procure 2 bytes que sobem e descem juntos. Em Ford é
     comum ser `bytes × 0,25` (÷4).
   - **Temperatura:** acompanhe o motor esquentando. Procure 1 byte que sobe devagar. É
     comum ser `byte − 40`.
   - **Velocidade:** precisa andar com o carro, ou suspender as rodas de tração.
   - **Modo de teste do painel:** os valores que ele mostra vêm dessas mensagens. Compare o
     número do painel com o byte para achar a fórmula.
4. Toque no byte para ver o valor em decimal, em 2 bytes (big e little endian), o mín/máx e
   um gráfico. Escolha **"Usar como"**, ajuste **Mult.** e **Soma** até o resultado bater e
   toque em **Adicionar sinal**. O valor passa a aparecer no **Painel**.
5. **Gravar / Marcar / Baixar** salva um arquivo `.txt` com toda mudança de cada ID e as
   marcas que você escrever (ex. "3000 rpm", "ventoinha ligou"). Mande esse arquivo para
   análise se não achar algum sinal.

Formato dos sinais (aba Rede → Sinais mapeados), uma linha por sinal:

```
# chave,ID,byte,tamanho,ordem,mult,soma[,máscara]
rpm,201,0,2,B,0.25,0
ect,420,0,1,B,1,-40
```

Chaves: `rpm spd ect iat tps map load adv o2 stft ltft vecu`. Ordem: `B` (big-endian), `L`
(little-endian), `BS` ou `LS` (com sinal). Máscara opcional em hexadecimal (ex. `0FFF` para 12
bits). *Os IDs do exemplo são de outros Ford e servem só de ilustração: confirme na tabela.*

No modo escuta, ler e apagar falhas, o console e a sondagem ficam desativados, porque
precisariam transmitir. Para isso, volte ao modo Diagnóstico OBD.

## Transmissão contínua pelo Wi-Fi

A página abre uma conexão **WebSocket** (porta 81) com o ESP32, que **empurra cada quadro CAN e
cada valor no instante em que chegam** (o ID 201 vem a ~50 Hz), em vez de a página perguntar 3 a
4 vezes por segundo. Funciona em qualquer navegador, inclusive o da multimídia, no mesmo
endereço `http://192.168.4.1`. A pílula **"ao vivo"** no topo indica que a transmissão está
ativa. Se ela cair, a página volta sozinha às consultas normais e tenta reconectar.

Com a transmissão ativa, a **gravação guarda todos os quadros**, com o tempo medido no ESP32 (em
ms), no mesmo formato de arquivo de antes.

Protocolo (texto, várias linhas por mensagem):

| Linha | Significado |
|---|---|
| `F <ms> <ID> <dlc> <dados>` | quadro CAN recebido (modo escuta) |
| `V <ms> <chave> <valor>` | valor novo (rpm, ect, ...) |
| `J {json}` | estado completo, 2× por segundo (o mesmo de `/api/data`) |

## Hardware

| Item | Observação |
|---|---|
| ESP32 DevKit (WROOM) | qualquer placa `esp32dev` |
| Módulo **MCP2515 + TJA1050** (o azul comum) | controlador CAN (SPI) + transceptor |
| Conversor de nível **BSS138 4 canais** | o módulo trabalha em 5 V, o ESP32 em 3,3 V (veja abaixo) |
| Step-down 12 V → 5 V (MP1584 / LM2596) | alimenta ESP32 (VIN) e módulo CAN |
| Fusível 1 A + diodo 1N4007 | proteção na entrada de +12 V |
| Plugue OBD-II macho de 16 pinos | |

### ⚠ 5 V × 3,3 V

O TJA1050 do módulo precisa de **5 V**, e com isso o MCP2515 também roda em 5 V: o pino
**SO (MISO) sai com 5 V**, o que pode danificar o ESP32. Escolha uma das opções:

1. **Conversor de nível BSS138** (recomendado, sem solda no módulo): passe SCK, SI, SO e CS
   por ele. Lado HV em 5 V, lado LV em 3,3 V. Mantenha `CAN_SPI_HZ 1000000` (1 MHz) em
   `src/config.h`, porque o BSS138 não é rápido.
2. **Modificar o módulo:** alimentar só o MCP2515 (pino 18, VDD) com 3,3 V e deixar o TJA1050
   em 5 V. Assim o SPI fica em 3,3 V e dá para ligar direto, com até 8 MHz.

**Não** alimente o módulo inteiro com 3,3 V: o TJA1050 não funciona direito abaixo de 4,75 V.

### Ligações

```
OBD 16 (+12V) ─ fusível 1A ─ 1N4007 ─ step-down ─ +5V ─┬─ VIN do ESP32
                                                        ├─ VCC do módulo MCP2515
                                                        └─ HV do conversor BSS138
ESP32 3V3 ─────────────────────────────────────────────── LV do conversor BSS138

ESP32            BSS138 (LV ↔ HV)          Módulo MCP2515
GPIO18 (SCK) ──── LV1 ↔ HV1 ─────────────── SCK
GPIO23 (MOSI) ─── LV2 ↔ HV2 ─────────────── SI
GPIO19 (MISO) ─── LV3 ↔ HV3 ─────────────── SO
GPIO5  (CS) ───── LV4 ↔ HV4 ─────────────── CS
GND ───────────────── GND ───────────────── GND ── OBD 4 e 5
                                              INT: não usado

Módulo MCP2515 (borne)       OBD
CAN H ─────────────────────── pino 6
CAN L ─────────────────────── pino 14
```

### Antes de ligar no carro

- **Retire o jumper J1 (terminação de 120 Ω) do módulo.** A rede do carro já é terminada nas
  pontas, e uma terceira terminação no meio só atrapalha.
- **Confira o cristal** (componente metálico): `8.000` = 8 MHz (o mais comum) ou
  `16.000` = 16 MHz. Ajuste em `CAN_XTAL_MHZ` ou na web (Diagnóstico → Cristal).
- **Meça os pinos 6 e 14 com a ignição ligada:** ~2,5 V cada em relação ao GND confirma a
  rede CAN.

## Compilar e gravar

Com [PlatformIO](https://platformio.org/) (a biblioteca `autowp-mcp2515` é baixada sozinha):

```
pio run -t upload
pio device monitor
```

## Uso

1. Ligue o módulo no OBD e dê ignição (ou ligue o motor).
2. No celular conecte na rede **FiestaDash** (senha `fiesta2004`).
3. Abra **http://192.168.4.1**.

Abas:

- **Painel**: conta-giros, velocidade, temperaturas, borboleta, MAP, carga, avanço, sonda
  lambda, correções de combustível e bateria. Luz de injeção e nº de falhas no topo.
- **Layouts e cores**: o botão ▦ troca o desenho (Arcos, Clássico com ponteiros, Barra
  esportiva com shift light, Digital), e o botão ◐ troca as cores (Escuro, Claro, Esportivo,
  Retrô LCD, Noturno). A escolha fica salva no celular.
- **Falhas**: lê (modo 03) e apaga (modo 04) os códigos de falha.
- **Diagnóstico**: estado do MCP2515, protocolo, ECU que respondeu, tráfego do barramento,
  erros CAN, configuração, console para pedidos crus e log.

**Modo demonstração** (aba Diagnóstico): dados fictícios para testar o painel na bancada.

### Botão BOOT (GPIO0)

| Ação | O que faz |
|---|---|
| **Toque curto** | força nova conexão agora (desliga o modo demonstração) |
| **Segurar 3 s** | **autoteste do MCP2515** em loopback: confirma SPI e chip sem transmitir nada no carro |

O LED azul (GPIO2) fica aceso durante a tentativa de conexão e pisca enquanto há comunicação.

## Como a conexão funciona

1. **Detecção da velocidade sem interferir no carro:** o MCP2515 fica em *listen-only*
   (não transmite nem confirma quadros) e escuta 500, 250 e 125 kbps, 400 ms cada. A
   velocidade em que chegam quadros válidos é a do carro. O log mostra quantos quadros e
   quais IDs foram vistos.
2. **Pedido OBD:** em modo normal, com filtros que só aceitam respostas de diagnóstico,
   envia `01 00` para **7DF** (funcional). Se ninguém responder, tenta **7E0** (direto na
   ECU do motor).
3. **Leitura contínua:** consulta só os PIDs que a ECU disse suportar, com o RPM em metade
   dos pedidos. No CAN isso rende dezenas de leituras por segundo.

Respostas longas (várias DTCs, VIN `09 02`) são remontadas por ISO-TP, com flow control.

### Diagnóstico pelo log

| Mensagem | Significado | O que fazer |
|---|---|---|
| `MCP2515 não responde no SPI` | o chip não foi encontrado | fiação SPI, alimentação 5 V, conversor de nível; segure o BOOT 3 s |
| `Tráfego em 500 kbps: N quadros…` | barramento OK, velocidade detectada | — |
| `Nenhum tráfego CAN em 500/250/125 kbps` | não chegou nenhum quadro válido | ignição ligada? CAN-H/CAN-L trocados? cristal certo (8/16 MHz)? |
| `Sem resposta de 7DF` / `7E0` | o barramento funciona mas a ECU não atende OBD nesse ID | tente outro ID de pedido (ex. 7E0, 7E2) |
| `Falha ao transmitir … BUS-OFF` | ninguém confirmou o quadro | velocidade errada, CAN-H/L trocados, ou linha sem a outra ponta |
