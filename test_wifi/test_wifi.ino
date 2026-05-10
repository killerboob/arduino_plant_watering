/*
 * WiFi модуль для системы полива на Arduino
 *
 * РАБОТА:
 * 1. При старте подключается к WiFi сети
 * 2. Каждые 10 секунд:
 *    - Считывает датчики (влажность земли, протечка, уровень воды)
 *    - Отправляет POST запрос на сервер с данными
 *    - Получает GET запросом команды (вкл/выкл насос, свет)
 *    - Выполняет полученные команды
 */

#include <SoftwareSerial.h>

// =============================================================================
// ПОДКЛЮЧЕНИЕ Wi-Fi МОДУЛЯ
// =============================================================================
// Arduino  ->  ESP8266 (WiFi модуль)
// Pin 4 TX ->  RX
// Pin 5 RX ->  TX
SoftwareSerial mySerial(4, 5);
#define WIFI_SERIAL mySerial

// =============================================================================
// НАСТРОЙКИ (редактируй под себя)
// =============================================================================

// Данные WiFi сети
const char* WIFI_SSID     = "AS47";         // Имя вашей WiFi сети
const char* WIFI_PASS     = "240490398002"; // Пароль от WiFi

// Адрес сервера, куда отправляем данные
const char* SERVER_IP     = "213.171.25.91";

// Как часто отправлять данные (в миллисекундах)
// 10000 = 10 секунд
const int TELEMETRY_INTERVAL_MS = 10000;

// ID устройства и имя
const int    DEVICE_ID         = 2;        // ID устройства (device_id)
const char* DEVICE_HUMAN_NAME = "second";   // Человеческое имя устройства

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ (минимум для экономии памяти)
// =============================================================================

unsigned long lastCheckTime = 0;  // Когда последний раз отправляли данные
bool            wifiConnected   = false; // Подключен ли WiFi сейчас

// Буферы для AT команд (ESP8266 общается текстовыми командами)
char txBuffer[256];  // Для отправки команд в WiFi модуль
char rxBuffer[512];  // Для получения ответов от WiFi модуля (увеличен для полного HTTP)
int  rxIndex = 0;    // Текущая позиция в буфере приёма

// =============================================================================
// ФУНКЦИЯ: Подключение к WiFi сети
// =============================================================================

void wifiConnect() {
  Serial.println(F("WiFi connecting..."));

  // Режим Station (клиент)
  sendATCommand("AT+CWMODE=1", 1000);

  // Single connection (одна сессия)
  sendATCommand("AT+CIPMUX=0", 1000);

  // Подключаемся к WiFi сети
  snprintf(txBuffer, sizeof(txBuffer), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
  if (waitForResponse("OK", 10000)) {
    wifiConnected = true;
    Serial.println(F("WiFi connected"));
  } else {
    wifiConnected = false;
    Serial.println(F("WiFi connect failed"));
  }
}

// =============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ для AT команд
// =============================================================================

// Отправляет AT команду и ждёт ответа "OK"
void sendATCommand(const char* cmd, int timeout) {
  WIFI_SERIAL.println(cmd);
  waitForResponse("OK", timeout);
}

// Ждёт ответ от WiFi модуля в течение timeout миллисекунд
// Возвращает true если найдена строка expected в ответе
bool waitForResponse(const char* expected, int timeout) {
  unsigned long start = millis();
  rxIndex = 0;

  while (millis() - start < timeout) {
    // Читаем всё что пришло от WiFi модуля
    while (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = c;
        rxBuffer[rxIndex] = '\0';
      }
    }

    // Проверили - есть ли нужная строка в ответе
    if (strstr(rxBuffer, expected) != NULL) {
      return true;
    }

    delay(10);
  }
  return false;
}

// =============================================================================
// ФУНКЦИИ ДАТЧИКОВ (ЗАГЛУШКИ - пока возвращают фиксированные значения)
// =============================================================================

