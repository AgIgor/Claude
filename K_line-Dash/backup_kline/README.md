# Fiesta Dash: K-line → ESP32 → Web

Dashboard web em tempo real para **Ford Fiesta 2004 1.0 (Zetec Rocam)** com ECU
**Magneti Marelli IAW 4AFR.BR** (Ford 5S65-12A650-AC, código CLYZA / AFR2).

O ESP32 conversa com a ECU pela K-line (pino 7 do conector OBD), cria uma rede Wi-Fi
própria e serve um painel que abre no navegador do celular.

## Hardware (interface com 2 transistores, adaptada para ESP32)

Baseada no circuito *Microcontroller-OBD Interface* de prj.perquin.com (`circuito.png`),
feito originalmente para ATmega8 em 5 V. Para o ESP32 (3,3 V) foram feitas 3 mudanças,
marcadas com ★.

| Ref. | Valor | Função |
|---|---|---|
| Q1, Q2 | BC547 (ou BC337 / 2N2222) | Q1 inverte o TX, Q2 puxa a K-line para o GND |
| R1 | 10 kΩ | base do Q1 |
| R3 | 10 kΩ → +12 V | coletor do Q1 / polarização do Q2 |
| R5 | 10 kΩ | base do Q2 |
| R4 ★ | **1 kΩ** (original 10 kΩ) → +12 V | pull-up da K-line (norma: 510 Ω) |
| R2 | 10 kΩ | RX: série da K-line para o GPIO16 |
| R7 ★ | 10 kΩ GPIO16 → GND | RX: divisor ÷2 |
| DZ1 ★ | zener 3V3 0,5 W, catodo no GPIO16 | RX: limita a tensão no ESP32 |
| R6 ★ | 10 kΩ GPIO17 → 3,3 V | mantém a K-line livre durante o boot |
| — | step-down 12 V → 5 V, fusível 1 A, 1N4007 | alimentação do ESP32 (VIN) |
| — | (opcional) TVS SMBJ18A K-line → GND | proteção contra picos |

Use ESP32 **WROOM** (no WROVER, GPIO16/17 são da PSRAM).

### Ligações

```
OBD 16 (+12V) ─ fusível ─ 1N4007 ─┬─ step-down 5V ─ VIN do ESP32
                                   └─ +12V (R3, R4)

                3V3          +12V           +12V
                 │            │              │
                R6 10k       R3 10k         R4 1k
                 │            │              │
GPIO17 ──────────┴─ R1 10k ─ B Q1  C ┴─ R5 10k ─ B Q2  C ┴──────────────┬── OBD 7 (K-line)
                            E ─ GND                     E ─ GND          │
                                                                         │
GPIO16 ──┬───────────────────────────────── R2 10k ──────────────────────┘
         ├── DZ1 zener 3V3 (catodo no GPIO) ── GND
         └── R7 10k ── GND

OBD 4/5 (GND) ── GND comum
```

| GPIO17 | Q1 | Q2 | K-line |
|---|---|---|---|
| alto (repouso / bit 1) | satura | cortado | 12 V |
| baixo (bit 0) | cortado | satura (base via R3 + R5) | ~0,1 V |

O sinal não é invertido, então `KLINE_TX_INVERT` fica em `0` em `src/config.h`.

**Por que as mudanças**

- **RX (DZ1 + R7):** no original, o R2 ligava a K-line (12 V) direto ao pino do ATmega8,
  e o diodo de proteção interno do chip absorvia o excesso. No ESP32 isso injetaria
  ~0,8 mA no pino de 3,3 V, acima do que o chip tolera (máximo 3,6 V). O zener limita a
  tensão, e o R7 faz o divisor para que um "0" da ECU de até ~1,6 V seja lido
  corretamente.
- **R6 (pull-up no GPIO17):** enquanto o ESP32 inicia, o GPIO fica flutuando. Sem o R6,
  o Q1 corta, o Q2 satura e a K-line fica presa em 0 V.
- **R4 1 kΩ:** com 10 kΩ, a borda de subida fica lenta (~25 µs com 2 nF de linha, num bit
  de 96 µs). Com 1 kΩ fica em ~2,5 µs. O Q2 saturado puxa 12 mA, e o BC547 aguenta
  100 mA. A base dele recebe ~0,56 mA via R3 + R5, o que satura com folga até 510 Ω.

> Ao ligar o módulo no carro, os 12 V chegam antes do ESP32 terminar de iniciar, e a
> linha pode ficar em 0 V por alguns milissegundos. Isso é inofensivo com a ignição
> desligada. Se quiser eliminar esse efeito, alimente R3 pelos 5 V do step-down e
> troque R3 e R5 por 4,7 kΩ.

### Outras opções de TX

- **Transceptor L9637D/MC33290:** `KLINE_TX_INVERT 0`.
- **Um transistor/MOSFET só** (sinal invertido): `KLINE_TX_INVERT 1`, com pull-down no gate/base.

## Compilar e gravar

