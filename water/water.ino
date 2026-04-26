/*
============================================================================
 SMART WATERING + TROYKA WiFi (AT-mode)
 Управление помпой и светом полностью через сервер (GET/POST).
 Интервал опроса: 30 сек (настраивается).
 Убраны: кнопка, пьезо, локальная логика автополива.
============================================================================
*/
#include <SoftwareSerial.h>

// === КОНФИГУРАЦИЯ СЕТИ И СЕРВЕРА ===
const char* WIFI_SSID     = "AS47";
const char* WIFI_PASS     = "240490398002";
const char* SERVER_IP     = "213.171.25.91";
const char* MACHINE_NAME  = "smart-watering";
const int   DEVICE_ID     = 2;
const char* HUMAN_NAME    = "second";

// Интервал обращения к серверу (мс)
const unsigned long SERVER_INTERVAL = 30000; 

// === ПИНЫ ===
// WiFi модуль (SoftwareSerial)
SoftwareSerial wifiSerial(5, 4); // RX=D5, TX=D4 (не меняйте, это стандарт Troyka WiFi)

// Датчики и исполнительные устройства
const int PIN_SOIL      = A5;  // Аналоговый датчик влажности
const int PIN_LEAK      = 2;   // Цифровой датчик протечки (LOW=протечка)
const int PIN_WATER     = 7;   // Цифровой датчик уровня воды (LOW=вода есть)
const int PIN_PUMP      = 11;   // Реле помпы (перенесено с D11 во избежание конфликта с TX)
const int PIN_LIGHT     = 12;   // Реле света
const int PIN_LED       = 10;   // Красный LED индикатор (перенесён с D10)

// === СОСТОЯНИЕ СИСТЕМЫ ===
unsigned long lastServerCheck = 0;
bool leakActive = false;
bool waterPresent = false;
int  soilRaw = 0;
bool wifiConnected = false;

// === ПРОТОТИПЫ ===
void setupPins();
bool ensureWiFi();
bool httpExchange(bool isPost, const String& payload, String& outResponse);
String buildPayload();
String findJsonValue(const String& json, const String& key); // <-- вынесена
void parseAndExecute(const String& response);
void updateIndicators();
String readWiFiLine(unsigned long timeout);

// ============================================================================
// SETUP / LOOP
// ============================================================================
void setup() {
  Serial.begin(9600);
  Serial.println(F("🚀 Smart Watering + WiFi init..."));
  
  setupPins();
  
  // Инициализация модуля
  wifiSerial.begin(9600);
  delay(1000);
  wifiSerial.println(F("AT+RST")); delay(1500);
  wifiSerial.println(F("AT+CWMODE_DEF=1")); delay(500);
  
  Serial.println(F("✅ Инициализация завершена. Подключение к WiFi..."));
  ensureWiFi();
  
  lastServerCheck = millis();
}

void loop() {
  // 1. Чтение датчиков
  soilRaw     = analogRead(PIN_SOIL);
  leakActive  = (digitalRead(PIN_LEAK) == LOW);
  waterPresent = (digitalRead(PIN_WATER) == LOW);

  // 2. Индикация
  updateIndicators();

  // 3. Цикл связи с сервером
  if (millis() - lastServerCheck >= SERVER_INTERVAL) {
    lastServerCheck = millis();
    Serial.println("\n📡 --- Server Cycle ---");
    
    if (ensureWiFi()) {
      String payload = buildPayload();
      String postResp = "";
      if (httpExchange(true, payload, postResp)) {
        Serial.println(F("✅ POST: Данные отправлены"));
        String getResp = "";
        if (httpExchange(false, "", getResp)) {
          Serial.println(F("📥 GET: Получены команды"));
          parseAndExecute(getResp);
        }
      } else {
        Serial.println(F("❌ Ошибка связи с сервером"));
      }
    } else {
      Serial.println(F("⚠️ WiFi отключен. Пропуск цикла."));
    }
  }
}

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================
void setupPins() {
  pinMode(PIN_LEAK, INPUT_PULLUP);
  pinMode(PIN_WATER, INPUT_PULLUP);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_LIGHT, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_PUMP, LOW);
  digitalWrite(PIN_LIGHT, LOW);
  digitalWrite(PIN_LED, LOW);
}

bool ensureWiFi() {
  if (wifiConnected) return true;
  
  Serial.print(F("📶 Подключение к ")); Serial.println(WIFI_SSID);
  wifiSerial.print(F("AT+CWJAP_DEF=\"")); wifiSerial.print(WIFI_SSID); 
  wifiSerial.print(F("\",\"")); wifiSerial.print(WIFI_PASS); wifiSerial.println(F("\""));
  
  String resp = readWiFiLine(15000);
  if (resp.indexOf("OK") >= 0) {
    wifiConnected = true;
    Serial.println(F("✅ WiFi подключён"));
    return true;
  }
  Serial.println(F("❌ Ошибка WiFi"));
  return false;
}