int readSoilMoisture() {
  return 75;  // ЗАГЛУШКА: тестовое значение 75%
}

int readLeak() {
  return 0;   // ЗАГЛУШКА: протечки нет
}

int readWaterReservoir() {
  return 0;   // ЗАГЛУШКА: бак пуст (тест)
}

// =============================================================================
// ФУНКЦИИ ИСПОЛНЕНИЯ КОМАНД
// =============================================================================

void setPump(bool state) {
  Serial.print(F("Pump command: "));
  Serial.println(state ? F("ON") : F("OFF"));
  // digitalWrite(PUMP_PIN, state ? HIGH : LOW);
}

void setLight(bool state) {
  Serial.print(F("Light command: "));
  Serial.println(state ? F("ON") : F("OFF"));
  // digitalWrite(LIGHT_PIN, state ? HIGH : LOW);
}

// =============================================================================
// ФУНКЦИЯ: Отправка данных на сервер (POST запрос)
// =============================================================================

void sendPostData(int soil_moisture, int leak, int water_reservoir) {
  if (!wifiConnected) {
    Serial.println(F("Not connected to WiFi"));
    return;
  }

  Serial.println(F("POST data..."));

  // 1. Закрываем старое соединение если было
  sendATCommand("AT+CIPCLOSE", 1000);
  delay(500);

  // 2. Открываем TCP соединение на порт 80
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  if (!waitForResponse("CONNECT", 5000)) {
    Serial.println(F("Failed to connect"));
    return;
  }

  // 3. Формируем JSON с данными
  char json[200];
  snprintf(json, sizeof(json),
    "{\"soil moisture\":%d,\"leak\":%d,\"water reservoir\":%d,\"human_name\":\"%s\"}",
    soil_moisture, leak, water_reservoir, DEVICE_HUMAN_NAME);

  int jsonLen = strlen(json);

  // 4. Формируем полный HTTP POST запрос
  char httpRequest[512];
  snprintf(httpRequest, sizeof(httpRequest),
    "POST /smart-watering/%d/post_endpoint HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n"
    "\r\n"
    "%s",
    DEVICE_ID, SERVER_IP, jsonLen, json);

  int reqLen = strlen(httpRequest);
  Serial.print(F("Request length: "));
  Serial.println(reqLen);

  // 5. Отправляем команду CIPSEND
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSEND=%d", reqLen);
  WIFI_SERIAL.println(txBuffer);

  // 6. Ждём ">" от модуля
  if (!waitForResponse(">", 3000)) {
    Serial.println(F("Timeout waiting for >"));
    sendATCommand("AT+CIPCLOSE", 1000);
    return;
  }

  // 7. Отправляем сам HTTP запрос
  WIFI_SERIAL.print(httpRequest);
  Serial.println(F("POST sent successfully"));

  // 8. Ждём ответа от сервера
  delay(2000);

  // 9. Читаем ответ
  rxIndex = 0;
  unsigned long readTimeout = millis() + 3000;
  while (millis() < readTimeout) {
    while (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = c;
        rxBuffer[rxIndex] = '\0';
      }
    }
    delay(10);
  }

  Serial.print(F("Server response: "));
  Serial.println(rxBuffer);

  // 10. Закрываем соединение
  sendATCommand("AT+CIPCLOSE", 2000);
  delay(500);
}

// =============================================================================
// ФУНКЦИЯ: Получение команд с сервера (GET запрос)
// =============================================================================

