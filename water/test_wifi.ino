/*
 * ============================================================================
 * ТЕСТ ПОДКЛЮЧЕНИЯ WIFI (ESP8266) К API СЕРВЕРУ
 * ============================================================================
 *
 * Оборудование:
 * - Arduino UNO - основной микроконтроллер
 * - ESP8266 (NodeMCU / HC-06 WiFi модуль) - подключается через SoftwareSerial
 *
 * Подключение ESP8266 к Arduino UNO:
 *   ESP8266 TX  -> Arduino пин 5 (RX для SoftwareSerial)
 *   ESP8266 RX  -> Arduino пин 4 (TX для SoftwareSerial)
 *   ESP8266 VCC -> 3.3V (НЕ 5V!)
 *   ESP8266 GND -> GND
 *
 * Примечание: ESP8266 работает на 3.3V логике. Если подключаете TX ESP8266
 * к Arduino (5V), нужен логический конвертер или делитель напряжения.
 *
 * Бaud rate для ESP8266: 9600 (стандартный) или 115200 (быстрый)
 *
 * ============================================================================
 */

#include <SoftwareSerial.h>

// =============================================================================
// КОНФИГУРАЦИЯ СЕТИ И СЕРВЕРА
// =============================================================================

const char* WIFI_SSID = "AS47";           // Имя WiFi сети
const char* WIFI_PASS = "240490398002";   // Пароль WiFi
const char* SERVER_IP = "213.171.25.91";  // IP адрес backend сервера
const int SERVER_PORT = 80;               // Порт HTTP (Backend FastAPI работает на 8000, но через nginx на 80)

// =============================================================================
// КОНФИГУРАЦИЯ ПИНОВ И ПОС
// =============================================================================

// SoftwareSerial для связи с ESP8266
// RX_PIN = 5 (принимает данные от ESP8266 TX)
// TX_PIN = 4 (передает данные на ESP8266 RX)
SoftwareSerial wifiSerial(5, 4);  // (RX_PIN, TX_PIN)

// Baud rate для ESP8266 - стандартно 9600, некоторые модули на 115200
const int WIFI_BAUD_RATE = 9600;

// =============================================================================
// ВРЕМЕННЫЕ НАСТРОЙКИ
// =============================================================================

const unsigned long CHECK_INTERVAL = 30000;  // 30 секунд - интервал проверки API
unsigned long lastCheckTime = 0;

// =============================================================================
// СОСТОЯНИЕ СИСТЕМЫ
// =============================================================================

bool wifiConnected = false;           // Флаг подключения к WiFi
bool ipConfigured = false;            // Флаг получения IP адреса
String currentIP = "";                // Текущий IP адрес модуля

// Бuffer для приема данных
String inputBuffer = "";
unsigned long lastResponseTime = 0;

// =============================================================================
// ПРОТОКОЛ AT КОМАНД ДЛЯ ESP8266
// =============================================================================

/*
 * Основные AT команды:
 * AT              - Проверка связи с модулем
 * AT+RST          - Перезагрузка модуля
 * AT+CWMODE=1     - Режим STA (клиент)
 * AT+CWJAP="SSID","PASS" - Подключение к сети
 * AT+CIFSR        - Получить IP адрес
 * AT+CIPSTART="TCP","IP",PORT - Установить TCP соединение
 * AT+CIPSEND=LEN  - Отправить данные
 * AT+CIPCLOSE     - Закрыть соединение
 *
 * Ответы модуля:
 * OK              - Команда выполнена успешно
 * ERROR           - Ошибка выполнения
 * WIFI CONNECTED  - Подключено к WiFi роутеру
 * WIFI GOT IP     - Получен IP адрес
 * CONNECT         - TCP соединение установлено
 * +HTTPRESP:      - HTTP ответ получен
 * DATA STARTED    - Начался прием данных
 */

// =============================================================================
// НАСТРОЙКА (SETUP)
// =============================================================================

