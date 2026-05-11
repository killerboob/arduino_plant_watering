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
#include <string.h>   // для явного использования strstr, strchr, memset

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
const int TELEMETRY_INTERVAL_MS = 10000;

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =============================================================================

unsigned long lastCheckTime = 0;
bool          wifiConnected = false;

char txBuffer[128];
char rxBuffer[256];
int  rxIndex = 0;

// =============================================================================
// ПРОТОТИПЫ ФУНКЦИЙ
// =============================================================================
void wifiConnect();
void sendATCommand(const char* cmd, int timeout);
bool waitForResponse(const char* expected, int timeout);
bool waitForResponse(char expected, int timeout);   // перегрузка для символа '>'
void sendPostData(int soil_moisture, int leak, int water_reservoir);
void getServerCommand();
void processWiFiCycle();
int readSoilMoisture();
int readLeak();
int readWaterReservoir();
void setPump(bool state);
void setLight(bool state);

// =============================================================================
// ПОДКЛЮЧЕНИЕ К WIFI
// =============================================================================
void wifiConnect() {
  Serial.println(F("WiFi connecting..."));

  sendATCommand("AT+CWMODE=1", 1000);
  sendATCommand("AT+CIPMUX=0", 1000);

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
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// =============================================================================
void sendATCommand(const char* cmd, int timeout) {
  WIFI_SERIAL.println(cmd);
  waitForResponse("OK", timeout);
}

// Ожидание строки в ответе
bool waitForResponse(const char* expected, int timeout) {
  unsigned long start = millis();
  rxIndex = 0;
  while (millis() - start < timeout) {
    while (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = c;
        rxBuffer[rxIndex] = '\0';
      }
    }
    if (strstr(rxBuffer, expected) != NULL) {
      return true;
    }
    delay(10);
  }
  return false;
}

// Перегрузка: ожидание конкретного символа (например, '>')
bool waitForResponse(char expected, int timeout) {
  unsigned long start = millis();
  while (millis() - start < timeout) {
    if (WIFI_SERIAL.available()) {
      if (WIFI_SERIAL.read() == expected) return true;
    }
    delay(10);
  }
  return false;
}

// =============================================================================
// ДАТЧИКИ (ЗАГЛУШКИ)
// =============================================================================
int readSoilMoisture() { return 75; }
int readLeak()         { return 0;  }
int readWaterReservoir(){ return 0;  }

// =============================================================================
// ИСПОЛНЕНИЕ КОМАНД
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
// POST ЗАПРОС (отправка данных)
// =============================================================================
void sendPostData(int soil_moisture, int leak, int water_reservoir) {
  if (!wifiConnected) return;

  Serial.println(F("POST data..."));

  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  sendATCommand(txBuffer, 5000);

  char json[128];
  snprintf(json, sizeof(json),
    "{\"soil moisture\":%d,\"leak\":%d,\"water reservoir\":%d,\"human_name\":\"second\"}",
    soil_moisture, leak, water_reservoir);

  int jsonLen = strlen(json);
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
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSEND=%d", reqLen);
  sendATCommand(txBuffer, 2000);

  if (waitForResponse('>', 2000)) {
    WIFI_SERIAL.print(httpRequest);
    Serial.println(F("POST sent"));
  }

  sendATCommand("AT+CIPCLOSE", 2000);
}

