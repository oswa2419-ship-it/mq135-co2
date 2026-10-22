#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MQ135.h>
#include <Preferences.h>

#include <algorithm>

namespace Pin {
constexpr uint8_t SENSOR = 34;
constexpr uint8_t SDA = 21;
constexpr uint8_t SCL = 22;
constexpr uint8_t BTN = 0;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t OLED_W = 128;
constexpr uint8_t OLED_H = 64;

constexpr uint8_t LED_VERDE = 25;
constexpr uint8_t LED_AMARILLO = 26;
constexpr uint8_t LED_ROJO = 27;
constexpr uint8_t BUZZER = 14;
}

namespace Cal {
constexpr uint32_t PREHEAT_MS = 60000UL;
constexpr uint32_t WINDOW_MS = 120000UL;
constexpr uint16_t INTERVAL_MS = 3000;
constexpr uint8_t SAMPLES = 40;
constexpr float RZERO_FALLBACK = 76.63F;

constexpr float TEMP_NOMINAL = 20.0F;
constexpr float HUM_NOMINAL = 33.0F;
}

namespace Alerta {
constexpr uint16_t PPM_VERDE_H = 800;
constexpr uint16_t PPM_AMARILLO_H = 1200;
constexpr uint32_t BUZZ_TOGGLE_MS = 500;
constexpr uint32_t YELLOW_BEEP_INTERVAL_MS = 180000;
constexpr uint32_t YELLOW_BEEP_DURATION_MS = 250;
}

namespace Meas {
constexpr uint16_t INTERVAL_MS = 2000;
constexpr uint8_t RAW_SAMPLES = 16;
constexpr float EMA_ALPHA = 0.15F;
constexpr float BAR_FULL_SCALE = 2000.0F;
}

namespace Btn {
constexpr uint32_t HOLD_MS = 2000;
}

Adafruit_SSD1306 display(Pin::OLED_W, Pin::OLED_H, &Wire, -1);
MQ135 gas(Pin::SENSOR);
Preferences prefs;

bool calibrating = false;
bool calibrated = false;
float rzero = Cal::RZERO_FALLBACK;
float lastResistance = 0.0F;
float lastAdc = 0.0F;
float ppmFiltered = 0.0F;

float readResistance() {
  for (int i = 0; i < 8; ++i) {
    const float r = gas.getResistance();
    if (std::isfinite(r) && r > 0.5F && r < 2000.0F) return r;
    delay(20);
  }
  return NAN;
}

float averageResistance() {
  float acc = 0.0F;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < Meas::RAW_SAMPLES; ++i) {
    const float r = readResistance();
    if (std::isfinite(r)) {
      acc += r;
      ++valid;
    }
    delay(2);
  }
  if (valid == 0) return NAN;
  return acc / valid;
}

float resistanceToPPM(float r) {
  return PARA * pow(r / rzero, -PARB);
}

float median(float* values, uint8_t count) {
  std::sort(values, values + count,
            [](float a, float b) { return a < b; });
  if (count % 2) return values[count / 2];
  return (values[count / 2 - 1] + values[count / 2]) / 2.0F;
}

void drawProgress(const char* label, uint32_t elapsed, uint32_t total) {
  const uint8_t pct =
      total ? static_cast<uint8_t>(elapsed * 100UL / total) : 0;
  const uint32_t remaining = total > elapsed ? (total - elapsed) / 1000 : 0;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 2);
  display.print(label);
  display.setCursor(0, 12);
  display.print("Faltan ");
  display.print(remaining);
  display.println(" s");
  display.setCursor(0, 24);
  display.print(pct);
  display.print(" %");
  display.drawRect(0, 36, 128, 12, SSD1306_WHITE);
  display.fillRect(2, 38, (124 * pct) / 100, 8, SSD1306_WHITE);
  display.setCursor(0, 52);
  display.print("No toques el sensor");
  display.display();
}

