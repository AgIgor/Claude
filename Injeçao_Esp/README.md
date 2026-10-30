# ECU ESP32

ECU programável para motor 4 cilindros:

- Injeção **sequencial** (com sensor de fase) ou **semissequencial** (automático se a fase falhar)
- Ignição **sequencial** (bobinas individuais) ou **centelha perdida**
- Roda fônica N-M (configurada para 60-2) + sensor de fase hall com qualquer número de pulsos (desde que as duas voltas tenham contagens diferentes)
- Speed-density: mapa de VE, avanço e lambda alvo 16×16 (RPM × MAP)
- Correções: aquecimento, partida, pós-partida, aceleração rápida, temperatura do ar, tempo morto pela bateria
- Malha fechada com sonda banda estreita, corte de giro, corte em desaceleração
- 2 ventoinhas, bomba com escorva e desligamento automático
- Marcha lenta: válvula PWM ou motor de passo, controle PI + correção pelo avanço, compensação de A/C e ventoinha, amortecedor
- Ar-condicionado: pedido, relé do compressor com atraso, cortes por rotação/temperatura/plena carga
- Detonação: sensor de 2 fios via TPIC8101, janela por cilindro e retardo individual
- Página web pelo Wi-Fi: painel ao vivo, editor de mapas, curvas, configuração, teste de saídas e log CSV

## Estrutura

```
platformio.ini        ambientes: ecu, simulador (ESP32), simulador_uno
src/rt.cpp            tempo real: decodificador da roda fônica + agendador (interrupções)
src/engine.cpp        tarefa de 1 kHz: sensores, combustível/avanço, marcha lenta, A/C, detonação, relés
src/idle.cpp          atuador de marcha lenta (PWM ou motor de passo)
src/knock.cpp         driver do TPIC8101
src/settings.cpp      configuração, mapas, gravação na flash (NVS), JSON
src/web.cpp           Wi-Fi, servidor web, WebSocket com dados ao vivo
web/index.html        interface (embutida no firmware na compilação)
src/sim/sim_main.cpp  gerador de sinal 60-2 + fase para bancada
```

Compilar e gravar: `pio run -e ecu -t upload` (ou pelo botão do PlatformIO no VS Code).
Simulador: `pio run -e simulador -t upload` em um segundo ESP32, ou `-e simulador_uno` num Arduino Uno.

## Arquitetura

| Núcleo | O que roda |
|---|---|
| 1 | Interrupção do sensor de rotação/fase, timer de hardware dos eventos, tarefa do motor a 1 kHz |
| 0 | Wi-Fi, servidor web, envio dos dados ao vivo (20 Hz) |

- A cada dente, a interrupção agenda no timer de hardware os eventos que caem antes do próximo dente. Assim a previsão nunca passa de 6° (18° na falha), o que mantém a precisão mesmo acelerando forte.
- O fechamento do bico é por tempo (largura de pulso exata). A abertura é por ângulo.
- As interrupções usam só matemática inteira. O cálculo em ponto flutuante fica na tarefa de 1 ms.
- **Gravar na flash só com o motor parado.** Durante a gravação o ESP32 desliga o cache da flash e as interrupções atrasam. Por isso a ECU recusa gravar com o motor girando. A página web fica na flash de programa e não usa sistema de arquivos pelo mesmo motivo.
- Alterações feitas pela web valem na hora (RAM). Para ficarem permanentes, clique em **Gravar na flash** com o motor parado.

## Pinos (ESP32 WROOM DevKit)

