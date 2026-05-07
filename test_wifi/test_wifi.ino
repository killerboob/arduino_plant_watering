#include <SoftwareSerial.h>

const char* WIFI_SSID = "AS47";
const char* WIFI_PASS = "240490398002";
const char* SERVER_IP = "213.171.25.91";
const int   SERVER_PORT = 80;

// ПОДКЛЮЧЕНИЕ ПРАВИЛЬНОЕ: TX модуля -> пин 4, RX модуля -> пин 5
SoftwareSerial wifiSerial(4, 5);  // (RX пин Arduino, TX пин Arduino)

bool waitForResponse(String expected, int timeout) {
  unsigned long start = millis();
  String response = "";
  while (millis() - start < timeout) {
    while (wifiSerial.available()) {
      char c = wifiSerial.read();
      response += c;
      Serial.print(c);  // Печатаем ВСЕ ответы модуля в монитор порта
      if (response.indexOf(expected) != -1) return true;
      if (response.length() > 200) response = "";
    }
  }
  return false;
}

// СПЕЦИАЛЬНАЯ ФУНКЦИЯ ДЛЯ СКАНИРОВАНИЯ WIFI СЕТЕЙ
void scanWiFiNetworks() {
  Serial.println("\n--- СКАНИРОВАНИЕ ДОСТУПНЫХ СЕТЕЙ ---");
  wifiSerial.println("AT+CWLAP");
  
  unsigned long start = millis();
  while (millis() - start < 10000) {
    while (wifiSerial.available()) {
      Serial.write(wifiSerial.read());
    }
  }
  Serial.println("\n--- КОНЕЦ СКАНИРОВАНИЯ ---\n");
}

void setup() {
  Serial.begin(9600);
  while (!Serial);
  
  Serial.println("\n=================================");
  Serial.println("ESP8266 Troyka WiFi отладка");
  Serial.println("=================================\n");
  
  wifiSerial.begin(115200);
  delay(1000);
  
  // ШАГ 1: Проверка связи
  Serial.println("[1] Проверка связи с модулем (AT)...");
  wifiSerial.println("AT");
  if (!waitForResponse("OK", 2000)) {
    Serial.println("ОШИБКА: Модуль не отвечает! Проверь питание и подключение:");
    Serial.println("  - TX модуля -> пин 4 Arduino");
    Serial.println("  - RX модуля -> пин 5 Arduino");
    Serial.println("  - VCC -> 5V, GND -> GND");
    while(1);
  }
  Serial.println("[OK] Модуль отвечает!\n");
  
  // ШАГ 2: СКАНИРУЕМ WIFI СЕТИ (это то, что ты просил!)
  scanWiFiNetworks();
  
  // ШАГ 3: Режим станции
  Serial.println("[2] Настройка режима станции...");
  wifiSerial.println("AT+CWMODE_DEF=1");
  waitForResponse("OK", 1000);
  
  // ШАГ 4: Подключение к твоей сети
  Serial.println("[3] Подключение к " + String(WIFI_SSID) + "...");
  wifiSerial.print("AT+CWJAP_DEF=\"");
  wifiSerial.print(WIFI_SSID);
  wifiSerial.print("\",\"");
  wifiSerial.print(WIFI_PASS);
  wifiSerial.println("\"");
  
  if (!waitForResponse("WIFI GOT IP", 15000)) {
    Serial.println("ОШИБКА: Не удалось подключиться!");
    Serial.println("Проверь имя сети и пароль. Список доступных сетей выше.");
    while(1);
  }
  Serial.println("[OK] WiFi подключен!\n");
  
  // ШАГ 5: IP адрес
  Serial.println("[4] IP адрес модуля:");
  wifiSerial.println("AT+CIFSR");
  delay(1000);
  while(wifiSerial.available()) Serial.write(wifiSerial.read());
  
  // ШАГ 6: GET запрос
  Serial.println("\n[5] Отправка GET /api/health...");
  wifiSerial.print("AT+CIPSTART=0,\"TCP\",\"");
  wifiSerial.print(SERVER_IP);
  wifiSerial.print("\",");
  wifiSerial.println(SERVER_PORT);
  
  if (!waitForResponse("CONNECT", 5000)) {
    Serial.println("ОШИБКА: Сервер не отвечает!");
    while(1);
  }
  
  String request = "GET /api/health HTTP/1.1\r\n";
  request += "Host: " + String(SERVER_IP) + "\r\n";
  request += "Connection: close\r\n\r\n";
  
  wifiSerial.print("AT+CIPSEND=0,");
  wifiSerial.println(request.length());
  
  if (!waitForResponse(">", 3000)) {
    Serial.println("ОШИБКА: Модуль не готов к отправке");
    while(1);
  }
  
  wifiSerial.print(request);
  waitForResponse("SEND OK", 5000);
  waitForResponse("+IPD", 3000);
  
  Serial.println("\n[ГОТОВО] Сеанс завершен. Перезагрузи плату для повтора.");
}

void loop() {}