void drawIdle() {
  const uint8_t fill =
      std::min<uint8_t>(124,
                        static_cast<uint8_t>(124.0F * ppmFiltered /
                                             Meas::BAR_FULL_SCALE));
  display.clearDisplay();
  display.drawRect(0, 0, 128, 6, SSD1306_WHITE);
  display.fillRect(2, 2, fill, 2, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(2, 10);
  display.print(ppmFiltered, 0);

  display.setTextSize(1);
  display.setCursor(2, 30);
  display.print("ppm  R ");
  display.print(lastResistance, 1);
  display.print("k");
  display.setCursor(0, 40);
  display.print("RZero ");
  display.print(rzero, 2);
  display.print(calibrated ? "k OK" : "k --");
  display.setCursor(0, 50);
  display.print("ADC ");
  display.print(lastAdc, 0);
  display.print(" Btn2s=");
  display.print(calibrated ? "cal" : "init");
  display.display();
}

bool handleButton() {
  static bool held = false;
  static bool fired = false;
  static uint32_t pressStart = 0;

  const bool pressed = digitalRead(Pin::BTN) == LOW;
  if (!pressed) {
    held = false;
    fired = false;
    return false;
  }
  if (!held) {
    held = true;
    pressStart = millis();
    return false;
  }
  if (!fired && millis() - pressStart >= Btn::HOLD_MS) {
    fired = true;
    return true;
  }
  return false;
}

bool runCalibration() {

  digitalWrite(Pin::LED_VERDE, LOW);
  digitalWrite(Pin::LED_AMARILLO, LOW);
  digitalWrite(Pin::LED_ROJO, LOW);
  digitalWrite(Pin::BUZZER, LOW);

  calibrating = true;

  const uint32_t heatStart = millis();
  while (millis() - heatStart < Cal::PREHEAT_MS) {
    drawProgress("Precalentando", millis() - heatStart, Cal::PREHEAT_MS);
    delay(200);
  }

  float samples[Cal::SAMPLES];
  uint8_t count = 0;
  const uint32_t start = millis();
  uint32_t next = start;

  while (millis() - start < Cal::WINDOW_MS) {
    if (static_cast<int32_t>(millis() - next) >= 0) {
      next += Cal::INTERVAL_MS;
      const float r = gas.getRZero();
      if (count < Cal::SAMPLES && std::isfinite(r) && r > 0.5F &&
          r < 2000.0F) {
        samples[count++] = r;
      }
    }
    drawProgress("Calibrando", millis() - start, Cal::WINDOW_MS);
    delay(100);
  }

  calibrating = false;

  if (count == 0) {
    Serial.println("Fallo la calibracion: sin lecturas validas.");
    return false;
  }

  rzero = median(samples, count);
  calibrated = true;

  prefs.begin("mq135", false);
  prefs.putFloat("rzero", rzero);
  prefs.end();

  Serial.printf("RZero calibrado: %.2f kOhm (%u muestras)\n", rzero, count);
  return true;
}

void handleSerial() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'c':
      case 'C':
        if (!calibrating) {
          Serial.println("Iniciando calibracion...");
          runCalibration();
        }
        break;
      case 'p':
      case 'P':
        Serial.printf("RZero=%.2f kOhm  CO2=%.0f ppm  R=%.2f kOhm\n", rzero,
                      ppmFiltered, lastResistance);
        break;
      case 'r':
      case 'R':
        Serial.printf("ADC=%.0f  R=%.2f kOhm\n", lastAdc, lastResistance);
        break;
      default:
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(Pin::BTN, INPUT_PULLUP);
  analogReadResolution(10);
  analogSetPinAttenuation(Pin::SENSOR, ADC_11db);

  pinMode(Pin::LED_VERDE, OUTPUT);
  pinMode(Pin::LED_AMARILLO, OUTPUT);
  pinMode(Pin::LED_ROJO, OUTPUT);
  pinMode(Pin::BUZZER, OUTPUT);
  digitalWrite(Pin::LED_VERDE, LOW);
  digitalWrite(Pin::LED_AMARILLO, LOW);
  digitalWrite(Pin::LED_ROJO, LOW);
  digitalWrite(Pin::BUZZER, LOW);

  prefs.begin("mq135", true);
  const float stored = prefs.getFloat("rzero", 0.0F);
  prefs.end();
  if (stored > 0.5F) {
    rzero = stored;
    calibrated = true;
  }

  Wire.begin(Pin::SDA, Pin::SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, Pin::OLED_ADDR)) {
    Serial.println("No se encontro la SSD1306 en 0x3C (revisa SDA/SCL).");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MQ-135 CO2 meter");
  display.println(calibrated ? "RZero guardado" : "Sin calibrar");
  display.println("Mantener BTN = recalibrar");
  display.display();

  Serial.println("Comandos: c=calibrar  p=print  r=raw");
  if (digitalRead(Pin::BTN) == LOW) {
    Serial.println("Boton presionado: forzando calibracion.");
    runCalibration();
  } else if (!calibrated) {
    Serial.println("Sin RZero guardado: arrancando calibracion inicial.");
    runCalibration();
  }
}