| Função | GPIO | Observação |
|---|---|---|
| Sensor de rotação (CKP) | 34 | só entrada, **sem pull-up interno**: pull-up na placa |
| Sensor de fase (CMP) | 35 | idem |
| Bico 1 / 2 / 3 / 4 | 25 / 26 / 27 / 13 | |
| Bobina 1 / 2 / 3 / 4 | 16 / 17 / 21 / 22 | |
| Ventoinha 1 / 2 | 32 / 33 | relé |
| Bomba de combustível | 14 | relé (o GPIO14 pulsa rapidamente no boot, inofensivo em relé) |
| Compressor do A/C | 12 | relé. **Strapping**: precisa estar em nível baixo no boot (pull-down de 10k) |
| Marcha lenta | 4 | PWM da válvula ou STEP do driver do motor de passo |
| DIR do motor de passo / LED | 2 | com motor de passo é o DIR; senão, LED de status (aceso = sincronizado) |
| Pedido do A/C | 36 | só entrada, sem pull-up interno |
| Livre (entrada) | 39 | só entrada |
| SPI SCK / MISO / MOSI | 18 / 19 / 23 | compartilhado entre MCP3208 e TPIC8101 |
| CS do MCP3208 | 5 | |
| CS do TPIC8101 | 0 | **Strapping**: precisa estar em nível alto no boot (o DevKit já tem pull-up) |
| INT/HOLD do TPIC8101 | 15 | janela de detonação (alto = integrando) |

Os pinos 0, 2, 12 e 15 definem o modo de boot do ESP32. As ligações acima respeitam os níveis exigidos, mas **não coloque pull-up no 12 nem pull-down no 0**: o ESP32 não inicializa.

Os números referem-se ao **cilindro** (bico 1 = cilindro 1). A ordem de ignição 1-3-4-2 já está no firmware.

### Canais do MCP3208

| Canal | Sensor | Ligação |
|---|---|---|
| 0 | MAP | 0-5 V → divisor (ex.: 10k em série + 20k ao GND = ÷1,5) |
| 1 | Temperatura da água | NTC com pull-up de 2,49k para **3,3 V** |
| 2 | Temperatura do ar | NTC com pull-up de 2,49k para **3,3 V** |
| 3 | Sonda lambda | sinal 0-1 V direto (resistor série 10k + 100 nF) |
| 4 | TPS (opcional) | 0-5 V → divisor ÷1,5 |
| 5 | Bateria | divisor 47k / 10k (÷5,7) com zener 3,3 V |

Alimente o MCP3208 com **3,3 V** (VDD e VREF). Com 5 V, o MISO mandaria 5 V para o ESP32.

## Observações de hardware importantes

**Entradas analógicas e zener.** O zener de 3,3 V começa a conduzir antes dos 3,3 V e distorce a leitura no fim da escala, principalmente nos NTC. Nas entradas analógicas é melhor usar resistor série (1k a 4,7k) + capacitor de 100 nF ao GND + diodo duplo Schottky (BAT54S) para o 3,3 V e o GND. Deixe o zener para as entradas digitais e a bateria.

**Sensor de rotação.** A roda 60-2 da Ford costuma vir com sensor **indutivo** (2 fios, gera tensão AC). Se for esse o caso, ele não serve direto numa entrada digital: precisa de um condicionador VR (MAX9926 ou LM1815). Se você trocou por um hall (3 fios), basta o pull-up.

**Velocidade do optoacoplador.** Optos comuns (PC817) têm atraso de 10 a 100 µs, diferente na subida e na descida. A 6000 rpm, 1° = 28 µs, então isso vira alguns graus de erro no avanço. Prefira optos rápidos (6N137, TLP2361, H11L1), pelo menos nas bobinas. Se usar PC817, meça o atraso com osciloscópio e coloque o valor em *Configuração → Ignição → Atraso do opto*, depois confirme com a pistola de ponto em alta rotação.

**Polaridade e boot.** Monte as saídas de forma que **GPIO em nível alto = saída acionada**. No boot os GPIOs ficam em nível baixo ou flutuando; com lógica invertida, bicos e bobinas seriam acionados durante a inicialização. A opção "inverter saída" existe para casos específicos, mas evite usar. Coloque um pull-down de 10k em cada GPIO de saída.

**Bobinas Honda City (COP com ignitor).** Sinal IGT: nível alto = carregando, borda de descida = faísca. Ligue o opto como seguidor de emissor (coletor no 5 V, emissor no IGT com 1k ao GND), para que o nível alto do ESP32 resulte em IGT alto. Dwell típico de 2,5 a 3 ms; a curva padrão já vem nessa faixa.

**Sensor de fase.** O pulso de fase não pode cair perto da falha da roda fônica. Se cair, ele aparece ora numa volta, ora na outra, e a ECU fica alternando entre sequencial e centelha perdida. O ideal é ficar pelo menos uns 30° longe da falha.

