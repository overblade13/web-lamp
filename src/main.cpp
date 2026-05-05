#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==========================================
// Настройки сети и сервера
// ==========================================
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* serverUrl = "https://web-lamp.vercel.app/api/telemetry"; // Боевой URL на Vercel (HTTPS)
const String deviceId = "b1a0fc95-8236-43be-af6a-cb315d05d75f"; // Уникальный ID устройства в Supabase

// ==========================================
// Настройки пинов (Пины ESP32)
// ==========================================
#define PIN_PHOTO 34
#define PIN_LED 13
#define PIN_BTN_TOUCH 4
#define PIN_BTN_MODE 15

// ==========================================
// Настройки ШИМ (LEDC) для LED
// ==========================================
#define LEDC_CHANNEL 0
#define LEDC_TIMER 12 // Разрешение 12 бит (0-4095)
#define LEDC_BASE_FREQ 5000 // Частота 5 кГц

// ==========================================
// Настройки OLED дисплея
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==========================================
// Локальная память (NVS)
// ==========================================
Preferences preferences;

// ==========================================
// Переменные состояния
// ==========================================
bool local_settings_changed = false;
bool is_powered = false;
bool is_auto_mode = true;
int brightness = 5; // Уровень яркости 0-10 в ручном режиме
int threshold = 2000;
unsigned long event_count = 0;

int current_pwm = 0;
int target_pwm = 0;
int filtered_light = 0;

// Таймеры
unsigned long lastTelemetryTime = 0;
unsigned long lastDisplayTime = 0;
unsigned long lastSaveTime = 0;

// Состояния кнопок (для защиты от дребезга и длительного нажатия)
bool lastTouchState = HIGH;
unsigned long touchPressTime = 0;
bool touchHandled = false;

bool lastModeState = HIGH;
unsigned long lastModeDebounceTime = 0;
bool modeHandled = false;

// ==========================================
// Прототипы функций
// ==========================================
void setupWiFi();
void readSensors();
void updateLogic();
void updateLED();
void updateDisplay();
void sendTelemetry();
void loadPreferences();
void savePreferences();
void handleButtons();

// ==========================================
// Инициализация
// ==========================================
void setup() {
  Serial.begin(115200);
  
  // Настройка пинов
  pinMode(PIN_BTN_TOUCH, INPUT_PULLUP);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  
  // Настройка ШИМ (LEDC)
  ledcSetup(LEDC_CHANNEL, LEDC_BASE_FREQ, LEDC_TIMER);
  ledcAttachPin(PIN_LED, LEDC_CHANNEL);
  
  // Настройка дисплея
  Wire.begin(21, 22); // SDA = 21, SCL = 22
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Booting Smart Lamp...");
  display.display();
  
  // Загрузка сохраненных настроек
  loadPreferences();
  
  // Подключение к Wi-Fi
  setupWiFi();
}

// ==========================================
// Главный цикл
// ==========================================
void loop() {
  unsigned long currentMillis = millis();

  readSensors();
  handleButtons();
  updateLogic();
  updateLED();
  
  // Обновление дисплея каждые 100 мс
  if (currentMillis - lastDisplayTime >= 100) {
    updateDisplay();
    lastDisplayTime = currentMillis;
  }
  
  // Отправка телеметрии каждые 3 секунды
  if (currentMillis - lastTelemetryTime >= 3000) {
    sendTelemetry();
    lastTelemetryTime = currentMillis;
  }
  
  // Автосохранение настроек каждые 5 секунд
  if (currentMillis - lastSaveTime >= 5000) {
    savePreferences();
    lastSaveTime = currentMillis;
  }
}

// ==========================================
// Подключение к Wi-Fi
// ==========================================
void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Failed. Continuing offline.");
  }
}

// ==========================================
// Чтение датчиков (фоторезистор с фильтрацией)
// ==========================================
void readSensors() {
  long sum = 0;
  for(int i = 0; i < 5; i++) {
    sum += analogRead(PIN_PHOTO);
    delay(2); // Небольшая задержка между замерами
  }
  filtered_light = sum / 5;
}