void getServerCommand() {
  if (!wifiConnected) return;

  Serial.println(F("GET commands..."));

  // 1. Закрываем старое соединение
  sendATCommand("AT+CIPCLOSE", 1000);
  delay(500);

  // 2. Открываем TCP соединение
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  if (!waitForResponse("CONNECT", 5000)) {
    Serial.println(F("Failed to connect for GET"));
    return;
  }

  // 3. Формируем GET запрос в ОТДЕЛЬНОЙ строке
  char getRequest[150];
  snprintf(getRequest, sizeof(getRequest),
    "GET /smart-watering/%d/get_endpoint HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Connection: close\r\n"
    "\r\n",
    DEVICE_ID, SERVER_IP);

  int reqLen = strlen(getRequest);

  // 4. Отправляем команду CIPSEND
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSEND=%d", reqLen);
  WIFI_SERIAL.println(txBuffer);

  // 5. Ждём ">"
  if (!waitForResponse(">", 3000)) {
    Serial.println(F("Timeout waiting for >"));
    sendATCommand("AT+CIPCLOSE", 1000);
    return;
  }

  // 6. Отправляем GET запрос (НЕ txBuffer, а getRequest!)
  WIFI_SERIAL.print(getRequest);
  Serial.println(F("GET request sent"));

  // 7. Ждём и читаем ответ
  delay(1000);

  rxIndex = 0;
  unsigned long readTimeout = millis() + 3000;
  while (millis() < readTimeout) {
    while (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = c;
        rxBuffer[rxIndex] = '\0';
      }
    }
    delay(10);
  }

  Serial.print(F("RAW response: "));
  Serial.println(rxBuffer);

  // 8. Ищем JSON (после \r\n\r\n)
  char* jsonStart = strstr(rxBuffer, "\r\n\r\n");
  if (jsonStart) {
    jsonStart += 4;
    Serial.print(F("JSON: "));
    Serial.println(jsonStart);

    // 9. Парсим команды
    bool light = false;
    bool pump = false;

    char* lightPos = strstr(jsonStart, "\"light\"");
    if (lightPos && strstr(lightPos, "true")) {
      light = true;
    }

    char* pumpPos = strstr(jsonStart, "\"pump\"");
    if (pumpPos && strstr(pumpPos, "true")) {
      pump = true;
    }

    Serial.print(F("Parsed: light="));
    Serial.print(light ? F("ON") : F("OFF"));
    Serial.print(F(", pump="));
    Serial.println(pump ? F("ON") : F("OFF"));

    // 10. Выполняем команды
    setLight(light);
    setPump(pump);
  } else {
    Serial.println(F("No JSON found in response"));
  }

  // 11. Закрываем соединение
  sendATCommand("AT+CIPCLOSE", 2000);
  delay(500);
}

// =============================================================================
// ФУНКЦИЯ: Основной цикл работы
// =============================================================================

void processWiFiCycle() {
  int soil_moisture   = readSoilMoisture();
  int leak            = readLeak();
  int water_reservoir = readWaterReservoir();

  Serial.print(F("Sensors: soil="));
  Serial.print(soil_moisture);
  Serial.print(F(", leak="));
  Serial.print(leak);
  Serial.print(F(", water="));
  Serial.print(water_reservoir);
  Serial.print(F(", name="));
  Serial.println(DEVICE_HUMAN_NAME);

  // Сначала POST (отправляем данные)
  sendPostData(soil_moisture, leak, water_reservoir);

  // Потом GET (получаем команды)
  getServerCommand();
}

// =============================================================================
// SETUP и LOOP
// =============================================================================

void setup() {
  Serial.begin(9600);
  while (!Serial) { }
  Serial.println(F("=== Arduino WiFi Module ==="));

  WIFI_SERIAL.begin(9600);

  wifiConnect();
}

void loop() {
  // Собираем фоновые данные от WiFi модуля
  while (WIFI_SERIAL.available()) {
    char c = WIFI_SERIAL.read();
    if (rxIndex < sizeof(rxBuffer) - 1) {
      rxBuffer[rxIndex++] = c;
      rxBuffer[rxIndex] = '\0';
    }
  }

  // Проверяем таймер
  if (millis() - lastCheckTime >= TELEMETRY_INTERVAL_MS) {
    lastCheckTime = millis();
    processWiFiCycle();
  }
}