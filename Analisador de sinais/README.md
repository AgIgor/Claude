# ACRemote: ar-condicionado Electrolux

Um ESP32 substitui o módulo Wi-Fi **Electrolux NIU-LIGHT** (Liteon `WN3602M_5V_V04`) e controla o
ar por uma página web servida por ele mesmo. O controle foi testado no aparelho, sem o módulo
original. O app da Electrolux não funciona com o ESP32 no lugar.

O firmware também escuta a serial, descobre baud e formato sozinho e mostra os quadros. Com
**Controle ativo** desmarcado, ele só lê: o pino de transmissão fica em alta impedância.

O protocolo descoberto até agora está em [PROTOCOLO.md](PROTOCOLO.md).

## Segurança

- Faça as ligações com o **disjuntor desligado**.
- O GND da placa do ar pode não ser isolado da rede elétrica. Alimente o ESP32 por **power bank**,
  nunca pelo USB do PC, e não toque no circuito com o ar ligado.
- Mantenha o módulo original ligado. O ESP32 fica em paralelo com ele.
- Não alimente o ESP32 pelo fio vermelho do módulo.

## Ligação

Primeiro meça o verde e o branco em relação ao preto, **parados, sem usar o app**:

| Tensão em repouso | Ligação de cada linha |
|---|---|
| Acima de 3,6 V (provável 5 V) | Divisor: linha -> 10 kΩ -> GPIO, GPIO -> 20 kΩ -> GND |
| Até 3,6 V | Linha -> 1 kΩ -> GPIO |
| Sem saber, com zener de 3,3 V | Linha -> 4,7 kΩ -> GPIO, zener do GPIO ao GND |

```
Fio verde  --[10k]--+-- GPIO34        Fio branco --[10k]--+-- GPIO35
                    |                                     |
                  [20k]                                 [20k]
                    |                                     |
Fio preto  ---------+-- GND do ESP32  --------------------+
```

Com menos de 3,6 V, o divisor deixaria o sinal abaixo do nível alto do ESP32. A ligação com 1 kΩ
tem outro cuidado: sem o fio conectado, o pino fica solto e capta ruído.

Evite resistor em série muito alto. Com 100 kΩ e zener, a corrente fica tão baixa que o zener
derruba a tensão antes de chegar a 3,3 V, e a capacitância dele deixa as bordas lentas. Em qualquer
ligação, o GPIO deve medir pelo menos 2,6 V com a linha em repouso.

Verde e branco podem ficar em qualquer pino: os dois são só entradas, e trocar apenas inverte os
rótulos A e B.

Sem conexão no fio vermelho. Sem ligação nos GPIO 25 e 26, que são só do teste de bancada.

## Uso

1. Grave o firmware: `pio run -t upload`.
2. Sem roteador configurado, o ESP32 abre a rede `ACRemote`. Conecte nela (senha
   `analisador`) e abra `http://192.168.4.1`.
3. Faça o teste de bancada abaixo.
4. Ligue ao ar e envie comandos pelo app. Com pelo menos 16 bytes bons, a página trava baud e
   formato e passa a mostrar os quadros. A trava fica salva na flash.

### Teste de bancada

Com o ar desconectado, ligue GPIO25 no GPIO34 e GPIO26 no GPIO35. Na página, escolha baud e formato
e clique em **Ligar gerador**. A trava deve mostrar os mesmos valores, e cada quadro deve mudar só o
contador e a soma final. Depois, desligue o gerador e retire os jumpers.

### Capturando o boot

Depois que baud e formato estiverem travados, deixe o ESP32 ligado com o ar desligado no disjuntor
e religue o ar. Os quadros ficam guardados (até 24 KB) até a página abrir.

### Decifrando o protocolo

- Faça uma mudança por vez e use **Marcar** antes de cada uma.
- Use o app, e não só o controle remoto: o comando de escrita passa pelo módulo.
- Bytes em destaque mudaram em relação ao quadro anterior com o mesmo início e tamanho.
- A linha que fala primeiro depois de um comando do app é, em geral, a do módulo para o ar.
- Use **Exportar CSV** antes de fechar a página: o histórico fica só no navegador.

### Lendo o histograma de pulsos

Uma UART mostra picos separados na faixa do tempo de 1 bit e nos múltiplos dele. A 9600 baud,
1 bit dura 104 µs: a maior parte cai em `<160 µs`, e o restante em `<320 µs` e `<640 µs`.
Barras maiores nas faixas curtas, diminuindo aos poucos, indicam ruído.

### Se nada aparecer

| Sintoma | Causa provável |
|---|---|
| Bordas em zero | Ligação, ou tensão menor que o nível alto do ESP32 |
| Contador **Ruído** subindo | Pino solto ou fio mal conectado |
| Bordas sobem, sem candidatos | Ruído ou outro protocolo; veja o histograma e a tela de sinal |
| Quadro cortado em dois | Aumente a pausa entre quadros |
| Dois quadros juntos | Diminua a pausa entre quadros |

## Controle do ar (substituindo o módulo)

Com o disjuntor desligado, desconecte o módulo original e ligue:

```
GPIO27 --[1k]-- fio branco      (TX: comandos para o ar)
```

Mantenha as duas ligações de escuta. A linha B passa a mostrar o que o ESP32 transmite. Não ligue o
fio vermelho, e nunca ligue o GPIO27 com o módulo original conectado: as duas saídas brigariam.

