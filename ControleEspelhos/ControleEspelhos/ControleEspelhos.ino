/*
 * Controle de espelhos retrovisores com botão resistivo VW Tiguan 2015
 * ---------------------------------------------------------------------
 * Placa: Arduino Uno / Nano (ATmega328P)
 *
 * Entradas analógicas (divisor de tensão: pull-up de 5V -> pino -> botão -> GND):
 *   A2 = joystick  (neutro, cima, baixo, esquerda, direita)
 *   A1 = seletor   (espelho esquerdo, centro, espelho direito, rebatimento)
 *
 * Entrada digital:
 *   D3 = desembaçador (botão fecha para GND na posição) -> relé em D12
 *
 * A calibração é gravada na EEPROM e carregada a cada partida.
 * Entrar em calibração:
 *   - automaticamente, se a EEPROM não tiver calibração válida;
 *   - segurando D2 em GND (botão/jumper) ao ligar;
 *   - enviando 'c' pelo Monitor Serial (115200 baud).
 *
 * Comandos seriais: c = calibrar | p = mostrar calibração | m = liga/desliga monitor
 */

#include <EEPROM.h>

// ===================== CONFIGURAÇÃO DE HARDWARE =====================

const uint8_t PIN_JOY = A2;
const uint8_t PIN_SEL = A1;
const uint8_t PIN_CAL = 2;            // botão/jumper para GND = calibrar ao ligar
const uint8_t PIN_LED = LED_BUILTIN;
const uint8_t PIN_DESEMB_ENTRADA = 3;  // GND = desembaçador ligado (usa pull-up interno)
const uint8_t PIN_DESEMB_RELE    = 12; // relé que alimenta as resistências dos espelhos

// Cada saída comanda um relé SPDT (ou meia-ponte):
//   desligado = fio do motor em GND, ligado = fio do motor em +12V.
enum {
  OUT_E_COM, OUT_E_V, OUT_E_H,        // espelho esquerdo: comum, vertical, horizontal
  OUT_D_COM, OUT_D_V, OUT_D_H,        // espelho direito
  OUT_FOLD_A, OUT_FOLD_B,             // motores de rebatimento (ambos em paralelo)
  N_OUT
};
const uint8_t PIN_OUT[N_OUT] = { 4, 5, 6, 7, 8, 9, 10, 11 };
const bool RELE_ATIVO_EM_LOW = true;  // módulos de relé comuns acionam com LOW

// Inverta se o espelho andar no sentido contrário (índice 0 = esquerdo, 1 = direito)
const bool INVERTE_V[2] = { false, false };
const bool INVERTE_H[2] = { false, true };   // espelhos são montados espelhados
const bool INVERTE_REBATER = false;

// ===================== PARÂMETROS =====================

const uint16_t TEMPO_REBATER_MS   = 5000;   // tempo de acionamento do rebatimento
const uint32_t MAX_AJUSTE_MS      = 15000;  // proteção: ajuste contínuo máximo
const uint16_t TEMPO_MORTO_MS     = 60;     // pausa ao inverter sentido dos relés
const uint16_t DEBOUNCE_JOY_MS    = 40;
const uint16_t DEBOUNCE_SEL_MS    = 150;
const uint16_t SEPARACAO_MIN      = 25;     // diferença mínima (ADC) entre níveis na calibração
const uint16_t TOLERANCIA_MAX     = 60;     // janela máxima de aceitação em torno de cada nível
const uint16_t JANELA_ESTAVEL     = 6;      // variação admitida enquanto "parado"
const uint16_t TEMPO_ESTAVEL_MS   = 1500;   // tempo parado para capturar um nível
const uint16_t DEBOUNCE_DESEMB_MS = 100;
const uint32_t MAX_DESEMB_MS      = 600000; // desliga após 10 min (0 = sem limite)

// ===================== ESTADOS =====================

enum { J_NEUTRO, J_CIMA, J_BAIXO, J_ESQ, J_DIR, N_JOY };
enum { S_ESQ, S_CENTRO, S_DIR, S_REBATER, N_SEL };

const char* const NOME_JOY[N_JOY] = { "NEUTRO", "CIMA", "BAIXO", "ESQUERDA", "DIREITA" };
const char* const NOME_SEL[N_SEL] = { "ESPELHO ESQ", "CENTRO", "ESPELHO DIR", "REBATER" };

// ===================== CALIBRAÇÃO (EEPROM) =====================

const uint16_t CAL_MAGIC  = 0x5E7A;
const uint8_t  CAL_VERSAO = 1;
const int      CAL_ENDERECO = 0;

