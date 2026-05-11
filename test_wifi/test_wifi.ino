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
#include <string.h>
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

  // 2. Формируем JSON с данными + human_name
  char json[128];
  snprintf(json, sizeof(json),
    "{\"soil moisture\":%d,\"leak\":%d,\"water reservoir\":%d,\"human_name\":\"second\"}",
    soil_moisture, leak, water_reservoir);

  int jsonLen = strlen(json);

  // 3. Формируем полный HTTP POST запрос
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

  Serial.println(F("[GET] Sending request..."));

  // ===== 1. Очистка буфера и проверка готовности ESP =====
  memset(rxBuffer, 0, sizeof(rxBuffer));
  rxIndex = 0;
  while (WIFI_SERIAL.available()) WIFI_SERIAL.read();  // удаляем хлам из SoftwareSerial

  WIFI_SERIAL.println("AT");
  if (!waitForResponse("OK", 2000)) {
    Serial.println(F("[GET] ESP not ready"));
    return;
  }

  // Закрываем возможное предыдущее соединение
  WIFI_SERIAL.println("AT+CIPCLOSE");
  waitForResponse("OK", 2000);
  delay(100);

  // ===== 2. TCP подключение =====
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  WIFI_SERIAL.println(txBuffer);

  bool connected = false;
  if (waitForResponse("OK", 10000)) {
    connected = true;
  } else if (waitForResponse("ALREADY CONNECTED", 1000)) {
    connected = true;
  }

  if (!connected) {
    Serial.println(F("[GET] TCP connect failed"));
    return;
  }
  Serial.println(F("[GET] TCP connected"));

  // ===== 3. Точная длина HTTP-запроса =====
  int len = 0;
  len += strlen("GET /smart-watering/2/get_endpoint HTTP/1.1\r\n");
  len += strlen("Host: 213.171.25.91\r\n");
  len += strlen("Connection: close\r\n");
  len += 2; // финальное \r\n

  // ===== 4. CIPSEND =====
  WIFI_SERIAL.print("AT+CIPSEND=");
  WIFI_SERIAL.println(len);

  if (!waitForResponse('>', 5000)) {
    Serial.println(F("[GET] No '>' prompt"));
  }

  // ===== 5. Отправляем GET запрос =====
  WIFI_SERIAL.print("GET /smart-watering/2/get_endpoint HTTP/1.1\r\n");
  WIFI_SERIAL.print("Host: 213.171.25.91\r\n");
  WIFI_SERIAL.print("Connection: close\r\n");
  WIFI_SERIAL.print("\r\n");
  Serial.println(F("[GET] Request sent, reading response..."));

  // ===== 6. Читаем ответ до "CLOSED" или таймаута 10 сек =====
  memset(rxBuffer, 0, sizeof(rxBuffer));
  rxIndex = 0;

  unsigned long start = millis();
  while (millis() - start < 10000) {
    while (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = c;
        rxBuffer[rxIndex] = '\0';
      }
      // Увидели закрытие – выходим
      if (strstr(rxBuffer, "CLOSED")) {
        start = 0; // метка для выхода из внешнего while
        break;
      }
    }
    if (start == 0) break;
    delay(1);
  }

  Serial.print(F("[GET] Raw buffer: "));
  Serial.println(rxBuffer);

  // ===== 7. Ищем JSON =====
  char* startJson = strchr(rxBuffer, '{');
  char* endJson   = strrchr(rxBuffer, '}');

  if (startJson && endJson && endJson > startJson) {
    int jsonLen = endJson - startJson + 1;
    char json[64];
    strncpy(json, startJson, jsonLen);
    json[jsonLen] = '\0';

    Serial.print(F("[GET] JSON: "));
    Serial.println(json);

    bool light = (strstr(json, "\"light\":\"true\"") != NULL);
    bool pump  = (strstr(json, "\"pump\":\"true\"") != NULL);

    Serial.print(F("[GET] light="));
    Serial.print(light);
    Serial.print(F(", pump="));
    Serial.println(pump);

    setLight(light);
    setPump(pump);
  } else {
    Serial.println(F("[GET] No JSON found"));
  }

  // ===== 8. Закрываем соединение =====
  WIFI_SERIAL.println("AT+CIPCLOSE");
  waitForResponse("OK", 2000);
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
  WIFI_SERIAL.println("AT+UART_DEF=9600,8,1,0,0");
  delay(100);
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
