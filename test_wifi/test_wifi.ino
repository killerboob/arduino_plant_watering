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

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ (минимум для экономии памяти)
// =============================================================================

unsigned long lastCheckTime = 0;  // Когда последний раз отправляли данные
bool            wifiConnected   = false; // Подключен ли WiFi сейчас

// Буферы для AT команд (ESP8266 общается текстовыми командами)
char txBuffer[128];  // Для отправки команд в WiFi модуль
char rxBuffer[256];  // Для получения ответов от WiFi модуля
int  rxIndex = 0;    // Текущая позиция в буфере приёма

// =============================================================================
// ФУНКЦИЯ: Подключение к WiFi сети
// =============================================================================
// Что делает:
// 1. Устанавливает режим работы модуля (Station = клиент)
// 2. Отключает режим доступа (не создаёт свою сеть)
// 3. Подключается к указанной сети (WIFI_SSID/WIFI_PASS)
//
// Результат: wifiConnected = true если успешно, false если нет

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

// Проверяет есть ли строка в последнем ответе
bool checkResponse(const char* expected) {
  return strstr(rxBuffer, expected) != NULL;
}

// =============================================================================
// ФУНКЦИИ ДАТЧИКОВ (ЗАГЛУШКИ - пока возвращают фиксированные значения)
// =============================================================================

// Возвращает влажность почвы (0-100%)
// СПОСОБНОСТЬ: Здесь будет реальный датчик (например, с аналогового пина)
int readSoilMoisture() {
  return 75;  // ЗАГЛУШКА: тестовое значение 75%
}

// Возвращает состояние протечки (0 = нет, 1 = есть)
// СПОСОБНОСТЬ: Здесь будет датчик протечки
int readLeak() {
  return 0;   // ЗАГЛУШКА: протечки нет
}

// Возвращает уровень воды в баке (0 = пусто, 1 = есть)
// СПОСОБНОСТЬ: Здесь будет датчик уровня
int readWaterReservoir() {
  return 0;   // ЗАГЛУШКА: бак пуст (тест)
}

// =============================================================================
// ФУНКЦИИ ИСПОЛНЕНИЯ КОМАНД
// =============================================================================

// Вкл/выкл насос
// СПОСОБНОСТЬ: Здесь будет digitalWrite на пин реле насоса
void setPump(bool state) {
  Serial.print(F("Pump command: "));
  Serial.println(state ? F("ON") : F("OFF"));
  // digitalWrite(PUMP_PIN, state ? HIGH : LOW);
}

// Вкл/выкл свет
// СПОСОБНОСТЬ: Здесь будет digitalWrite на пин реле света
void setLight(bool state) {
  Serial.print(F("Light command: "));
  Serial.println(state ? F("ON") : F("OFF"));
  // digitalWrite(LIGHT_PIN, state ? HIGH : LOW);
}

// =============================================================================
// ФУНКЦИЯ: Отправка данных на сервер (POST запрос)
// =============================================================================
// Что делает:
// 1. Открывает TCP соединение с сервером
// 2. Формирует JSON с данными датчиков
// 3. Отправляет POST запрос
// 4. Закрывает соединение
//
// Формат данных:
// {"soil moisture":75,"leak":0,"water reservoir":0}

void sendPostData(int soil_moisture, int leak, int water_reservoir) {
  if (!wifiConnected) return;

  Serial.println(F("POST data..."));

  // 1. Открываем TCP соединение на порт 80
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  sendATCommand(txBuffer, 5000);

  // 2. Формируем JSON с данными
  char json[128];
  snprintf(json, sizeof(json),
    "{\"soil moisture\":%d,\"leak\":%d,\"water reservoir\":%d}",
    soil_moisture, leak, water_reservoir);

  int jsonLen = strlen(json);

  // 3. Формируем полный HTTP POST запрос (как делает requests.post в Python)
  char httpRequest[256];
  snprintf(httpRequest, sizeof(httpRequest),
    "POST /smart-watering/2/post_endpoint HTTP/1.0\r\n"
    "Host: %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
    "%s",
    SERVER_IP, jsonLen, json);

  int reqLen = strlen(httpRequest);

  // 4. Отправляем запрос
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSEND=%d", reqLen);
  sendATCommand(txBuffer, 2000);

  if (waitForResponse(">", 2000)) {
    WIFI_SERIAL.print(httpRequest);
    Serial.println(F("POST sent"));
  }

  // 5. Закрываем соединение
  sendATCommand("AT+CIPCLOSE", 2000);
}