struct Calibracao {
  uint16_t magic;
  uint8_t  versao;
  uint16_t joy[N_JOY];
  uint16_t sel[N_SEL];
  uint8_t  checksum;
};

Calibracao cal;
uint16_t tolJoy[N_JOY], tolSel[N_SEL];

uint8_t calcChecksum(const Calibracao& c) {
  const uint8_t* p = (const uint8_t*)&c;
  uint8_t s = 0xA5;
  for (size_t i = 0; i < offsetof(Calibracao, checksum); i++) s += p[i];
  return s;
}

bool carregarCalibracao() {
  EEPROM.get(CAL_ENDERECO, cal);
  return cal.magic == CAL_MAGIC && cal.versao == CAL_VERSAO && cal.checksum == calcChecksum(cal);
}

void salvarCalibracao() {
  cal.magic = CAL_MAGIC;
  cal.versao = CAL_VERSAO;
  cal.checksum = calcChecksum(cal);
  EEPROM.put(CAL_ENDERECO, cal);      // put() só regrava bytes que mudaram
}

uint16_t difAbs(uint16_t a, uint16_t b) { return a > b ? a - b : b - a; }

// Tolerância de cada nível = metade da distância até o vizinho mais próximo
void calcularTolerancias(const uint16_t* niveis, uint16_t* tol, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    uint16_t menor = 0xFFFF;
    for (uint8_t k = 0; k < n; k++)
      if (k != i) menor = min(menor, difAbs(niveis[i], niveis[k]));
    tol[i] = min((uint16_t)(menor / 2), TOLERANCIA_MAX);
  }
}

// Retorna o índice do nível mais próximo, ou -1 se fora de todas as janelas
int8_t classificar(uint16_t v, const uint16_t* niveis, const uint16_t* tol, uint8_t n) {
  int8_t melhor = -1;
  uint16_t melhorDif = 0xFFFF;
  for (uint8_t i = 0; i < n; i++) {
    uint16_t d = difAbs(v, niveis[i]);
    if (d <= tol[i] && d < melhorDif) { melhor = i; melhorDif = d; }
  }
  return melhor;
}

// ===================== LEITURA ANALÓGICA =====================

uint16_t lerMedia(uint8_t pino, uint8_t amostras = 16) {
  uint32_t soma = 0;
  for (uint8_t i = 0; i < amostras; i++) soma += analogRead(pino);
  return soma / amostras;
}

struct Debounce {
  int8_t estavel = -1, candidato = -1;
  uint32_t desde = 0;
  bool atualizar(int8_t valor, uint16_t tempoMs) {   // retorna true quando o estado estável muda
    if (valor != candidato) { candidato = valor; desde = millis(); return false; }
    if (candidato != estavel && millis() - desde >= tempoMs) { estavel = candidato; return true; }
    return false;
  }
};

// ===================== LED =====================

// Pisca 'codigo' vezes e pausa (indica o passo da calibração sem PC)
void piscarCodigo(uint8_t codigo) {
  uint32_t periodo = codigo * 400UL + 1000;
  uint32_t t = millis() % periodo;
  digitalWrite(PIN_LED, t < codigo * 400UL && (t % 400) < 200);
}

void piscarRapido(uint8_t vezes) {
  for (uint8_t i = 0; i < vezes; i++) {
    digitalWrite(PIN_LED, HIGH); delay(60);
    digitalWrite(PIN_LED, LOW);  delay(60);
  }
}

// ===================== SAÍDAS =====================

bool saidaAtual[N_OUT];

void escreverSaida(uint8_t i, bool ligado) {
  digitalWrite(PIN_OUT[i], (ligado != RELE_ATIVO_EM_LOW) ? HIGH : LOW);
}

void desligarTudo() {
  for (uint8_t i = 0; i < N_OUT; i++) { escreverSaida(i, false); saidaAtual[i] = false; }
}

void escreverDesembacador(bool ligado) {
  digitalWrite(PIN_DESEMB_RELE, (ligado != RELE_ATIVO_EM_LOW) ? HIGH : LOW);
}

// Aplica o padrão desejado com tempo morto ao trocar de um acionamento para outro
void aplicarSaidas(const bool* alvo) {
  bool igual = true, atualAtivo = false, alvoAtivo = false;
  for (uint8_t i = 0; i < N_OUT; i++) {
    if (alvo[i] != saidaAtual[i]) igual = false;
    atualAtivo |= saidaAtual[i];
    alvoAtivo  |= alvo[i];
  }
  if (igual) return;
  if (atualAtivo && alvoAtivo) { desligarTudo(); delay(TEMPO_MORTO_MS); }
  for (uint8_t i = 0; i < N_OUT; i++) { escreverSaida(i, alvo[i]); saidaAtual[i] = alvo[i]; }
}