// =============================================================================
// GET ЗАПРОС (получение команд) — РАБОЧАЯ ВЕРСИЯ
// =============================================================================
void getServerCommand() {
  if (!wifiConnected) return;

  Serial.println(F("[GET] Sending request..."));

  // 1. Открываем TCP соединение
  WIFI_SERIAL.print("AT+CIPSTART=\"TCP\",\"");
  WIFI_SERIAL.print(SERVER_IP);
  WIFI_SERIAL.println("\",80");
  if (!waitForResponse("OK", 5000)) {
    Serial.println(F("[GET] Connection failed"));
    return;
  }

  // 2. Точная длина HTTP-запроса
  int len = 0;
  len += strlen("GET /smart-watering/2/get_endpoint HTTP/1.1\r\n");
  len += strlen("Host: 213.171.25.91\r\n");
  len += strlen("Connection: close\r\n");
  len += 2; // финальное \r\n

  // 3. Команда CIPSEND
  WIFI_SERIAL.print("AT+CIPSEND=");
  WIFI_SERIAL.println(len);
  if (!waitForResponse('>', 2000)) {
    Serial.println(F("[GET] No prompt, sending anyway"));
  }

  // 4. Отправляем GET запрос
  WIFI_SERIAL.print("GET /smart-watering/2/get_endpoint HTTP/1.1\r\n");
  WIFI_SERIAL.print("Host: ");
  WIFI_SERIAL.print(SERVER_IP);
  WIFI_SERIAL.print("\r\n");
  WIFI_SERIAL.print("Connection: close\r\n");
  WIFI_SERIAL.print("\r\n");

  Serial.println(F("[GET] Request sent, waiting for response..."));

  // 5. Ждём 3 секунды, чтобы сервер успел ответить
  delay(3000);

  // 6. Читаем всё, что пришло в буфер
  rxIndex = 0;
  memset(rxBuffer, 0, sizeof(rxBuffer));
  unsigned long start = millis();
  while (millis() - start < 3000) {
    while (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = c;
        rxBuffer[rxIndex] = '\0';
      }
    }
  }

  // 7. Ищем JSON между { и }
  char* startJson = strchr(rxBuffer, '{');
  char* endJson   = strrchr(rxBuffer, '}');

  if (startJson && endJson && endJson > startJson) {
    int jsonLen = endJson - startJson + 1;
    char json[64];
    strncpy(json, startJson, jsonLen);
    json[jsonLen] = '\0';

    Serial.print(F("[GET] JSON: "));
    Serial.println(json);

    // Парсим (порядок полей может быть любым)
    bool light = (strstr(json, "\"light\":\"true\"") != NULL);
    bool pump  = (strstr(json, "\"pump\":\"true\"") != NULL);

    Serial.print(F("[GET] light="));
    Serial.print(light);
    Serial.print(F(", pump="));
    Serial.println(pump);

    setLight(light);
    setPump(pump);
  } else {
    Serial.print(F("[GET] No JSON found. Buffer: "));
    Serial.println(rxBuffer);
  }

  // 8. Закрываем соединение
  WIFI_SERIAL.println("AT+CIPCLOSE");
  waitForResponse("OK", 2000);
}

// =============================================================================
// ОСНОВНОЙ ЦИКЛ ОПРОСА
// =============================================================================
void processWiFiCycle() {
  int soil_moisture   = readSoilMoisture();
  int leak            = readLeak();
  int water_reservoir = readWaterReservoir();

  Serial.print(F("Sensors: soil_moisture="));
  Serial.print(soil_moisture);
  Serial.print(F(", leak="));
  Serial.print(leak);
  Serial.print(F(", water_reservoir="));
  Serial.println(water_reservoir);

  sendPostData(soil_moisture, leak, water_reservoir);
  getServerCommand();
}

// =============================================================================
// SETUP И LOOP
// =============================================================================
void setup() {
  Serial.begin(9600);
  while (!Serial) { }
  Serial.println(F("=== Arduino WiFi Module ==="));

  WIFI_SERIAL.begin(9600);
  // Принудительно устанавливаем скорость ESP на 9600 (если была иная)
  WIFI_SERIAL.println("AT+UART_DEF=9600,8,1,0,0");
  delay(100);

  wifiConnect();
}

void loop() {
  // Фоновый сбор данных от ESP (не блокирующий)
  while (WIFI_SERIAL.available()) {
    char c = WIFI_SERIAL.read();
    if (rxIndex < sizeof(rxBuffer) - 1) {
      rxBuffer[rxIndex++] = c;
      rxBuffer[rxIndex] = '\0';
    }
  }

  if (millis() - lastCheckTime >= TELEMETRY_INTERVAL_MS) {
    lastCheckTime = millis();
    processWiFiCycle();
  }
}