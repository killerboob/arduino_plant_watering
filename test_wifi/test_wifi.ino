#include <SoftwareSerial.h>

// ========== КОНФИГУРАЦИЯ ==========
const char* WIFI_SSID = "AS47_EXT";
const char* WIFI_PASS = "240490398002";
const char* SERVER_IP = "213.171.25.91";
const int   SERVER_PORT = 80;

// SoftwareSerial на пинах 4 (RX) и 5 (TX)
// Подключение: TX модуля -> пин 4, RX модуля -> пин 5
SoftwareSerial wifiSerial(4, 5);  // (RX, TX)

// ========== ФУНКЦИИ РАБОТЫ С AT ==========
void sendCmd(const char* cmd) {
  wifiSerial.println(cmd);
  delay(300);
}

bool waitFor(const char* target, int timeout) {
  unsigned long start = millis();
  String buf = "";
  while (millis() - start < timeout) {
    while (wifiSerial.available()) {
      char c = wifiSerial.read();
      buf += c;
      if (buf.indexOf(target) != -1) return true;
      if (buf.length() > 100) buf = "";
    }
  }
  return false;
}

void flush() {
  while (wifiSerial.available()) wifiSerial.read();
}

// ========== SETUP ==========
void setup() {
  wifiSerial.begin(115200);
  delay(2000);

  // 1. Проверка AT
  flush();
  sendCmd("AT");
  if (!waitFor("OK", 2000)) errorHang();

  // 2. Режим станции
  sendCmd("AT+CWMODE_DEF=1");
  if (!waitFor("OK", 1000)) errorHang();

  // 3. Подключение к WiFi
  wifiSerial.print("AT+CWJAP_DEF=\"");
  wifiSerial.print(WIFI_SSID);
  wifiSerial.print("\",\"");
  wifiSerial.print(WIFI_PASS);
  wifiSerial.println("\"");

  if (!waitFor("WIFI GOT IP", 15000)) errorHang();

  // 4. GET запрос
  flush();
  wifiSerial.print("AT+CIPSTART=0,\"TCP\",\"");
  wifiSerial.print(SERVER_IP);
  wifiSerial.print("\",");
  wifiSerial.println(SERVER_PORT);
  if (!waitFor("CONNECT", 5000)) errorHang();

  String request = "GET /api/health HTTP/1.1\r\n";
  request += "Host: ";
  request += SERVER_IP;
  request += "\r\n";
  request += "Connection: close\r\n\r\n";

  wifiSerial.print("AT+CIPSEND=0,");
  wifiSerial.println(request.length());
  if (!waitFor(">", 3000)) errorHang();

  wifiSerial.print(request);

  // Если отправка прошла, сервер получит запрос — ты увидишь в его логах
  waitFor("SEND OK", 5000);
  wifiSerial.println("AT+CIPCLOSE=0");

  // Всё, больше ничего не делаем
}

void loop() {
  delay(86400000);
}

void errorHang() {
  while (1) delay(1000);
}