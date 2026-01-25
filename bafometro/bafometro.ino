#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===================== PINAGEM (seguindo a imagem) =====================
// OLED I2C
static const int I2C_SDA = 2;   // GPIO1
static const int I2C_SCL = 3;   // GPIO2

// MQ-3 analógico (ADC)
static const int MQ3_PIN = 4;   // GPIO4 (ADC)


// ===================== DISPLAY =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===================== CONFIG =====================
static const uint32_t WARMUP_SEC = 10;     // 3 minutos
static const uint16_t SAMPLE_MS  = 50;      // taxa de amostragem do ADC
static const uint16_t BASELINE_SAMPLES = 600; // ~30s se SAMPLE_MS=50

// Conversão "estimada" (ajuste no seu teste real)
// mg/L = (adc_filtrado - baseline_adc) * MG_PER_L_PER_ADC
static const float MG_PER_L_PER_ADC = 0.0025f;  // ajuste fino aqui
static const float MG_L_MAX = 2.50f;            // limite pra barra/escala

// Suavização
static const float EMA_ALPHA = 0.08f; // 0.05~0.15 (menor = mais suave)

// ===================== ESTADO =====================
enum State { ST_WARMUP, ST_RUNNING };
State st = ST_WARMUP;

uint32_t t0 = 0;
uint32_t lastSample = 0;

float adcEma = 0.0f;

uint32_t baselineCount = 0;
double baselineSum = 0;
float baselineAdc = 0;

// ===================== HELPERS =====================
static void drawCenterText(const String &s, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(s, 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  display.setCursor(x, y);
  display.print(s);
}

static void drawProgressBar(int x, int y, int w, int h, float pct) {
  if (pct < 0) pct = 0;
  if (pct > 1) pct = 1;
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  int fill = (int)((w - 2) * pct);
  display.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);
}

static void showWarmup(uint32_t elapsedSec) {
  uint32_t left = (elapsedSec >= WARMUP_SEC) ? 0 : (WARMUP_SEC - elapsedSec);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  drawCenterText("BAFOMETRO MQ-3", 0);

  display.setTextSize(1);
  drawCenterText("Aquecendo / Calibrando", 16);

  display.setTextSize(2);
  char buf[20];
  snprintf(buf, sizeof(buf), "%lus", (unsigned long)left);
  drawCenterText(String(buf), 32);

  float pct = (float)elapsedSec / (float)WARMUP_SEC;
  drawProgressBar(10, 56, 108, 7, pct);

  display.display();
}

static void showRunning(float mgL, int adcNow) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("MQ-3  | Base:");
  display.print((int)baselineAdc);

  display.setCursor(0, 12);
  display.print("ADC:");
  display.print(adcNow);

  display.setTextSize(2);
  display.setCursor(0, 28);
  display.print(mgL, 2);
  display.print(" mg/L");

  // barra 0..MG_L_MAX
  float pct = mgL / MG_L_MAX;
  drawProgressBar(10, 56, 108, 7, pct);

  display.display();
}

// ===================== SETUP =====================
void setup() {
  // Serial opcional
  Serial.begin(115200);

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    // se não achar 0x3C, tente 0x3D (alguns módulos são 0x3D)
    while (true) { delay(100); }
  }

  // ADC config (ESP32-S3)
  analogReadResolution(12); // 0..4095
  // Atenuacao p/ ler ate ~3.3V (aprox)
  analogSetPinAttenuation(MQ3_PIN, ADC_11db);

  // inicia EMA com uma leitura inicial
  int r0 = analogRead(MQ3_PIN);
  adcEma = (float)r0;

  t0 = millis();
  st = ST_WARMUP;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  drawCenterText("Iniciando...", 24);
  display.display();
}

// ===================== LOOP =====================
void loop() {
  uint32_t now = millis();

  // amostragem periódica
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;

    int adcRaw = analogRead(MQ3_PIN);

    // EMA
    adcEma = (EMA_ALPHA * (float)adcRaw) + ((1.0f - EMA_ALPHA) * adcEma);

    // durante warmup: acumula baseline (últimos ~30s, por exemplo)
    if (st == ST_WARMUP) {
      // começa a coletar baseline depois de um tempinho (ex: após 30s),
      // pra evitar a subida inicial mais agressiva do aquecimento.
      uint32_t elapsedSec = (now - t0) / 1000;

      if (elapsedSec >= 30 && baselineCount < BASELINE_SAMPLES) {
        baselineSum += adcEma;
        baselineCount++;
        baselineAdc = (float)(baselineSum / (double)baselineCount);
      }

      showWarmup(elapsedSec);

      if (elapsedSec >= WARMUP_SEC) {
        // se não coletou baseline suficiente, usa o valor atual
        if (baselineCount == 0) baselineAdc = adcEma;

        st = ST_RUNNING;
      }
    } else {
      // RUN
      float delta = adcEma - baselineAdc;
      if (delta < 0) delta = 0;

      float mgL = delta * MG_PER_L_PER_ADC;
      if (mgL < 0) mgL = 0;
      if (mgL > 9.99f) mgL = 9.99f; // evita estourar visualmente

      showRunning(mgL, (int)adcEma);

      // debug opcional
      // Serial.printf("raw=%d ema=%.1f base=%.1f mg/L=%.2f\n", adcRaw, adcEma, baselineAdc, mgL);
    }
  }
}
