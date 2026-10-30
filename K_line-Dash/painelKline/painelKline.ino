

// -------------------------------
//       BIBLIOTECAS USADAS
// -------------------------------
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h> // LiquidCrystal_I2C by Frank de Brabander
#include "AltSoftSerial.h"     // AltSoftSerial-master.zip      
#include "OBD9141.h"           // obd9141-master.zip

// Configuração dos pinos
#define RX_PIN 8            // Entrada de sinal: Conectado ao catodo do diodo zener do conversor de nível
#define TX_PIN 9            // Saída do sinal: Conectado ao resistor de base do conversor de nível
#define EN_PIN 10           // Usado para acionar algum circuito que precise de sinal de enable para ligar
#define led_green_pids 3    // Led Verde - usado como marcador de requisição(pisca a cada ciclo de requisição)
#define led_yellow_falhas 4 // Led amarelo - usado como marcador de falha.
#define pino_botao 2        // Botão de páginas


// --- Objetos ---
AltSoftSerial altSerial;
OBD9141 obd;
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Endereço I2C: 0x27 ou 0x3F conforme o módulo

// ------------------
// Valores default dos parâmetros
uint16_t rpm = 9999;
uint8_t tps = 99;
uint8_t mapkpa = 99;
uint8_t temp = 99;
uint8_t temp_ar = 99;
uint8_t carga = 99;
float adv = 99.0;
uint8_t sonda_1a=0;
uint8_t sonda_1b=0;
uint8_t sonda_2a=0;
uint8_t sonda_2b=0;
uint16_t tensao_sonda_1=0;
uint16_t tensao_sonda_2=0;
float fueltrim_sonda_1=0.0;
float fueltrim_sonda_2=0.0;


// ------------------
// Variáveis gerais
bool conectado=false;
uint8_t pidCount = 0;
uint8_t pidList[96];
int pid_intervalo=5;
String proto_name = "Proto"; 
int falhas = 0;
const int max_falhas = 15; // Quantidade máxima de falhas antes de considerar a comunicação como "perdida"
uint32_t sup_block=0;
uint8_t offset = 0;        // Offset para requisição de pids do bloco superior



// ------------------
// Variáveis do botão

const byte total_modos = 3;
byte modo = 1;
uint8_t prevModo = 0;
bool estadoAnterior = HIGH;
bool estadoEstavel = HIGH;
unsigned long tempoDebounce = 0;
const unsigned long debounce = 30;
  

// -----------------------------------------------
// Desenha Splash Screen e tentativas de conexão
// -----------------------------------------------
void drawConnect()
{
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("K-LINE");
    lcd.setCursor(0, 1);
    lcd.print("Conect: ");
    lcd.print(proto_name.substring(0, 8));
}



void piscaLed(int t){
  for (uint8_t i = 0; i < t; i++){
    digitalWrite(led_yellow_falhas, LOW);
    digitalWrite(led_green_pids, HIGH);
    delay(200);
    digitalWrite(led_green_pids, LOW);
    digitalWrite(led_yellow_falhas, HIGH);
    delay(200);    
  }
  digitalWrite(led_yellow_falhas, LOW);
  digitalWrite(led_green_pids, LOW);
}


void conectar(){
    while(!conectado){
      
        if (!conectado){
            piscaLed(2);                    
            proto_name = "5 Baud";         
            drawConnect();    
            if (obd.init()){
                delay (2000);    
                conectado=1;
            }    
        }

        if (!conectado){
            piscaLed(2);                    
            proto_name = "Kwp 2000";         
            drawConnect();    
            if (obd.initKWP()){
                delay (2000);
                conectado=1;
            }
        }
        
        if (!conectado){  
            piscaLed(2);                      
            proto_name = "Slow";             
            drawConnect();    
            if (obd.initKWPSlow()){
                delay (2000);    
                conectado=1;
            }
        } 
    }        
}

void debug_serial(){
    Serial.print("O2 = ");
    Serial.print(tensao_sonda_1);  Serial.print(" mV   Trim = ");
    Serial.print(fueltrim_sonda_1);  Serial.print("% ");
    Serial.print("ADV = ");
    Serial.println(adv);
}

void setup() {
  Serial.begin(115200);                 // Inicia o monitor serial para Debug  
  lcd.init();                           // Inicializa o display LCD I2C
  lcd.backlight();                      // Liga a retroiluminação
  pinMode(led_green_pids, OUTPUT);      // Configura o pino do led verde (pids) como saida
  pinMode(led_yellow_falhas, OUTPUT);   // Configura o pino do led amarelo (falhas) como saida
  pinMode(pino_botao, INPUT_PULLUP);    // Configura o botão de modos em nível alto
  obd.begin(altSerial, RX_PIN, TX_PIN); // Configura a saída serial para o OBD
  delay(100);                           // Aguarda estabilizar
  piscaLed(8);                          // Pisca LEDS
}

void loop() {
       botao();                     // Seleciona o modo de funcionamento: 1=Modo Painel, 2=Modo scan, 3=Modo pids disponíveis
       uint8_t offset = 0;  
       digitalWrite(led_green_pids, LOW);  // Inicia o led verde em nivel baixo (poe em zero o pino de enable)

      if (!conectado){ 
        conectar(); // Tenta conexão e retorna "conectado=true"         
      } else {
        botao();    // Checa o modo
        bool is_more = true;
        delay(1000);  


        // ------------ Laço solicitações de pids e respostas da ECU
        while(1){

            // Acender o led verde indicando início da varredura dos pids
            digitalWrite(led_green_pids, HIGH); 
            botao();        // Checa o modo
          
            // Interrompe o laço caso o número de falhas  ultrapasse determinado valor 
            if (falhas >= max_falhas){
                 digitalWrite(led_yellow_falhas, HIGH);
                 conectado=false;
                 falhas=0;
                 break;
            }

            le_pids();  // Interroga a ECU sobre quais PIDS estão disponíveis 
            
            
            // Apaga o led verde finalizando a varredura dos pids
            digitalWrite(led_green_pids, LOW);  
            
            
            // debug_serial(); // Escreve na serial os dados recebidos

            // -----------------------------------------------      
            // Desenha as tela de acordo com o valor de "modo"
            if (modo != prevModo) {
              lcd.clear();
              prevModo = modo;
            }

              if (modo==1){       // Modo Painel 
                drawTela();       
              }  
              
              if (modo==2){       // Modo Scanner
                drawScanner();    
              }
                
              if (modo==3){       // Modo Lista de Pids
                pidCount=0;
                is_more = lista_pids(sup_block, offset);
                drawPids();                             
                offset=0;
              } 

            
                     
        }   // fim while
    }       // Fim do if conectado()
           
    delay(1000);
} // Fim do Loop