// Motores do espelho compartilham o fio comum. O motor parado recebe o
// mesmo potencial do comum para não girar junto.
void montarAjuste(bool* alvo, uint8_t espelho, int8_t joy) {
  uint8_t base = espelho == 0 ? OUT_E_COM : OUT_D_COM;
  uint8_t com = base, v = base + 1, h = base + 2;
  bool vertical = (joy == J_CIMA || joy == J_BAIXO);
  bool positivo = (joy == J_CIMA || joy == J_DIR);
  if (vertical ? INVERTE_V[espelho] : INVERTE_H[espelho]) positivo = !positivo;

  uint8_t motor = vertical ? v : h, outro = vertical ? h : v;
  alvo[motor] = positivo;    // motor em +12V e comum em GND, ou o inverso
  alvo[com]   = !positivo;
  alvo[outro] = !positivo;
}

void montarRebatimento(bool* alvo, bool rebater) {
  bool sentido = rebater != INVERTE_REBATER;
  alvo[OUT_FOLD_A] = sentido;
  alvo[OUT_FOLD_B] = !sentido;
}

// ===================== CALIBRAÇÃO =====================

bool longeDeTodos(uint16_t v, const uint16_t* lista, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) if (difAbs(v, lista[i]) < SEPARACAO_MIN) return false;
  return true;
}

// Espera a leitura ficar parada por TEMPO_ESTAVEL_MS e diferente dos níveis já capturados
uint16_t capturarEstavel(uint8_t pino, const uint16_t* excluir, uint8_t nExcluir, uint8_t codigoLed) {
  uint16_t ref = lerMedia(pino);
  uint32_t desde = millis();
  for (;;) {
    piscarCodigo(codigoLed);
    uint16_t v = lerMedia(pino);
    if (difAbs(v, ref) > JANELA_ESTAVEL || !longeDeTodos(v, excluir, nExcluir)) {
      ref = v;
      desde = millis();
    } else if (millis() - desde >= TEMPO_ESTAVEL_MS) {
      return lerMedia(pino, 64);
    }
  }
}

void esperarJoystickNeutro(const uint16_t* tol) {
  uint32_t desde = millis();
  while (millis() - desde < 300) {
    if (classificar(lerMedia(PIN_JOY), cal.joy, tol, N_JOY) != J_NEUTRO) desde = millis();
  }
}

void calibrar() {
  desligarTudo();
  escreverDesembacador(false);
  Serial.println(F("\n===== CALIBRACAO ====="));
  Serial.println(F("A calibracao anterior so e substituida ao final."));

  uint16_t tol[N_JOY];

  // --- Joystick: captura automática quando a leitura estabiliza ---
  Serial.println(F("[1] Solte o joystick (NEUTRO) e aguarde..."));
  cal.joy[J_NEUTRO] = capturarEstavel(PIN_JOY, nullptr, 0, 1);
  Serial.print(F("    NEUTRO = ")); Serial.println(cal.joy[J_NEUTRO]);
  piscarRapido(3);

  for (uint8_t j = J_CIMA; j < N_JOY; j++) {
    Serial.print(F("[")); Serial.print(j + 1); Serial.print(F("] Segure o joystick para "));
    Serial.print(NOME_JOY[j]); Serial.println(F(" ate o LED piscar rapido..."));
    cal.joy[j] = capturarEstavel(PIN_JOY, cal.joy, j, j + 1);
    Serial.print(F("    ")); Serial.print(NOME_JOY[j]); Serial.print(F(" = ")); Serial.println(cal.joy[j]);
    piscarRapido(3);

    // aguarda soltar (volta para perto do neutro)
    Serial.println(F("    Solte o joystick."));
    while (difAbs(lerMedia(PIN_JOY), cal.joy[J_NEUTRO]) > SEPARACAO_MIN / 2) piscarCodigo(j + 1);
    delay(300);
  }
  calcularTolerancias(cal.joy, tol, N_JOY);

  // --- Seletor: posições travam, então confirma com toque no joystick ---
  for (uint8_t s = 0; s < N_SEL; s++) {
    uint8_t passo = N_JOY + s + 1;
    Serial.print(F("[")); Serial.print(passo); Serial.print(F("] Gire o seletor para "));
    Serial.print(NOME_SEL[s]); Serial.println(F(" e toque o joystick em qualquer direcao."));
    for (;;) {
      // espera um toque firme no joystick
      uint32_t desde = millis();
      while (millis() - desde < 200) {
        piscarCodigo(passo);
        int8_t j = classificar(lerMedia(PIN_JOY), cal.joy, tol, N_JOY);
        if (j <= J_NEUTRO) desde = millis();
      }
      uint16_t v = lerMedia(PIN_SEL, 64);
      esperarJoystickNeutro(tol);
      if (longeDeTodos(v, cal.sel, s)) { cal.sel[s] = v; break; }
      Serial.print(F("    Leitura ")); Serial.print(v);
      Serial.println(F(" igual a uma posicao ja gravada. Confira a posicao e tente de novo."));
      for (uint8_t k = 0; k < 3; k++) { digitalWrite(PIN_LED, HIGH); delay(500); digitalWrite(PIN_LED, LOW); delay(200); }
    }
    Serial.print(F("    ")); Serial.print(NOME_SEL[s]); Serial.print(F(" = ")); Serial.println(cal.sel[s]);
    piscarRapido(3);
  }

  salvarCalibracao();
  calcularTolerancias(cal.joy, tolJoy, N_JOY);
  calcularTolerancias(cal.sel, tolSel, N_SEL);
  Serial.println(F("Calibracao salva na EEPROM."));
  imprimirCalibracao();
  piscarRapido(15);
}