// ==========================================
// Обработка кнопок (защита от дребезга)
// ==========================================
void handleButtons() {
  unsigned long currentMillis = millis();
  
  // 1. Обработка кнопки MODE (Смена режима)
  bool modeState = digitalRead(PIN_BTN_MODE);
  if (modeState != lastModeState) {
    lastModeDebounceTime = currentMillis;
  }
  
  if ((currentMillis - lastModeDebounceTime) > 50) { // Дребезг 50мс
    if (modeState == LOW && !modeHandled) { // Кнопка нажата
      is_auto_mode = !is_auto_mode;
      event_count++;
      local_settings_changed = true;
      modeHandled = true;
      Serial.print("Mode toggled: ");
      Serial.println(is_auto_mode ? "AUTO" : "MANUAL");
    } else if (modeState == HIGH) {
      modeHandled = false; // Кнопка отпущена
    }
  }
  lastModeState = modeState;

  // 2. Обработка сенсорной кнопки (Вкл/Выкл и Яркость)
  bool touchState = digitalRead(PIN_BTN_TOUCH);
  
  if (touchState == LOW && lastTouchState == HIGH) { 
    // Только что нажали
    touchPressTime = currentMillis;
    touchHandled = false;
  } 
  else if (touchState == LOW && lastTouchState == LOW) { 
    // Удерживают
    if ((currentMillis - touchPressTime) > 1000) { // Длинное нажатие (>1 сек)
      if (!is_auto_mode && is_powered) {
        brightness++;
        if (brightness > 10) brightness = 0; // Циклично 0..10
        event_count++;
        local_settings_changed = true;
        touchPressTime = currentMillis; // Сброс времени, чтобы менять уровень каждую секунду
        touchHandled = true; // Помечаем, что обработали длинное нажатие
        Serial.print("Brightness changed to: ");
        Serial.println(brightness);
      } else {
        // Если в режиме AUTO или выключено, игнорируем длинное нажатие,
        // но блокируем срабатывание выключения при отпускании
        touchHandled = true; 
      }
    }
  }
  else if (touchState == HIGH && lastTouchState == LOW) { 
    // Отпустили кнопку
    unsigned long pressDuration = currentMillis - touchPressTime;
    if (pressDuration > 50 && pressDuration <= 1000 && !touchHandled) { 
      // Короткое нажатие (Вкл/Выкл)
      is_powered = !is_powered;
      event_count++;
      local_settings_changed = true;
      Serial.print("Power toggled: ");
      Serial.println(is_powered ? "ON" : "OFF");
    }
  }
  lastTouchState = touchState;
}

// ==========================================
// Логика работы устройства (Расчет целевого ШИМ)
// ==========================================
void updateLogic() {
  if (!is_powered) {
    target_pwm = 0;
  } else {
    if (is_auto_mode) {
      // Автоматический режим (если света меньше порога -> включаем LED)
      if (filtered_light < threshold) {
        target_pwm = 4095; // Максимальная яркость, если темно
      } else {
        target_pwm = 0; // Выключаем, если светло
      }
    } else {
      // Ручной режим: Преобразуем уровень 0-10 в ШИМ 0-4095
      target_pwm = map(brightness, 0, 10, 0, 4095);
    }
  }
}

// ==========================================
// Управление светом (Плавное изменение)
// ==========================================
void updateLED() {
  if (current_pwm < target_pwm) {
    current_pwm += 20; // Шаг увеличения (для плавности)
    if (current_pwm > target_pwm) current_pwm = target_pwm;
  } else if (current_pwm > target_pwm) {
    current_pwm -= 20; // Шаг уменьшения
    if (current_pwm < target_pwm) current_pwm = target_pwm;
  }
  ledcWrite(LEDC_CHANNEL, current_pwm);
}