bool httpExchange(bool isPost, const String& payload, String& outResponse) {
  // Открываем TCP
  wifiSerial.println(F("AT+CIPSTART=\"TCP\",\"213.171.25.91\",80"));
  String s = readWiFiLine(3000);
  if (s.indexOf("CONNECT") < 0 && s.indexOf("ALREADY") < 0) return false;

  // Формируем запрос
  String req;
  if (isPost) {
    req = F("POST /"); req += MACHINE_NAME; req += "/"; req += DEVICE_ID; req += F("/post_endpoint HTTP/1.1\r\n");
    req += F("Host: "); req += SERVER_IP; req += "\r\n";
    req += F("Content-Type: application/json\r\n");
    req += F("Content-Length: "); req += payload.length(); req += "\r\n";
    req += F("Connection: close\r\n\r\n");
    req += payload;
  } else {
    req = F("GET /"); req += MACHINE_NAME; req += "/"; req += DEVICE_ID; req += F("/get_endpoint HTTP/1.1\r\n");
    req += F("Host: "); req += SERVER_IP; req += "\r\n";
    req += F("Connection: close\r\n\r\n");
  }

  // Отправляем
  wifiSerial.print(F("AT+CIPSEND=")); wifiSerial.println(req.length());
  delay(300);
  s = readWiFiLine(1000);
  if (s.indexOf(">") < 0) {
    wifiSerial.println(F("AT+CIPCLOSE")); return false;
  }
  
  wifiSerial.print(req);
  
  // Ждём ответ
  outResponse = "";
  unsigned long t0 = millis();
  while (millis() - t0 < 5000) {
    while (wifiSerial.available()) {
      outResponse += (char)wifiSerial.read();
      if (outResponse.length() > 1024) break;
    }
    if (outResponse.indexOf("200 OK") >= 0 || outResponse.indexOf("SEND OK") >= 0) break;
  }

  wifiSerial.println(F("AT+CIPCLOSE"));
  delay(200);
  
  return outResponse.indexOf("200") >= 0;
}

String buildPayload() {
  int soilPercent = map(soilRaw, 0, 1023, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);
  int leakVal   = leakActive ? 1 : 0;
  int waterVal  = waterPresent ? 1 : 0;
  
  String p = F("{\"soil moisture\":"); p += soilPercent;
  p += F(",\"leak\":"); p += leakVal;
  p += F(",\"water reservoir\":"); p += waterVal;
  p += F(",\"human_name\":\""); p += HUMAN_NAME; p += F("\"}");
  return p;
}

// ============================================================================
// ✅ ВЫНЕСЕННАЯ ФУНКЦИЯ ПАРСИНГА (глобальная, не внутри другой!)
// ============================================================================
String findJsonValue(const String& json, const String& key) {
  String searchKey = "\"" + key + "\":\"";
  int keyIndex = json.indexOf(searchKey);
  if (keyIndex < 0) return "";
  
  int valueStart = keyIndex + searchKey.length();
  int valueEnd = json.indexOf('"', valueStart);
  if (valueEnd <= valueStart) return "";
  
  String result = json.substring(valueStart, valueEnd);
  result.toLowerCase();
  return result;
}

// ============================================================================
void parseAndExecute(const String& resp) {
  String lightCmd = findJsonValue(resp, "light");
  String pumpCmd  = findJsonValue(resp, "pump");

  if (lightCmd.length() > 0) {
    bool turnOn = (lightCmd == "on" || lightCmd == "true" || lightCmd == "1");
    digitalWrite(PIN_LIGHT, turnOn ? HIGH : LOW);
    Serial.print(F("💡 Light: ")); 
    Serial.println(turnOn ? F("ON") : F("OFF"));
  }
  
  if (pumpCmd.length() > 0) {
    bool reqOn = (pumpCmd == "on" || pumpCmd == "true" || pumpCmd == "1");
    if (reqOn && (leakActive || !waterPresent)) {
      digitalWrite(PIN_PUMP, LOW);
      Serial.println(F("⚠️ Pump BLOCKED (leak/no water)!"));
    } else {
      digitalWrite(PIN_PUMP, reqOn ? HIGH : LOW);
      Serial.print(F("💧 Pump: ")); 
      Serial.println(reqOn ? F("ON") : F("OFF"));
    }
  }
}

void updateIndicators() {
  if (leakActive) {
    static unsigned long blinkT = 0;
    static bool state = false;
    if (millis() - blinkT >= 500) {
      blinkT = millis();
      state = !state;
      digitalWrite(PIN_LED, state ? HIGH : LOW);
    }
  } else {
    digitalWrite(PIN_LED, waterPresent ? LOW : HIGH);
  }
}

String readWiFiLine(unsigned long timeout) {
  String line;
  unsigned long t0 = millis();
  while (millis() - t0 < timeout) {
    while (wifiSerial.available()) {
      char c = wifiSerial.read();
      if (c == '\r' || c == '\n') return line;
      line += c;
      if (line.length() > 128) return line;
    }
  }
  return line;
}