void imprimirCalibracao() {
  Serial.println(F("--- Calibracao (valor ADC +/- tolerancia) ---"));
  for (uint8_t i = 0; i < N_JOY; i++) {
    Serial.print(F("  JOY ")); Serial.print(NOME_JOY[i]); Serial.print(F(": "));
    Serial.print(cal.joy[i]); Serial.print(F(" +/- ")); Serial.println(tolJoy[i]);
  }
  for (uint8_t i = 0; i < N_SEL; i++) {
    Serial.print(F("  SEL ")); Serial.print(NOME_SEL[i]); Serial.print(F(": "));
    Serial.print(cal.sel[i]); Serial.print(F(" +/- ")); Serial.println(tolSel[i]);
  }
  for (uint8_t i = 0; i < N_JOY; i++)
    if (tolJoy[i] < 15) { Serial.println(F("  AVISO: niveis do joystick muito proximos; ajuste o resistor de pull-up.")); break; }
  for (uint8_t i = 0; i < N_SEL; i++)
    if (tolSel[i] < 15) { Serial.println(F("  AVISO: niveis do seletor muito proximos; ajuste o resistor de pull-up.")); break; }
}

// ===================== PROGRAMA PRINCIPAL =====================

Debounce dbJoy, dbSel, dbDesemb;
bool monitor = false;
uint32_t fimRebatimento = 0;
bool rebatendo = false, sentidoRebater = false;
uint32_t inicioAjuste = 0;
bool ajusteBloqueado = false;
bool desembLigado = false;
uint32_t inicioDesemb = 0;

// Liga enquanto o botão estiver na posição; o tempo máximo desliga até a
// posição ser deixada e selecionada de novo.
void atualizarDesembacador() {
  if (dbDesemb.atualizar(digitalRead(PIN_DESEMB_ENTRADA) == LOW, DEBOUNCE_DESEMB_MS)) {
    desembLigado = dbDesemb.estavel == 1;
    inicioDesemb = millis();
    Serial.println(desembLigado ? F("Desembacador ligado") : F("Desembacador desligado"));
  }
  if (desembLigado && MAX_DESEMB_MS > 0 && millis() - inicioDesemb >= MAX_DESEMB_MS) {
    desembLigado = false;
    Serial.println(F("Desembacador desligado: tempo maximo atingido"));
  }
  escreverDesembacador(desembLigado);
}

void iniciarRebatimento(bool rebater) {
  sentidoRebater = rebater;
  rebatendo = true;
  fimRebatimento = millis() + TEMPO_REBATER_MS;
  Serial.println(rebater ? F("Rebatendo espelhos") : F("Abrindo espelhos"));
}