void setup() {
  // Инициализация Serial для связи с ПК (монитор Serial)
  Serial.begin(9600);
  delay(100);

  // Инициализация SoftwareSerial для ESP8266
  wifiSerial.begin(WIFI_BAUD_RATE);
  delay(100);

  // Приветственное сообщение
  Serial.println("\r\n");
  Serial.println("=================================================");
  Serial.println("   WiFi Test - Arduino UNO + ESP8266");
  Serial.println("   Plant Watering System");
  Serial.println("=================================================");
  Serial.print("   WiFi SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("   Target Server: ");
  Serial.println(SERVER_IP);
  Serial.print("   Target Port: ");
  Serial.println(SERVER_PORT);
  Serial.print("   API Endpoint: /api/health");
  Serial.println("\r\n");

  // Даем модулю время инициализации
  delay(1000);

  // Первичная проверка связи с модулем
  Serial.println("[STEP 1] Проверка связи с ESP8266...");
  if (!testWiFiModule()) {
    Serial.println("   ОШИБКА: ESP8266 не отвечает!");
    Serial.println("   Проверьте подключение и питание модуля.");
  } else {
    Serial.println("   ESP8266 отвечает OK");
  }

  lastCheckTime = millis();
}

// =============================================================================
// ОСНОВНОЙ ЦИКЛ (LOOP)
// =============================================================================

void loop() {
  // 1. Обработка входящих данных от ESP8266
  processWiFiResponse();

  // 2. Если WiFi не подключен - пробуем подключиться
  if (!wifiConnected) {
    connectToWiFi();
    return;  // Ждем подключения, не продолжаем
  }

  // 3. Проверяем состояние IP адреса
  if (!ipConfigured) {
    checkIPAddress();
    return;
  }

  // 4. Каждые 30 секунд проверяем API health endpoint
  if (millis() - lastCheckTime >= CHECK_INTERVAL) {
    lastCheckTime = millis();
    checkAPIHealth();
  }

  delay(100);
}

// =============================================================================
// ФУНКЦИИ ДЛЯ РАБОТЫ С WIFI МОДУЛЕМ
// =============================================================================

/**
 * Тест связи с WiFi модулем
 * Отправляет "AT" и ждет "OK"
 */
bool testWiFiModule() {
  for (int i = 0; i < 3; i++) {
    wifiSerial.println("AT");
    delay(500);
    if (waitForResponse("OK", 1000)) {
      return true;
    }
    delay(500);
  }
  return false;
}

/**
 * Подключение к WiFi сети
 */
void connectToWiFi() {
  Serial.println("\r\n[STEP 2] Подключение к WiFi сети...");

  // Устанавливаем режим STA (клиент)
  wifiSerial.println("AT+CWMODE=1");
  if (!waitForResponse("OK", 2000)) {
    Serial.println("   ОШИБКА: Не удалось установить режим STA");
    delay(3000);
    return;
  }
  Serial.println("   Режим STA установлен");

  // Отключаемся если было подключение
  wifiSerial.println("AT+CWQCONN");
  delay(500);

  // Attempt подключение к сети
  Serial.print("   Подключаемся к ");
  Serial.println(WIFI_SSID);

  String connectCmd = "AT+CWJAP=\"";
  connectCmd += WIFI_SSID;
  connectCmd += "\",\"";
  connectCmd += WIFI_PASS;
  connectCmd += "\"";

  wifiSerial.println(connectCmd);

  // Ждем подтверждения подключения (до 30 секунд)
  unsigned long timeout = millis() + 30000;

  while (millis() < timeout) {
    if (waitForResponse("WIFI CONNECTED", 500)) {
      Serial.println("   [OK] WIFI CONNECTED - подключено к роутеру");

      if (waitForResponse("WIFI GOT IP", 5000)) {
        Serial.println("   [OK] WIFI GOT IP - получен IP адрес");
        wifiConnected = true;
        return;
      }
    }

    // Если получили ошибку подключения
    if (waitForResponse("FAIL", 500) || waitForResponse("ERROR", 500)) {
      Serial.println("   ОШИБКА: Не удалось подключиться к WiFi");
      Serial.println("   Проверьте SSID и пароль");
      delay(3000);
      return;
    }

    delay(500);
  }

  Serial.println("   TIMEOUT: Превышено время ожидания подключения");
  wifiConnected = false;
}

/**
 * Получение и отображение IP адреса
 */
void checkIPAddress() {
  Serial.println("\r\n[STEP 3] Получение IP адреса...");
  wifiSerial.println("AT+CIFSR");

  if (waitForResponse("+CIFSR:STAIP,", 3000)) {
    // Извлекаем IP из ответа
    if (inputBuffer.indexOf("+CIFSR:STAIP,") >= 0) {
      int start = inputBuffer.indexOf(",") + 1;
      int end = inputBuffer.indexOf("\"", start);
      if (end > start) {
        currentIP = inputBuffer.substring(start, end);
        ipConfigured = true;
        Serial.print("   [OK] IP адрес: ");
        Serial.println(currentIP);
        Serial.println("\r\n=================================================");
        Serial.println("   WiFi подключен и готов!");
        Serial.println("=================================================\r\n");
        return;
      }
    }
  }

  Serial.println("   ОШИБКА: Не получен IP адрес");
  ipConfigured = false;
  delay(3000);
}

// =============================================================================
// ФУНКЦИИ ДЛЯ РАБОТЫ С API
// =============================================================================

/**
 * Проверка health endpoint API
 * GET /api/health
 * Ожидается ответ: {"status": "ok"}
 */
void checkAPIHealth() {
  if (!wifiConnected || !ipConfigured) {
    Serial.println("\r\n[API CHECK] WiFi не подключен, пропускаем");
    return;
  }

  Serial.println("\r\n[API CHECK] Проверка сервера...");
  Serial.print("   Target: http://");
  Serial.print(SERVER_IP);
  Serial.println(":80/api/health");

  // 1. Устанавливаем TCP соединение
  String connectCmd = "AT+CIPSTART=\"TCP\",\"";
  connectCmd += SERVER_IP;
  connectCmd += "\",";
  connectCmd += SERVER_PORT;

  Serial.println("   [1] AT+CIPSTART...");
  wifiSerial.println(connectCmd);

  if (!waitForResponse("CONNECT", 5000)) {
    Serial.println("   ОШИБКА: Не удалось установить TCP соединение");
    wifiConnected = false;
    ipConfigured = false;
    return;
  }
  Serial.println("   [OK] TCP соединение установлено");

  // 2. Формируем HTTP GET запрос
  String httpRequest = "GET /api/health HTTP/1.1\r\n";
  httpRequest += "Host: " + String(SERVER_IP) + "\r\n";
  httpRequest += "Connection: close\r\n\r\n";

  // 3. Отправляем запрос на длину +2 (для некоторых модулей)
  int sendLength = httpRequest.length() + 2;
  Serial.print("   [2] AT+CIPSEND=");
  Serial.println(sendLength);

  wifiSerial.println("AT+CIPSEND=" + String(sendLength));

  if (!waitForResponse(">", 5000)) {
    Serial.println("   ОШИБКА: Модуль не готов к отправке данных");
    return;
  }
  Serial.println("   [OK] Модуль готов, отправляем HTTP запрос");

  // 4. Отправляем сам HTTP запрос
  wifiSerial.print(httpRequest);
  delay(500);
  Serial.println("   [OK] HTTP GET отправлен");

  // 5. Ждем ответа (модуль сам передаст данные после "OK")
  Serial.println("   [3] Ожидаем ответ от сервера...");

  // Ждем окончания ответа
  if (waitForEndOfResponse(10000)) {
    Serial.println("\r\n   --- Ответ сервера ---");
    Serial.println(inputBuffer);
    Serial.println("   -----------------------");

    // Проверяем на успешный ответ
    if (inputBuffer.indexOf("200 OK") >= 0 || inputBuffer.indexOf("\"status\"") >= 0) {
      Serial.println("   [OK] API ответ получен успешно!");
    } else if (inputBuffer.indexOf("Connection closed") >= 0) {
      Serial.println("   [OK] Соединение закрыто сервером");
    }
  } else {
    Serial.println("   TIMEOUT: Ответ не получен");
  }

  Serial.println();

  // Закрываем соединение
  wifiSerial.println("AT+CIPCLOSE");
  delay(1000);
  waitForResponse("CLOSED", 2000);
}

// =============================================================================
// ФУНКЦИИ ДЛЯ ОБРАБОТКИ ОТВЕТОВ
// =============================================================================

/**
 * Обработка входящих данных от ESP8266
 */
void processWiFiResponse() {
  while (wifiSerial.available()) {
    char c = (char)wifiSerial.read();

    // Перевод строки - конец сообщения
    if (c == '\n') {
      inputBuffer.trim();
      if (inputBuffer.length() > 0) {
        lastResponseTime = millis();
      }
      inputBuffer = "";
    } else if (c == '\r') {
      // Игнорируем \r
    } else {
      inputBuffer += c;
    }
  }
}

/**
 * Ждем конкретный ответ от модуля
 */
bool waitForResponse(const char* expected, unsigned long timeout) {
  unsigned long start = millis();

  while (millis() - start < timeout) {
    while (wifiSerial.available()) {
      char c = (char)wifiSerial.read();

      if (c == '\n') {
        inputBuffer.trim();

        // Проверяем на ожидаемую строку
        if (inputBuffer.indexOf(expected) >= 0) {
          Serial.println("      > " + inputBuffer);
          return true;
        }

        // Выводим все ответы для отладки
        if (inputBuffer.length() > 0 && inputBuffer != "OK" && inputBuffer != "data") {
          // Серьезные ответы показываем сразу
          if (inputBuffer.indexOf("WIFI") >= 0 ||
              inputBuffer.indexOf("ERROR") >= 0 ||
              inputBuffer.indexOf("FAIL") >= 0 ||
              inputBuffer.indexOf("CONNECT") >= 0) {
            Serial.println("      > " + inputBuffer);
          }
        }

        inputBuffer = "";
      } else if (c != '\r') {
        inputBuffer += c;
      }
    }
    delay(10);
  }
  return false;
}

/**
 * Ждем окончания всего ответа (несколько строк)
 */
bool waitForEndOfResponse(unsigned long timeout) {
  unsigned long start = millis();
  String fullResponse = "";
  bool gotResponse = false;

  while (millis() - start < timeout) {
    bool newData = false;

    while (wifiSerial.available()) {
      char c = (char)wifiSerial.read();
      gotResponse = true;
      newData = true;

      if (c == '\n') {
        inputBuffer.trim();
        fullResponse += inputBuffer + "\n";

        // Проверка на конец TCP соединения
        if (inputBuffer.indexOf("0,OK") >= 0 ||
            inputBuffer.indexOf("CLOSED") >= 0 ||
            inputBuffer.indexOf("Connection closed") >= 0) {
          inputBuffer = fullResponse;
          return true;
        }

        inputBuffer = "";
      } else if (c != '\r') {
        inputBuffer += c;
      }
    }

    if (!newData) {
      delay(50);
    }
  }

  // Если таймаут, сохраняем что получили
  if (gotResponse) {
    inputBuffer = fullResponse;
  }

  return gotResponse;
}