Com [PlatformIO](https://platformio.org/) (VS Code ou CLI):

```
pio run -t upload
pio device monitor
```

Configurações (pinos, Wi-Fi, temporizações) ficam em `src/config.h`.

## Uso

1. Ligue o módulo no OBD e dê ignição (ou ligue o motor).
2. No celular conecte na rede **FiestaDash** (senha `fiesta2004`).
3. Abra **http://192.168.4.1**.

Abas:

- **Painel**: conta-giros, velocidade, temperatura do motor e do ar, borboleta (TPS),
  pressão do coletor (MAP), carga, avanço, sonda lambda, correções de combustível e bateria.
  Luz de injeção e número de falhas aparecem no topo quando a ECU informa.
- **Falhas**: lê (modo 03) e apaga (modo 04) os códigos de falha.
- **Layouts e cores**: botão ▦ no topo troca o desenho do painel (Arcos, Clássico com
  ponteiros, Barra esportiva com shift light, Digital); botão ◐ troca as cores (Escuro,
  Claro, Esportivo, Retrô LCD, Noturno). A escolha fica salva no celular.
- **Diagnóstico**: protocolo detectado, PIDs suportados, configuração da inicialização,
  console para enviar pedidos crus à ECU, trace de TX/RX e log.

**Modo demonstração** (botão na aba Diagnóstico): gera dados fictícios para testar o
painel na bancada, sem carro.

### Botão BOOT (GPIO0)

| Ação | O que faz |
|---|---|
| **Toque curto** | força nova conexão com a ECU agora: desliga o modo demonstração e pula a espera entre tentativas |
| **Segurar 3 s** | **teste do circuito**: por 20 s a K-line alterna 1 s em 0 V e 1 s em 12 V. O log mostra se o RX acompanhou (`OK` / `FALHA`) |

Se o botão for tocado durante uma tentativa em andamento, a nova conexão começa assim
que essa tentativa terminar (até ~5 s).

O LED azul da placa (GPIO2) fica **aceso durante a tentativa de conexão** e
**pisca** enquanto há comunicação com a ECU.

### Se não sair nenhum sinal na K-line

1. Abra o monitor serial (`pio device monitor`, 115200) e reinicie a placa. As primeiras
   linhas mostram pinos e configuração. Se aparecer `demonstração LIGADA`, toque no BOOT.
2. Toda tentativa gera no log `Tentativa de conexão #N`, `Init rápida...` e
   `Init 5 baud...`. Se essas linhas aparecem, o firmware está transmitindo.
3. O pulso da init rápida dura 25 ms, rápido demais para o multímetro mostrar. A init de
   5 baud dura ~2 s com bits de 200 ms e já dá para ver no ponteiro ou num LED.
   O jeito mais fácil é **segurar o BOOT 3 s** e medir a K-line: tem que alternar entre
   ~0 V e ~12 V a cada segundo.
4. Se o teste falhar: meça o GPIO17 (deve alternar 0 / 3,3 V), depois o coletor do Q1
   e por fim a K-line. Placas **WROVER** usam GPIO16/17 na PSRAM, e nelas é preciso trocar os
   pinos em `src/config.h`.

### Se a ECU nunca responde

No modo **Automático**, cada tentativa usa uma combinação diferente (init rápida/5 baud ×
endereços 33, 10, 11, 01). Quando uma funciona, a reconexão começa por ela. Cada falha
gera uma linha de diagnóstico:

| Mensagem no log | Significado | O que fazer |
|---|---|---|
| `Nenhum eco: o ESP32 não está lendo a K-line` | o TX sai, mas o RX não enxerga a própria transmissão | circuito do RX (R2, zener, divisor, GPIO16) |
| `Erro de eco: enviado XX, lido YY` | o RX lê algo diferente do que foi enviado | nível lógico do RX, pull-up da K-line |
| `RX: linha parada, a ECU não transmitiu nada` | o circuito está OK, mas a ECU não respondeu | ignição ligada? pino 7? outro endereço/protocolo |
| `RX: N bordas, menor pulso X us (~Y bps)` | a ECU respondeu, mas não deu para decodificar | se `Y` ≠ 10400, escolha essa velocidade em Diagnóstico → Velocidade |
| `Sync inesperado: XX` | a ECU respondeu à init 5 baud em outra velocidade | idem |

## Protocolo

Em modo Automático o firmware tenta, nesta ordem:

1. **Init rápida ISO 14230 (KWP2000)**: pulso de 25 ms + `StartCommunication`
   (`C1 33 F1 81 66`), endereçamento funcional OBD 0x33.
2. **Init 5 baud** no endereço 0x33: detecta ISO 9141-2 (keybytes `08 08` / `94 94`)
   ou KWP2000 lento pelos keybytes.

Depois de conectar, pergunta à ECU quais PIDs ela suporta (`01 00`, `01 20`, `01 40`) e
consulta só esses. O RPM é pedido em metade das requisições para o ponteiro ficar fluido;
temperaturas e correções são lidas com menos frequência. Rende de 6 a 8 pedidos por segundo,
o limite prático da K-line a 10400 bps com os tempos da norma.

### Se a ECU não responder ao OBD padrão

O Fiesta 2004 nacional é anterior à exigência do OBD-BR2, e a família Marelli IAW 4AF
costuma usar KWP2000 na camada de transporte, mas parte dos dados pode estar só em serviços
proprietários (ex. `21 xx`, *readDataByLocalIdentifier*). Se o log mostrar *"ECU não
respondeu ao modo 01"*:

1. Em **Diagnóstico**, teste o alvo físico da ECU (ex. `10`, `11`, `7A`) no campo
   *Alvo KWP* e/ou outro endereço na init 5 baud, e clique *Aplicar e reconectar*.
2. Com conexão estabelecida, use o console: `1A 80` (identificação da ECU),
   `21 01`, `21 02`… (blocos de dados locais) e compare as respostas com o motor em
   condições conhecidas (marcha lenta, aquecendo, acelerando) para descobrir as fórmulas.
3. Ligue o **Trace TX/RX** para ver os bytes crus no log.