// ==========================================
// Обновление OLED дисплея
// ==========================================
void updateDisplay() {
  display.clearDisplay();
  
  display.setCursor(0, 0);
  display.print("Power: ");
  display.println(is_powered ? "ON" : "OFF");
  
  display.print("Mode:  ");
  display.println(is_auto_mode ? "AUTO" : "MANUAL");
  
  display.print("Light: ");
  display.println(filtered_light);
  
  display.print("Events:");
  display.println(event_count);
  
  // Прогресс-бар (10 сегментов)
  // Отображаем текущий уровень ШИМ в виде полоски
  int bars = map(current_pwm, 0, 4095, 0, 10);
  
  display.setCursor(0, 48); // Смещаемся вниз
  display.print("Level: [");
  for (int i = 0; i < 10; i++) {
    if (i < bars) {
      display.print("=");
    } else {
      display.print(" ");
    }
  }
  display.print("]");
  
  display.display();
}

// ==========================================
// Отправка данных на сервер (Телеметрия)
// ==========================================
void sendTelemetry() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); // Отключаем проверку SSL для работы в симуляторе
    
    HTTPClient http;
    http.begin(client, serverUrl);
    http.addHeader("Content-Type", "application/json");
    
    // Формируем JSON
    StaticJsonDocument<256> doc;
    doc["device_id"] = deviceId;
    doc["light_level"] = filtered_light;
    doc["brightness"] = brightness;
    doc["is_powered"] = is_powered;
    doc["is_auto_mode"] = is_auto_mode;
    doc["pwm_value"] = current_pwm;
    doc["event_count"] = event_count;
    doc["local_settings_changed"] = local_settings_changed;
    
    String requestBody;
    serializeJson(doc, requestBody);
    
    // Отправляем POST запрос
    int httpResponseCode = http.POST(requestBody);
    
    if (httpResponseCode > 0) {
      local_settings_changed = false; // Сбрасываем флаг после успешной отправки
      String response = http.getString();
      
      // Парсим ответ с настройками от сервера
      StaticJsonDocument<256> respDoc;
      DeserializationError error = deserializeJson(respDoc, response);
      
      if (!error) {
        // Применяем настройки сервера, если они изменились
        if (respDoc.containsKey("is_auto_mode")) {
          bool srv_auto = respDoc["is_auto_mode"];
          if (srv_auto != is_auto_mode) is_auto_mode = srv_auto;
        }

        if (respDoc.containsKey("is_powered")) {
          bool srv_powered = respDoc["is_powered"];
          if (srv_powered != is_powered) is_powered = srv_powered;
        }
        
        if (respDoc.containsKey("brightness")) {
          int srv_bright = respDoc["brightness"];
          if (srv_bright >= 0 && srv_bright <= 10) { // Строгая валидация значения от сервера
            if (srv_bright != brightness) brightness = srv_bright;
          }
        }
        
        if (respDoc.containsKey("threshold")) {
          int srv_thresh = respDoc["threshold"];
          if (srv_thresh != threshold) threshold = srv_thresh;
        }
      }
    } else {
      Serial.print("Error sending POST, HTTP Code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}

// ==========================================
// Работа с локальной памятью (Загрузка)
// ==========================================
void loadPreferences() {
  preferences.begin("lamp", false);
  is_powered = preferences.getBool("is_powered", false);
  is_auto_mode = preferences.getBool("is_auto", true);
  brightness = preferences.getInt("bright", 5);
  if (brightness < 0 || brightness > 10) brightness = 5; // Сброс при поврежденной NVS памяти
  event_count = preferences.getULong("events", 0);
  threshold = preferences.getInt("threshold", 2000);
}

// ==========================================
// Работа с локальной памятью (Сохранение)
// ==========================================
void savePreferences() {
  // Библиотека Preferences проверяет, изменилось ли значение перед записью
  preferences.putBool("is_powered", is_powered);
  preferences.putBool("is_auto", is_auto_mode);
  preferences.putInt("bright", brightness);
  preferences.putULong("events", event_count);
  preferences.putInt("threshold", threshold);
}