// Informa no Serial cada mudança do joystick e o que foi feito com ela
void informarAjuste(int8_t espelho) {
  static int8_t joyAnterior = J_NEUTRO;
  static bool bloqueadoAnterior = false;
  int8_t joy = dbJoy.estavel < 0 ? J_NEUTRO : dbJoy.estavel;

  if (ajusteBloqueado && !bloqueadoAnterior)
    Serial.println(F("Ajuste interrompido: tempo maximo atingido. Solte o joystick."));
  bloqueadoAnterior = ajusteBloqueado;

  if (joy == joyAnterior) return;
  joyAnterior = joy;

  if (joy == J_NEUTRO) { Serial.println(F("Parado")); return; }
  Serial.print(F("Joystick ")); Serial.print(NOME_JOY[joy]); Serial.print(F(" -> "));
  if (rebatendo)        Serial.println(F("ignorado (rebatimento em andamento)"));
  else if (espelho < 0) Serial.println(F("ignorado (seletor fora de ESQ/DIR)"));
  else { Serial.print(F("movendo ")); Serial.println(NOME_SEL[espelho == 0 ? S_ESQ : S_DIR]); }
}

void tratarSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'c' || c == 'C') calibrar();
  else if (c == 'p' || c == 'P') imprimirCalibracao();
  else if (c == 'm' || c == 'M') { monitor = !monitor; Serial.println(monitor ? F("Monitor ON") : F("Monitor OFF")); }
}

void setup() {
  for (uint8_t i = 0; i < N_OUT; i++) pinMode(PIN_OUT[i], OUTPUT);
  desligarTudo();
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_CAL, INPUT_PULLUP);
  pinMode(PIN_DESEMB_ENTRADA, INPUT_PULLUP);
  escreverDesembacador(false);
  pinMode(PIN_DESEMB_RELE, OUTPUT);
  Serial.begin(115200);
  Serial.println(F("Controle de espelhos - c=calibrar p=calibracao m=monitor"));

  bool valida = carregarCalibracao();
  if (!valida) Serial.println(F("Sem calibracao valida na EEPROM."));
  if (!valida || digitalRead(PIN_CAL) == LOW) calibrar();

  calcularTolerancias(cal.joy, tolJoy, N_JOY);
  calcularTolerancias(cal.sel, tolSel, N_SEL);
  imprimirCalibracao();
}

void loop() {
  tratarSerial();

  uint16_t brutoJoy = lerMedia(PIN_JOY);
  uint16_t brutoSel = lerMedia(PIN_SEL);
  int8_t joy = classificar(brutoJoy, cal.joy, tolJoy, N_JOY);
  int8_t sel = classificar(brutoSel, cal.sel, tolSel, N_SEL);

  // Joystick fora de qualquer nível conhecido = parar (seguro)
  dbJoy.atualizar(joy < 0 ? J_NEUTRO : joy, DEBOUNCE_JOY_MS);

  // Seletor fora de nível (transição entre posições) mantém o último estado válido
  int8_t selAnterior = dbSel.estavel;
  if (sel >= 0 && dbSel.atualizar(sel, DEBOUNCE_SEL_MS)) {
    if (dbSel.estavel == S_REBATER) iniciarRebatimento(true);
    else if (selAnterior == S_REBATER) iniciarRebatimento(false);
  }

  if (rebatendo && (int32_t)(millis() - fimRebatimento) >= 0) rebatendo = false;

  // Proteção de tempo máximo de ajuste
  if (dbJoy.estavel == J_NEUTRO || dbJoy.estavel < 0) { inicioAjuste = millis(); ajusteBloqueado = false; }
  else if (millis() - inicioAjuste > MAX_AJUSTE_MS) ajusteBloqueado = true;

  bool alvo[N_OUT] = { false };
  int8_t espelho = dbSel.estavel == S_ESQ ? 0 : dbSel.estavel == S_DIR ? 1 : -1;
  if (rebatendo) {
    montarRebatimento(alvo, sentidoRebater);
  } else if (!ajusteBloqueado && dbJoy.estavel > J_NEUTRO && espelho >= 0) {
    montarAjuste(alvo, espelho, dbJoy.estavel);
  }
  aplicarSaidas(alvo);
  informarAjuste(espelho);
  atualizarDesembacador();

  if (monitor) {
    static uint32_t ultimo = 0;
    if (millis() - ultimo >= 200) {
      ultimo = millis();
      Serial.print(F("JOY ")); Serial.print(brutoJoy); Serial.print(F(" -> "));
      Serial.print(joy >= 0 ? NOME_JOY[joy] : "???");
      Serial.print(F(" | SEL ")); Serial.print(brutoSel); Serial.print(F(" -> "));
      Serial.print(sel >= 0 ? NOME_SEL[sel] : "???");
      Serial.print(F(" | DESEMB ")); Serial.println(digitalRead(PIN_DESEMB_ENTRADA) == LOW ? F("ON") : F("OFF"));
    }
  }
}
