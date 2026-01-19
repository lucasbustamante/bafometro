// Programa: Bafometro com ESP32
// Adaptado do código MakerHero para ESP32 + U8g2

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ======== DISPLAY SSD1306 I2C ========
// Padrão ESP32: SDA=21 / SCL=22 (pode mudar se seu ESP32 for diferente)
#define I2C_SDA 8
#define I2C_SCL 9

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ======== SENSOR (ADC) ========
// Use um pino ADC do ESP32 (boas opções: 34, 35, 32, 33, 36, 39)
// Evite ADC2 se você for usar Wi-Fi depois.
#define ADC_PIN 4

// Tempo de aquecimento (segundos)
int tempo_aquecimento = 300;

// Variáveis
int valor_sensor = 0;       // valor já escalado (pra ficar parecido com o original)
int valor_raw = 0;          // valor real do ADC (0-4095)
unsigned long timeSec = 0;  // tempo em segundos
int status = 1;             // 1 = aquecendo / 0 = lendo
String estado;
int posicao1;

void draw()
{
  // Mensagens iniciais
  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.drawRFrame(0, 18, 128, 46, 4);
  u8g2.drawStr(30, 15, "BAFOMETRO");
  u8g2.drawStr(10, 37, "Aguarde");

  // Animacao caneca (aquecimento)
  if (status == 1)
  {
    u8g2.drawBox(80, 25, 20, 30);
    u8g2.drawHLine(77, 24, 26);
    u8g2.drawRFrame(78, 25 , 24, 32, 0);
    u8g2.drawRFrame(77, 25 , 26, 32, 0);
    u8g2.drawHLine(76, 57, 28);
    u8g2.drawHLine(76, 58, 28);
    u8g2.drawRFrame(102, 30 , 7, 20, 2);
    u8g2.drawRFrame(102, 28 , 9, 24, 2);

    // "Enche" a caneca conforme o tempo (0 a 30 px)
    int fill = (int)timeSec; // aqui timeSec já vai estar mapeado para 0..30 no loop
    if (fill < 0) fill = 0;
    if (fill > 30) fill = 30;

    // “apaga” a parte de dentro (simulando líquido subindo)
    u8g2.setDrawColor(0);
    u8g2.drawBox(79, 25, 22, fill);
    u8g2.setDrawColor(1);
  }

  // Após aquecimento, exibe dados do sensor
  if (status == 0)
  {
    u8g2.setFont(u8g2_font_fub20_tn);
    u8g2.setDrawColor(0);
    u8g2.drawBox(10, 25, 110, 33);
    u8g2.setDrawColor(1);

    // Centraliza o valor na tela
    if (valor_sensor <= 99) posicao1 = 50;
    else posicao1 = 43;

    // Mostra valor do sensor
    u8g2.setCursor(posicao1, 47);
    u8g2.print(valor_sensor);

    // Imprime mensagem no rodapé
    u8g2.setFont(u8g2_font_ncenB08_tf);
    int tamanho = estado.length();
    int posicao = (128 / 2 - 3) - ((tamanho * 5) / 2);
    u8g2.setCursor(posicao, 62);
    u8g2.print(estado);
  }
}

void setup(void)
{
  Serial.begin(115200);

  // I2C do display
  Wire.begin(I2C_SDA, I2C_SCL);

  // Display
  u8g2.begin();

  // ADC (opcional, mas ajuda a estabilizar leitura)
  pinMode(ADC_PIN, INPUT);
  analogReadResolution(12);                 // 0..4095
  analogSetPinAttenuation(ADC_PIN, ADC_11db); // melhor faixa (~0 a 3.3V)

  // Telinha inicial rápida
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.drawStr(10, 30, "Iniciando...");
  u8g2.sendBuffer();
  delay(500);
}

void loop(void)
{
  // Tempo em segundos
  unsigned long sec = millis() / 1000;

  // Leitura do ADC (0..4095)
  valor_raw = analogRead(ADC_PIN);

  // Escala pra ficar parecido com o “0..700” do Arduino UNO
  // (assim seus limites 0..600+ continuam fazendo sentido)
  valor_sensor = map(valor_raw, 0, 4095, 0, 700);

  // Aquecimento
  if (sec <= (unsigned long)tempo_aquecimento)
  {
    // para a animação da caneca: 0..30 px
    timeSec = map(sec, 0, tempo_aquecimento, 0, 30);
    status = 1;
  }
  else
  {
    status = 0;
    timeSec = sec; // não é usado aqui, mas mantém consistente
  }

  // Mensagem rodapé (mantive seus ranges)
  if (valor_sensor >= 0 && valor_sensor <= 50)
    estado = "Voce nao bebeu...";
  else if (valor_sensor <= 200)
    estado = "Bebeu 1 cerveja ?";
  else if (valor_sensor <= 400)
    estado = "Bebeu 2 cervejas ?";
  else if (valor_sensor <= 600)
    estado = "Voce cheira a 51 !";
  else
    estado = "Voce esta bebado !!";

  // Debug
  Serial.print("RAW=");
  Serial.print(valor_raw);
  Serial.print("  ESCALADO=");
  Serial.println(valor_sensor);

  // Desenha
  u8g2.clearBuffer();
  draw();
  u8g2.sendBuffer();

  delay(50);
}