// =============================================================================
// ФУНКЦИЯ: Получение команд с сервера (GET запрос)
// =============================================================================
// Что делает:
// 1. Открывает TCP соединение с сервером
// 2. Отправляет GET запрос
// 3. Читает ответ (JSON с командами)
// 4. Парсит ответ на light и pump
// 5. Вызывает setLight() и setPump() с полученными значениями
//
// Формат ответа сервера:
// {"light":"true","pump":"true"}

void getServerCommand() {
  if (!wifiConnected) return;

  Serial.println(F("GET commands..."));

  // 1. Открываем TCP соединение
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  sendATCommand(txBuffer, 5000);

  // 2. Формируем GET запрос
  snprintf(txBuffer, sizeof(txBuffer),
    "GET /smart-watering/2/get_endpoint HTTP/1.0\r\nHost: %s\r\n\r\n",
    SERVER_IP);

  int reqLen = strlen(txBuffer);
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSEND=%d", reqLen);
  sendATCommand(txBuffer, 2000);

  // 3. Отправляем запрос и ждём ответ
  if (waitForResponse(">", 2000)) {
    WIFI_SERIAL.print(txBuffer);
  }

  delay(500);  // Даём время на получение ответа

  // 4. Парсим JSON ответ (ручной парсинг без библиотек)
  bool light = false;
  bool pump  = false;

  char* lightPos = strstr(rxBuffer, "\"light\"");
  if (lightPos && strstr(lightPos, "true")) {
    light = true;
  }

  char* pumpPos = strstr(rxBuffer, "\"pump\"");
  if (pumpPos && strstr(pumpPos, "true")) {
    pump = true;
  }

  // Выводим что получили
  Serial.print(F("Parsed: light="));
  Serial.print(light ? F("true") : F("false"));
  Serial.print(F(", pump="));
  Serial.println(pump ? F("true") : F("false"));

  // 5. Выполняем команды
  setLight(light);
  setPump(pump);

  // 6. Закрываем соединение
  sendATCommand("AT+CIPCLOSE", 2000);
}

// =============================================================================
// ФУНКЦИЯ: Основной цикл работы (вызывается каждые 10 секунд)
// =============================================================================
// Последовательность:
// 1. Считать все датчики
// 2. Отправить POST с данными на сервер
// 3. Получить GET запросом команды с сервера
// 4. Выполнить полученные команды

void processWiFiCycle() {
  // 1. Считываем датчики
  int soil_moisture   = readSoilMoisture();
  int leak            = readLeak();
  int water_reservoir = readWaterReservoir();

  // Вывод для отладки в Serial
  Serial.print(F("Sensors: soil_moisture="));
  Serial.print(soil_moisture);
  Serial.print(F(", leak="));
  Serial.print(leak);
  Serial.print(F(", water_reservoir="));
  Serial.println(water_reservoir);

  // 2. Отправляем данные на сервер
  sendPostData(soil_moisture, leak, water_reservoir);

  // 3. Получаем команды с сервера
  getServerCommand();
}

// =============================================================================
// SETUP и LOOP - точка входа Arduino
// =============================================================================

void setup() {
  // Открываем Serial для отладки (9600 baud)
  Serial.begin(9600);
  while (!Serial) { }
  Serial.println(F("=== Arduino WiFi Module ==="));

  // Открываем связь с WiFi модулем (9600 baud)
  WIFI_SERIAL.begin(9600);

  // Подключаемся к WiFi
  wifiConnect();
}

void loop() {
  // Получаем данные от WiFi модуля (не блокирующая версия)
  while (WIFI_SERIAL.available()) {
    char c = WIFI_SERIAL.read();
    if (rxIndex < sizeof(rxBuffer) - 1) {
      rxBuffer[rxIndex++] = c;
      rxBuffer[rxIndex] = '\0';
    }
  }

  // Проверяем прошли ли 10 секунд с последнего цикла
  if (millis() - lastCheckTime >= TELEMETRY_INTERVAL_MS) {
    lastCheckTime = millis();
    processWiFiCycle();  // Запускаем полный цикл
  }
}