**Sensor de fase com vários pulsos.** A ECU conta quantos pulsos de fase aparecem em cada volta do virabrequim (de falha a falha). Configure "Pulsos de fase na volta A" e "na volta B" com essas contagens (ex.: 1 e 0 para um dente só; 3 e 1, 4 e 1, 2 e 1...). As duas contagens precisam ser diferentes. Se o comando tiver dentes igualmente espaçados (2 e 2, por exemplo), não dá para saber a fase só por contagem. Nenhum pulso pode cair perto da falha da roda fônica. Para descobrir a contagem, ligue o motor sem fase ("Usar sensor de fase" desmarcado) e meça com osciloscópio, ou use o simulador com `a`/`b` para testar a configuração na bancada.

**Marcha lenta.**
- *Válvula PWM de 2 fios*: MOSFET no lado negativo com diodo de roda livre (igual ao bico). A frequência típica fica entre 100 e 300 Hz; consulte a válvula.
- *Motor de passo (4 fios, bipolar)*: driver DRV8825 ou A4988 com STEP no GPIO4, DIR no GPIO2, SLEEP e RESET em nível alto e corrente limitada a cerca de 0,3 A. Ao ligar, a ECU fecha o curso todo para achar o zero.
- Os optoacopladores nesses sinais funcionam bem, porque são lentos (no máximo algumas centenas de Hz).

**Ar-condicionado.** Ligue o sinal que vem do botão do A/C, depois de passar pelos pressostatos, na entrada GPIO36, com a mesma proteção das outras entradas digitais. Configure se ele é ativo em GND ou em 12 V. Se for 12 V, use divisor ou opto. A ECU primeiro aumenta a abertura da marcha lenta e só depois de "Atraso para acoplar" liga o compressor.

**Detonação (TPIC8101).** O sensor de 2 fios é piezoelétrico: ele gera um sinal de alguns kHz que precisa ser filtrado e integrado. Quem faz isso é o TPIC8101, ligado no mesmo SPI do MCP3208.
- Alimentação de **5 V** e cristal de **8 MHz** nos pinos OSCIN/OSCOUT.
- O sensor vai nas entradas CH1P/CH1N, com os componentes do datasheet: resistores em série e capacitores de acoplamento.
- **Níveis lógicos**: o TPIC funciona em 5 V. Coloque um 74HCT125 alimentado em 5 V nos sinais que saem do ESP32 (SCK, MOSI, CS e INT/HOLD). No SO do TPIC, que volta para o ESP32, use um buffer 74LVC1G125 alimentado em 3,3 V, habilitado pelo CS do TPIC, para não brigar com o MISO do MCP3208.
- O TPIC8101 saiu de linha na TI, mas ainda é vendido (e há clones). A placa de detonação da rusEFI usa o mesmo chip e serve de referência de esquema.

**Sonda lambda do Fiesta 2004.** É **banda estreita** (0-1 V): só indica se a mistura está rica ou pobre em torno de λ = 1. Com ela a ECU mantém λ 1 em marcha lenta e cruzeiro (malha fechada), mas **não mede** o 0,85 a 0,88 de plena carga. Para acertar plena carga com segurança, use uma **sonda wideband** (LSU 4.9 + controlador com saída 0-5 V). Dá para trocar a lógica depois. O aquecedor da sonda pode ser ligado por relé junto com a bomba.

**TPS.** Não estava na sua lista, mas vale muito a pena instalar. Melhora o enriquecimento de aceleração, o corte em desaceleração e permite o "flood clear" (acelerador no fundo durante a partida = sem combustível).

**Alimentação.** Use um regulador buck automotivo com TVS (SMBJ24A ou similar) na entrada de 12 V. Ligue os GNDs de potência (bicos, bobinas) separados do GND de sinal, unindo num só ponto.

**Atualização de firmware.** Quando a estrutura de configuração muda (como nesta versão), a ECU carrega os padrões no primeiro boot. Antes de atualizar, use *Configuração → Exportar configuração* e depois importe o arquivo. Os campos novos ficam com o valor padrão.

## Primeira partida: passo a passo

