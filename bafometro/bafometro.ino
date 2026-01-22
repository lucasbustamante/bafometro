#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const int MQ3_PIN = A1;
const int SAMPLES = 30;

// ====== AJUSTES IMPORTANTES ======
// Tempo mínimo de aquecimento antes de calibrar (recomendado: >= 5 min; ideal: bem mais)
const unsigned long WARMUP_MS = 1UL * 10UL * 1000UL;

// Valor “zero” (baseline) medido no ar limpo (vai ser calculado automaticamente)
int baselineRaw = 0;

// Fator de conversão para mg/L (VOCÊ AJUSTA)
// Ex.: se você soprar e um bafômetro de referência mostrar 0.25 mg/L e seu deltaRaw for 120,
// então SCALE_MG_PER_L = 0.25 / 120 = 0.002083
float SCALE_MG_PER_L = 0.0020; // chute inicial (ajuste depois)

// Limite máximo para não explodir número na tela
const float MAX_MG_L = 2.00; // 2.00 mg/L é bem alto (ajuste como quiser)

int readMQ3Average() {
  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(MQ3_PIN);
    delay(5);
  }
  return (int)(sum / SAMPLES);
}

void showMessage(const char* line1, const char* line2 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2) display.println(line2);
  display.display();
}

void calibrateBaseline() {
  // Mede baseline no ar “limpo” por alguns segundos e tira média
  showMessage("Calibrando baseline", "Nao sopre (ar limpo)");

  const int N = 60;
  long sum = 0;
  for (int i = 0; i < N; i++) {
    sum += readMQ3Average();
    delay(50);
  }
  baselineRaw = (int)(sum / N);
}

void setup() {
  pinMode(MQ3_PIN, INPUT);
  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) { delay(100); }
  }

  showMessage("MQ-3 + OLED 0.96", "Aquecendo...");
}

void loop() {
  static unsigned long startMs = millis();
  unsigned long elapsed = millis() - startMs;

  // Aguarda aquecimento antes de calibrar baseline
  if (baselineRaw == 0) {
    if (elapsed < WARMUP_MS) {
      // Mostra contagem regressiva simples
      unsigned long remain = (WARMUP_MS - elapsed) / 1000UL;

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("Aquecendo sensor...");
      display.setCursor(0, 16);
      display.print("Faltam: ");
      display.print(remain);
      display.println(" s");
      display.setCursor(0, 32);
      display.println("Depois calibra zero.");
      display.display();

      delay(300);
      return;
    } else {
      calibrateBaseline();
      showMessage("Baseline OK!", "Pode medir/soprar");
      delay(800);
    }
  }

  int raw = readMQ3Average();
  int delta = raw - baselineRaw;
  if (delta < 0) delta = 0;

  // “mg/L estimado” baseado no delta e no fator
  float mgL = delta * SCALE_MG_PER_L;
  if (mgL > MAX_MG_L) mgL = MAX_MG_L;

  // Tensão só pra debug
  float volts = raw * (5.0 / 1023.0);

  // Tela
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Bafometro (estimado)");

  display.setCursor(0, 14);
  display.print("mg/L: ");
  display.setTextSize(2);
  display.setCursor(0, 24);
  display.print(mgL, 2);
  display.setTextSize(1);

  display.setCursor(0, 48);
  display.print("RAW:");
  display.print(raw);
  display.print(" 0:");
  display.print(baselineRaw);

  // Barrinha mg/L (0..MAX_MG_L)
  display.drawRect(0, 56, 128, 8, SSD1306_WHITE);
  int bar = (int)(126.0 * (mgL / MAX_MG_L));
  if (bar < 0) bar = 0;
  if (bar > 126) bar = 126;
  display.fillRect(1, 57, bar, 6, SSD1306_WHITE);

  display.display();
  delay(200);
}
