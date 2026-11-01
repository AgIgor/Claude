# Protocolo: ar Electrolux e módulo NIU-LIGHT

Baseado nestas capturas, em `Resultado/`:

- `captura_2026-10-31-21-56-09.csv`: 132 quadros, comandos feitos pelo controle remoto.
- `captura_2026-10-31-22-19-31.csv`: 120 quadros, comandos feitos pelo app.
- `captura_2026-10-31-22-46-08.csv`: 254 quadros, todas as funções do app.
- `captura_2026-11-01-15-04-58.csv`: 70 quadros, modo silencioso.

Todos têm tamanho e checksum corretos. Decodifique outras capturas com:

```
python tools/decode_capture.py Resultado/arquivo.csv
```

## Serial

- 9600 baud, 8E1, repouso em nível alto.
- Fio verde: transmissão do ar. Fio branco: transmissão do módulo.

## Quadro

| Posição | Campo | Observação |
|---|---|---|
| 0 | Início | Sempre `C6` |
| 1 | Tamanho | Bytes do quadro menos 3 |
| 2-3 | Contador | 16 bits; cada lado tem o seu. Bit 15 ligado no envio, desligado na confirmação |
| 4 | Fixo | Sempre `AD`; função desconhecida |
| 5-7 | Destino | 3 letras ASCII |
| 8-10 | Origem | 3 letras ASCII |
| 11-12 | Propriedade | Ver abaixo |
| 13 | Operação | Ver abaixo |
| 14... | Valor | Tamanho variável; ausente na confirmação |
| Último | Checksum | XOR de todos os bytes após o `C6`, sem incluir o checksum |

O contador passa de `D0FF` para `D100`. Ele sobe também com heartbeats, e cada envio consome um
valor. No módulo, ele voltou para perto de `8000` depois de religado.

| Operação | Sentido | Uso |
|---|---|---|
| `04` | `RP1` para `CA1` | Comando de escrita |
| `05` | `CA1` para `RP1` | Confirmação do comando |
| `06` | `CA1` ou `AE1` para `ALL`, `NIU` para `CA1` | Aviso de estado e heartbeat |
| `07` | Sentido inverso | Confirmação do aviso |

Na confirmação, origem e destino trocam de lugar e o contador é o mesmo do envio, sem o bit 15.

## Sequência de um comando

1. O módulo envia a operação `04`.
2. O ar confirma com `05` assim que o quadro termina.
3. Cerca de 19 ms após o início do comando, o ar avisa `ALL` com `06` e o valor novo.
4. O módulo confirma o aviso com `07`.

A confirmação e o aviso costumam vir colados no fio verde: separe pelo campo de tamanho.

## Nós

| Nome | Identificação |
|---|---|
| `NIU` | O módulo Wi-Fi; envia o heartbeat |
| `RP1` | Nome usado pelo módulo nos comandos do app |
| `CA1` | Ar; recebe os comandos e avisa o estado |
| `AE1` | Ar; função desconhecida, talvez a unidade externa |
| `ALL` | Endereço de difusão |

## Comandos confirmados

Escrita de `RP1` para `CA1`, operação `04`. Todos foram capturados com o app e confirmados pelo ar:

| Função | Propriedade | Valores |
|---|---|---|
| Ligar e desligar | `04.03` | `01` liga, `00` desliga |
| Modo | `10.00` | `01` resfriar, `02` aquecer, `03` ventilar, `06` automático |
| Ventilador | `10.02` | `01` baixa, `02` média, `04` alta, `08` turbo |
| Setpoint | `04.32` | `00 00 GG 00`, com GG em °C; 22, 23 e 30 capturados |
| Direção fixa | `10.05` = `00`, depois `10.07` | Posição `01` (para cima, como no app) a `06` |
| Oscilar | `10.05` = `01`, depois `10.07` | `00` amplo, `01` em cima, `02` no meio, `03` em baixo |
| Dormir | `04.28` | `01` liga, `00` desliga |
| Timer | `04.13` | Segundos em 32 bits: `00 00 0E 10` = 1 h |
| Display | `04.29` | `01` liga, `00` desliga |
| Silencioso | `04.25` | `00` silencia, `01` liga o som (testado no ar; os marcadores da captura estavam ao contrário) |
| Ar indireto | `10.01` | `01` |
| Auto sense | `04.38` | `01` |

Ao desligar com o modo dormir ativo, o app envia primeiro `04.28` = `00`.

Exemplo, ligar: `C6 0D D0 F2 AD 43 41 31 52 50 31 04 03 04 01 80`

## Avisos de estado

| Propriedade | Origem | Significado | Confiança |
|---|---|---|---|
| `04.01` | CA1 | Estado: `02` ligado, `00` desligado | Alta |
| `10.00` | CA1 | Mesmos valores do comando | Alta |
| `10.02` | CA1 | Mesmos valores do comando | Alta |
| `04.32` | CA1 | Setpoint | Alta |
| `10.05` | CA1 | Oscilar | Alta |
| `10.07` | CA1 | Posição da aleta; `07` apareceu ao entrar em aquecer | Alta; `07` em aberto |
| `04.13` | CA1 | Timer em segundos; zera ao desligar | Alta |
| `04.28`, `04.29`, `04.38`, `10.01` | CA1 | Dormir, display, auto sense, ar indireto | Alta |
| `04.30` | CA1 | Provável temperatura interna em °C: ficou em 22-23 enquanto o app mostrava 23 | Média |
| `04.33` | CA1 | Provável temperatura em °C, sensor desconhecido | Média |
| `04.35`, `04.36`, `04.30` | AE1 | Provável temperatura em °C | Média |
| `04.A0`, `04.A1`, `04.A3`, `04.A4`, `04.44`, `04.50`, `10.11` | AE1 | Desconhecido | Nenhuma |
| `04.25` | CA1 | Silencioso. Já apareceu sem comando pela serial, talvez pelo controle remoto | Alta |

`04.A1` e `04.50` foram a `01` ao resfriar e voltaram a `00` em modo ventilar.

## Heartbeat

`NIU` para `CA1`, propriedade `00.20`, operação `06`, a cada 10 s. O valor é
`01 02 01 02 13 XX`, MAC (6 bytes), `04`, IP (4 bytes). XX foi `01` e depois `05`, sem efeito
visível. Depois de religado, o módulo também enviou `NIU` para `CA1`, propriedade `00.60`,
valores `08` e `00`, função desconhecida. O ar só confirmou: esse quadro não faz o ar reenviar o estado.
Em outra partida, enviou `00.32` com `03`, `02` e `01`, e o XX do heartbeat seguiu os mesmos valores.

## Em aberto

- Se o ar exige heartbeat, confirmações `07` ou um contador em certa faixa.
- O que acontece em uma falha de confirmação ou com um comando inválido.
- O significado de `RP1`, `CA1`, `AE1` e `AD`.
- Se o ar exige a sequência `00.32` da partida do módulo antes de aceitar comandos.
- `NIU` para `CA1`, propriedade `04.73`, operação `06`, visto com o ESP32 em paralelo ao módulo; o
  ar pareceu responder com operação `0E`. Os valores não foram guardados.
- Valor `00` de ar indireto e auto sense, cancelar o timer, temperatura mínima e timers acima de 1 h.
- O alvo só é conhecido quando o ar avisa: não vimos pedido de leitura do estado.