void loop() {
  handleSerial();
  if (calibrating) return;

  if (handleButton()) {
    Serial.println("Boton mantenido: forzando calibracion.");
    runCalibration();
    return;
  }

  static uint32_t next = 0;
  if (static_cast<int32_t>(millis() - next) < 0) return;
  next = millis() + Meas::INTERVAL_MS;

  const float r = averageResistance();
  if (!std::isfinite(r)) {
    Serial.println("Lectura invalida del sensor.");
    return;
  }

  lastResistance = r;
  lastAdc = analogRead(Pin::SENSOR);
  const float factor = gas.getCorrectionFactor(Cal::TEMP_NOMINAL, Cal::HUM_NOMINAL);
  const float corrected = factor > 0.0F ? r / factor : r;
  const float ppm = resistanceToPPM(corrected);

  ppmFiltered = (ppmFiltered == 0.0F) ? ppm
                                         : ppmFiltered +
                                               Meas::EMA_ALPHA * (ppm - ppmFiltered);

  {
    static uint32_t lastBuzzToggle = 0;
    static bool buzzState = false;
    static uint32_t lastYellowBeepStart = 0;
    static bool yellowBeepActive = false;
    static uint32_t yellowBeepStart = 0;
    static int lastState = -1;

    int state = 0;
    if (ppmFiltered < Alerta::PPM_VERDE_H) state = 0;
    else if (ppmFiltered < Alerta::PPM_AMARILLO_H) state = 1;
    else state = 2;

    if (lastState != state) {
      if (state == 1) {
        lastYellowBeepStart = millis();
      } else {
        yellowBeepActive = false;
        digitalWrite(Pin::BUZZER, LOW);
      }
      lastBuzzToggle = millis();
      buzzState = false;
      lastState = state;
    }

    if (state == 0) {
      digitalWrite(Pin::LED_VERDE, HIGH);
      digitalWrite(Pin::LED_AMARILLO, LOW);
      digitalWrite(Pin::LED_ROJO, LOW);
      digitalWrite(Pin::BUZZER, LOW);
      yellowBeepActive = false;
    } else if (state == 1) {
      digitalWrite(Pin::LED_VERDE, LOW);
      digitalWrite(Pin::LED_AMARILLO, HIGH);
      digitalWrite(Pin::LED_ROJO, LOW);
      if (yellowBeepActive) {
        if (millis() - yellowBeepStart >= Alerta::YELLOW_BEEP_DURATION_MS) {
          yellowBeepActive = false;
          digitalWrite(Pin::BUZZER, LOW);
        }
      } else {
        digitalWrite(Pin::BUZZER, LOW);
        if (millis() - lastYellowBeepStart >= Alerta::YELLOW_BEEP_INTERVAL_MS) {
          yellowBeepStart = millis();
          yellowBeepActive = true;
          digitalWrite(Pin::BUZZER, HIGH);
        }
      }
    } else {
      digitalWrite(Pin::LED_VERDE, LOW);
      digitalWrite(Pin::LED_AMARILLO, LOW);
      digitalWrite(Pin::LED_ROJO, HIGH);
      yellowBeepActive = false;
      if (millis() - lastBuzzToggle >= Alerta::BUZZ_TOGGLE_MS) {
        lastBuzzToggle = millis();
        buzzState = !buzzState;
        digitalWrite(Pin::BUZZER, buzzState ? HIGH : LOW);
      }
    }
  }

  Serial.printf("RZero=%.2f kOhm  R=%.2f kOhm  CO2=%.0f ppm\n", rzero,
                lastResistance, ppmFiltered);

  drawIdle();
}