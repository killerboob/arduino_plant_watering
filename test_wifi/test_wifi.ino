#include <SoftwareSerial.h>

// ========== КОНФИГУРАЦИЯ ==========
const char* WIFI_SSID = "AS47";
const char* WIFI_PASS = "240490398002";
const char* SERVER_IP = "213.171.25.91";
const int   SERVER_PORT = 80;

// Пины для SoftwareSerial (Troyka Wi-Fi)
// Джамперы на Slot Shield должны быть в положении 2 и 3
SoftwareSerial wifiSerial(2, 3);  // (RX, TX)

// Пин для светодиода (Troyka LED "Пиранья")
const int LED_PIN = 6;

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ LED ==========
void ledBlink(int onTimeMs, int offTimeMs, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(onTimeMs);
    digitalWrite(LED_PIN, LOW);
    delay(offTimeMs);
  }
}

void ledSet(bool state) {
  digitalWrite(LED_PIN, state ? HIGH : LOW);
}

// ========== ФУНКЦИИ РАБОТЫ С AT-КОМАНДАМИ ==========
void sendCommand(const __FlashStringHelper* cmd) {
  wifiSerial.println(cmd);
  delay(300);
}

bool waitForResponse(const __FlashStringHelper* expected, int timeoutMs) {
  unsigned long start = millis();
  String buffer = "";
  while (millis() - start < timeoutMs) {
    while (wifiSerial.available()) {
      char c = wifiSerial.read();
      buffer += c;
      if (buffer.indexOf(expected) != -1) {
        return true;
      }
      if (buffer.length() > 100) buffer = "";
    }
  }
  return false;
}

void clearSerialBuffer() {
  while (wifiSerial.available()) wifiSerial.read();
}

// ========== ОСНОВНАЯ ЛОГИКА ==========
void setup() {
  pinMode(LED_PIN, OUTPUT);
  ledSet(false);

  // Стартовый сигнал: 3 быстрых вспышки
  ledBlink(200, 200, 3);
  delay(500);

  // Инициализация Serial для отладки (скорость 9600)
  Serial.begin(9600);
  wifiSerial.begin(115200);
  delay(2000);  // Ждём стабилизации модуля

  Serial.println(F("Начинаем подключение к WiFi..."));
  
  // ===== ИНДИКАЦИЯ: инициализация WiFi (медленное мигание) =====
  // Будем мигать пока не подключимся (таймаут 20 сек)
  unsigned long startWifi = millis();
  bool wifiConnected = false;
  while (millis() - startWifi < 20000) {
    ledBlink(1000, 1000, 1);  // одно медленное мигание
    
    sendCommand(F("AT"));
    if (waitForResponse(F("OK"), 500)) {
      wifiConnected = true;
      break;
    }
  }
  
  if (!wifiConnected) {
    Serial.println(F("Ошибка: модуль WiFi не отвечает"));
    errorLoop();
    return;
  }
  
  // Настройка режима станции
  sendCommand(F("AT+CWMODE_DEF=1"));
  waitForResponse(F("OK"), 1000);
  
  // Подключение к сети
  Serial.print(F("Подключаюсь к ")); Serial.println(WIFI_SSID);
  wifiSerial.print(F("AT+CWJAP_DEF=\""));
  wifiSerial.print(WIFI_SSID);
  wifiSerial.print(F("\",\""));
  wifiSerial.print(WIFI_PASS);
  wifiSerial.println(F("\""));
  
  if (!waitForResponse(F("WIFI GOT IP"), 15000)) {
    Serial.println(F("Не удалось подключиться к WiFi"));
    errorLoop();
    return;
  }
  
  Serial.println(F("WiFi подключен!"));
  // Сигнал успеха WiFi: 2 короткие вспышки, потом постоянно горим
  ledBlink(300, 300, 2);
  delay(1000);
  ledSet(true);   // светодиод горит постоянно – готов к POST
  
  // Отправляем POST запрос
  httpPostRequest();
}

void loop() {
  // После отправки запроса больше ничего не делаем (можно повесить на паузу)
  delay(60000);
}

void httpPostRequest() {
  Serial.println(F("Отправка POST /api/health..."));
  
  // ===== ИНДИКАЦИЯ: отправка (быстрое мигание) =====
  // Запоминаем состояние (выключим постоянное горение)
  ledSet(false);
  
  // 1. TCP соединение
  wifiSerial.print(F("AT+CIPSTART=0,\"TCP\",\""));
  wifiSerial.print(SERVER_IP);
  wifiSerial.print(F("\","));
  wifiSerial.println(SERVER_PORT);
  
  if (!waitForResponse(F("CONNECT"), 5000)) {
    Serial.println(F("Ошибка: нет соединения с сервером"));
    errorLoop();
    return;
  }
  
  // 2. Формируем запрос
  String body = "{\"status\":\"ok\"}";
  String headers = "";
  headers += "POST /api/health HTTP/1.1\r\n";
  headers += "Host: ";
  headers += SERVER_IP;
  headers += "\r\n";
  headers += "Content-Type: application/json\r\n";
  headers += "Connection: close\r\n";
  headers += "Content-Length: ";
  headers += body.length();
  headers += "\r\n\r\n";
  headers += body;
  
  // 3. Отправляем данные
  wifiSerial.print(F("AT+CIPSEND=0,"));
  wifiSerial.println(headers.length());
  
  if (!waitForResponse(F(">"), 3000)) {
    Serial.println(F("Ошибка: не получено приглашение >"));
    errorLoop();
    return;
  }
  
  wifiSerial.print(headers);
  
  if (waitForResponse(F("SEND OK"), 5000)) {
    Serial.println(F("Данные отправлены успешно, ждём ответ..."));
    // Ждём ответ от сервера (содержит +IPD)
    if (waitForResponse(F("+IPD"), 3000)) {
      // Прочитаем ответ для отладки
      delay(300);
      while (wifiSerial.available()) {
        Serial.write(wifiSerial.read());
      }
      Serial.println();
      // ===== УСПЕХ: длинная вспышка 2 секунды =====
      ledBlink(2000, 0, 1);
    } else {
      Serial.println(F("Сервер не ответил"));
      errorLoop();
      return;
    }
  } else {
    Serial.println(F("Ошибка отправки данных"));
    errorLoop();
    return;
  }
  
  // Закрываем соединение
  wifiSerial.println(F("AT+CIPCLOSE=0"));
  delay(500);
  
  // Всё успешно – светодиод гаснет до следующего цикла
  ledSet(false);
  Serial.println(F("POST выполнен. Жду следующего цикла (1 мин)"));
}

// Бесконечный цикл при ошибке – быстрые 5 вспышек, пауза
void errorLoop() {
  while (true) {
    ledBlink(200, 200, 5);
    delay(2000);
  }
}