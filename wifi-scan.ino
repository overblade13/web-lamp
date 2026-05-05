#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// ==================== КОНФИГУРАЦИЯ ПИНОВ ====================
#define LED_PIN 13              // Лампа (ШИМ управление)
#define SENSOR_PIN 4            // Сенсорная кнопка
#define MODE_BUTTON_PIN 15      // Кнопка переключения режимов
#define LIGHT_SENSOR_PIN 34     // Фоторезистор
#define I2C_SDA 21              // OLED SDA
#define I2C_SCL 22              // OLED SCL

// ==================== ПАРАМЕТРЫ СИСТЕМЫ ====================
#define BRIGHTNESS_STEPS 10
#define AUTO_MODE_THRESHOLD 2000
#define DEBOUNCE_DELAY 50
#define LONG_PRESS_TIME 1000
#define SAVE_DELAY 5000

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
Preferences preferences;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

struct SystemState {
  bool isPowered = false;
  uint8_t brightness = 5;
  bool isAutoMode = false;
  unsigned long lastSaveTime = 0;
  uint16_t lightLevel = 0;
  unsigned long eventCount = 0;
};

SystemState state;

unsigned long sensorPressTime = 0;
bool sensorPressed = false;
unsigned long lastDebounceTime = 0;
bool lastSensorState = HIGH;

unsigned long modePressTime = 0;
bool modeButtonPressed = false;

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Система управления лампой ===");
  
  // Инициализация пинов
  pinMode(LED_PIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  
  // Инициализация ШИМ (НОВЫЙ API для ESP32 Core 2.0+)
  ledcAttach(LED_PIN, 5000, 12);  // частота 5000 Гц, разрешение 12 бит
  
  // Инициализация I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Инициализация дисплея
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Ошибка: OLED дисплей не найден!");
    while (1);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Загрузка...");
  display.display();
  delay(1000);
  
  // Инициализация памяти
  preferences.begin("lamp_control", false);
  loadSettings();
  
  updateDisplay();
  Serial.println("Система инициализирована");
}

// ==================== ОСНОВНОЙ ЦИКЛ ====================
void loop() {
  readSensors();
  handleSensorButton();
  handleModeButton();
  updateLamp();
  
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 100) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  if (millis() - state.lastSaveTime > SAVE_DELAY) {
    saveSettings();
    state.lastSaveTime = millis();
  }
  
  delay(10);
}

// ==================== ЧТЕНИЕ ДАТЧИКОВ ====================
void readSensors() {
  state.lightLevel = analogRead(LIGHT_SENSOR_PIN);
  
  static uint16_t lightBuffer[5];
  static uint8_t bufferIndex = 0;
  lightBuffer[bufferIndex++] = state.lightLevel;
  if (bufferIndex >= 5) bufferIndex = 0;
  
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 5; i++) sum += lightBuffer[i];
  state.lightLevel = sum / 5;
}

// ==================== ОБРАБОТКА КНОПОК ====================
void handleSensorButton() {
  bool sensorState = digitalRead(SENSOR_PIN);
  
  if (sensorState != lastSensorState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (sensorState == LOW && !sensorPressed) {
      sensorPressed = true;
      sensorPressTime = millis();
      Serial.println("Сенсор нажат");
    }
    
    if (sensorState == HIGH && sensorPressed) {
      sensorPressed = false;
      unsigned long pressDuration = millis() - sensorPressTime;
      
      if (pressDuration < LONG_PRESS_TIME) {
        togglePower();
      } else {
        adjustBrightness();
      }
      
      state.eventCount++;
    }
  }
  
  lastSensorState = sensorState;
}

void handleModeButton() {
  bool modeState = digitalRead(MODE_BUTTON_PIN);
  
  if (modeState == LOW && !modeButtonPressed) {
    modeButtonPressed = true;
    modePressTime = millis();
  }
  
  if (modeState == HIGH && modeButtonPressed) {
    modeButtonPressed = false;
    if (millis() - modePressTime < 500) {
      toggleMode();
    }
  }
}

// ==================== УПРАВЛЕНИЕ ЛАМПОЙ ====================
void togglePower() {
  state.isPowered = !state.isPowered;
  Serial.printf("Питание: %s\n", state.isPowered ? "ВКЛ" : "ВЫКЛ");
  
  if (!state.isPowered) {
    ledcWrite(LED_PIN, 0);
  }
}

void adjustBrightness() {
  if (!state.isPowered) {
    state.isPowered = true;
  }
  
  state.brightness++;
  if (state.brightness > BRIGHTNESS_STEPS) {
    state.brightness = 0;
  }
  
  Serial.printf("Яркость: %d/10\n", state.brightness);
}

void toggleMode() {
  state.isAutoMode = !state.isAutoMode;
  Serial.printf("Режим: %s\n", state.isAutoMode ? "АВТО" : "РУЧНОЙ");
}

void updateLamp() {
  if (!state.isPowered) {
    ledcWrite(LED_PIN, 0);
    return;
  }
  
  uint16_t pwmValue;
  
  if (state.isAutoMode) {
    if (state.lightLevel < AUTO_MODE_THRESHOLD) {
      pwmValue = map(state.lightLevel, 0, AUTO_MODE_THRESHOLD, 4095, 0);
    } else {
      pwmValue = 0;
    }
  } else {
    pwmValue = map(state.brightness, 0, BRIGHTNESS_STEPS, 0, 4095);
  }
  
  pwmValue = constrain(pwmValue, 0, 4095);
  ledcWrite(LED_PIN, pwmValue);
}

// ==================== ДИСПЛЕЙ ====================
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  
  display.print(state.isPowered ? "[ON] " : "[OFF]");
  display.print(state.isAutoMode ? "AUTO" : "MANU");
  
  display.setCursor(0, 12);
  display.print("Light: ");
  display.print(state.lightLevel);
  
  display.setCursor(0, 24);
  display.print("Bright: ");
  drawProgressBar(70, 24, state.brightness * 5, 50, 10);
  
  display.setCursor(0, 36);
  display.print("Events: ");
  display.print(state.eventCount);
  
  display.setCursor(0, 48);
  display.print("PWM: ");
  display.print(ledcRead(LED_PIN));
  
  display.display();
}

void drawProgressBar(int x, int y, int percent, int width, int height) {
  display.drawRect(x, y, width, height, WHITE);
  int fillWidth = map(constrain(percent, 0, 100), 0, 100, 0, width - 2);
  display.fillRect(x + 1, y + 1, fillWidth, height - 2, WHITE);
}

// ==================== СОХРАНЕНИЕ НАСТРОЕК ====================
void saveSettings() {
  preferences.putBool("powered", state.isPowered);
  preferences.putUChar("brightness", state.brightness);
  preferences.putBool("autoMode", state.isAutoMode);
  preferences.putULong("events", state.eventCount);
  Serial.println("Настройки сохранены");
}

void loadSettings() {
  state.isPowered = preferences.getBool("powered", false);
  state.brightness = preferences.getUChar("brightness", 5);
  state.isAutoMode = preferences.getBool("autoMode", false);
  state.eventCount = preferences.getULong("events", 0);
  Serial.println("Настройки загружены");
}