1. **Bancada com simulador.** Grave o `simulador` num segundo ESP32/Uno e ligue as saídas dele nas entradas CKP/CMP. Coloque LEDs (com resistor) nas saídas. No monitor serial do simulador digite `r1000`. O painel deve mostrar ~1000 rpm, "sincronizado", "fase OK" e "sequencial". Teste `c0` (sem fase → muda para centelha perdida) e `j1` (oscilação de partida).
2. **Teste de saídas** (aba Testes) com o motor parado: confira que cada bico e cada bobina corresponde ao cilindro certo.
3. **Configuração básica**: cilindrada, vazão dos bicos, combustível (AFR estequiométrico 13,2 para gasolina comum, 9,0 para etanol), calibração do MAP e dos NTC. Confira as temperaturas e o MAP com o motor desligado (MAP ≈ pressão atmosférica, ~92 a 100 kPa).
4. **Ponto (PMS cil. 1 após a falha)**: em *Ignição*, ative "Travar avanço" em 10°. Dê partida e confira com a pistola de ponto. Ajuste o "PMS cil. 1 após a falha" até a marca bater em 10°.
   - Se o motor não pegar de jeito nenhum com sequencial, a fase pode estar invertida: some **360** ao valor.
   - Para testar sem depender da fase, desmarque "Usar sensor de fase" (centelha perdida). Se pegar assim e não pegar com fase, é esse o problema.
5. **Marcha lenta**: em *Testes → Marcha lenta manual*, com o motor quente, ache a posição que segura a rotação no alvo. Use esse valor para ajustar a curva "Posição base" (e repita com o motor frio, se puder). Depois volte ao automático. Se oscilar, reduza o ganho I. Se demorar a corrigir, aumente o P.
6. Volte para o mapa de avanço, faça o acerto do **VE** na marcha lenta e em cruzeiro com a malha fechada ativa. Na aba Mapas, o botão **Aplicar correção da sonda** passa para a célula atual a correção que a malha fechada está aplicando. Repita até a correção ficar perto de 0%.
7. **Detonação**: ative com "Retardo máximo" em 0. Rode em várias rotações sem detonar e anote o nível de cada cilindro no painel. Monte a curva "Limiar de detonação" uns 30 a 50% acima desse nível normal. Se o nível ficar colado em 1023, aumente o índice de ganho (menos ganho); se ficar perto de 0, diminua. Só depois coloque o retardo máximo (6 a 8°).
8. Grave na flash com o motor desligado.

## Limitações conhecidas

- Sem corpo de borboleta eletrônico (a marcha lenta é por válvula/motor de passo).
- Detonação: a leitura digital do TPIC8101 (ordem dos bytes no modo avançado) foi escrita pelo datasheet e ainda precisa ser confirmada na bancada. Dê leves batidas no bloco perto do sensor: o nível do painel deve subir.
- Sonda banda estreita: só malha fechada em λ 1.
- Ordem de ignição fixa em 1-3-4-2.
- Os mapas de fábrica são genéricos e conservadores. **Não use carga alta antes de acertar os mapas**, porque mistura pobre e avanço excessivo quebram motor.

## Validação

O decodificador e o agendador foram testados no PC (compilando o `rt.cpp` real com timer e GPIO simulados), com roda 60-2 + fase:

- 3000 rpm constante: faíscas exatamente em PMS − avanço para os 4 cilindros, na ordem 1-3-4-2, e pulsos de injeção com a largura e o ângulo corretos.
- Centelha perdida: pares 1/4 e 2/3 disparando juntos a cada volta.
- Aceleração de 1000 a 6500 rpm: erro de ponto abaixo de 0,1°.
- Partida a 200 rpm com rotação oscilando 30% a cada compressão: sem perda de sincronismo.
- Fase com 3 pulsos numa volta e 1 na outra: sincroniza e dispara na ordem certa. Com a contagem configurada errada, a fase é rejeitada (e a ECU passa para centelha perdida).
- Janela de detonação: abre exatamente em PMS + 10° de cada cilindro, com a duração configurada.

Ainda falta testar no hardware real: tempos reais de interrupção com o Wi-Fi ativo, ruído elétrico e leitura dos sensores. Comece pela bancada com o simulador e um osciloscópio (ou analisador lógico barato de 24 MHz + PulseView).