Em **Ar-condicionado**, marque **Controle ativo**. Isso trava `9600 8E1` e pausa de 3 ms, e o ESP32
passa a enviar heartbeat e a confirmar os avisos do ar. A opção fica salva: desmarque antes de
reconectar o módulo original.

O estado do ar aparece mesmo com o controle desligado, mas só depois que o ar avisar cada valor.

## Rede e atualização

Na página, em **Rede Wi-Fi**, escolha a rede e clique em **Conectar**. O IP aparece no topo, e a
página também abre em `http://acremote.local`.

- `http://acremote.local/`: controle do ar.
- `http://acremote.local/monitor`, também em `/ajustes`: no ESP32, quadros, detecção, Wi-Fi,
  atualização de firmware e o **Controle ativo**.

A rede `ACRemote` é só reserva. Ela abre quando o roteador fica 30 s sem responder, e fecha
depois de 30 s de conexão estável. Enquanto isso, o ESP32 tenta o roteador a cada 60 s.

Há duas formas de atualizar sem USB. A senha padrão é `analisador` (`OTA_PASS` em `config.h`):

- **Pela página:** rode `pio run` e envie `.pio/build/esp32dev/firmware.bin` em **Atualizar firmware**.
- **Pelo PlatformIO:** rode `pio run -e ota -t upload`. O PC precisa estar na mesma rede, e o
  firewall do Windows pode bloquear a resposta do ESP32.

A primeira gravação com esta versão precisa ser por USB. Uma gravação com falha mantém o firmware
anterior.

## Versão para ESP-01

O ESP-01 de 1 MB só faz o controle e fica em paralelo com o módulo original: a página é a mesma do
ESP32, sem monitor da serial, que depende da captura de bordas e de ler as duas linhas. O modelo de 512 KB não serve. Esta versão já comandou o
ar e recebeu o estado dele com a ligação abaixo.

| ESP-01 | Liga em |
|---|---|
| GPIO3 (RX) | Fio verde, por 1 kOhm em série e zener 3V3 ao GND; com 5 kOhm, o ESP-01 não recebe |
| GPIO1 (TX) | Gate de um MOSFET N de nível lógico, com dreno por 220 Ohm a 1 kOhm no fio branco e fonte no GND |
| GND | Fio preto |
| VCC | 3,3 V de um regulador com capacitor, que aguente ao menos 300 mA |
| EN (CH_PD) | 3,3 V |
| GPIO0 e GPIO2 | Sem ligação |

- Alimente o regulador pelo fio vermelho de 5 V só depois de medir essa tensão. Nunca ligue 5 V
  direto no ESP-01.
- O GPIO1 e o GPIO2 precisam estar altos ao ligar, senão a placa não inicia. Não coloque resistor
  de pull-down no gate. O MOSFET inverte o sinal, e o firmware compensa com `AC_TX_INVERTED`; até o firmware
  iniciar, a linha fica baixa por um instante.
- Para testar a saída com multímetro, abra `/api/txtest`: o GPIO1 alterna a cada 3 s por 60 s, e
  nesse tempo nada é transmitido para o ar.
- Gravação: com um adaptador USB-serial de 3,3 V e o GPIO0 no GND ao ligar, `pio run -e esp01 -t
  erase` e depois, religando com o GPIO0 no GND, `pio run -e esp01 -t upload`. Depois retire o GPIO0 do GND. As próximas podem ser por `pio run -e esp01_ota -t upload`
  ou em `http://acremote.local/ajustes`.
- Na primeira vez, conecte na rede `ACRemote` (senha `analisador`) e abra
  `http://192.168.4.1/ajustes` para escolher o roteador.
- Com o ESP-01 alimentado pelo ar, não ligue USB nele sem desligar o disjuntor: o GND do ar pode não
  ser isolado da rede.
- O módulo original cuida do heartbeat e das confirmações, e o ESP-01 só envia comandos e lê o ar
  (`AC_PARALLEL`). Com ele, `hb` fica em 0 no `/api/ac/state`, e `hbok` sobe com os heartbeats do
  módulo. Sem o módulo, grave com `AC_PARALLEL false`: depois de uma queda de energia, o ar pode
  não aceitar comandos.

## Modos e programação

Nas duas placas, os cartões **Modos** e **Programação** da página de controle ficam salvos na
memória da placa. São até 6 modos e 10 horários.

- Um modo guarda tipo, temperatura, ventilador, direção, Dormir, Display e Silencioso, e também
  liga o ar ao ser aplicado.
- Um horário escolhe os dias da semana e aplica um modo ou desliga o ar. Com dois no mesmo
  minuto, vale o último da lista.
- O relógio vem da internet (`pool.ntp.org`), com fuso de Brasília (`AC_TIMEZONE`). Sem internet
  no roteador, nenhum horário dispara, e a página avisa. Um horário perdido com a placa desligada
  não é executado depois.

## Testes no PC

O decodificador não depende do Arduino:

```
cd test_host
g++ -std=c++17 -I../src test_decode.cpp ../src/uart_decode.cpp -o test_decode
./test_decode
g++ -std=c++17 -I../src test_protocol.cpp ../src/ac_protocol.cpp -o test_protocol
./test_protocol
g++ -std=c++17 -I../src test_schedule.cpp -o test_schedule
./test_schedule
```

Limite conhecido: 8N2, 8E2 e 8O2 com pausa entre bytes aparecem como a versão de 1 stop bit, e a
leitura sai igual. Formatos de 7 bits com paridade aparecem como 8N1, com a paridade no bit 7.
