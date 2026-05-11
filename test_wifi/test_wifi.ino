#include <SoftwareSerial.h>
#include <string.h>

SoftwareSerial mySerial(4, 5);
#define WIFI_SERIAL mySerial

const char* WIFI_SSID     = "AS47";
const char* WIFI_PASS     = "240490398002";
const char* SERVER_IP     = "213.171.25.91";

const int TELEMETRY_INTERVAL_MS = 10000;

bool wifiConnected = false;
unsigned long lastCheckTime = 0;

char txBuffer[128];
char rxBuffer[256];      // глобальный буфер для waitForResponse
int rxIndex = 0;

// ===================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ =====================
// Каждый раз очищает буфер и ждёт строку
bool waitForResponse(const char* expected, unsigned long timeout) {
  memset(rxBuffer, 0, sizeof(rxBuffer));
  rxIndex = 0;
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = c;
        rxBuffer[rxIndex] = '\0';
      }
      if (strstr(rxBuffer, expected) != NULL) return true;
    }
  }
  return false;
}

bool waitForChar(char expected, unsigned long timeout) {
  unsigned long start = millis();
  while (millis() - start < timeout) {
    if (WIFI_SERIAL.available()) {
      if (WIFI_SERIAL.read() == expected) return true;
    }
  }
  return false;
}

void flushESP() {
  while (WIFI_SERIAL.available()) WIFI_SERIAL.read();
}

// ===================== ПОДКЛЮЧЕНИЕ К WIFI =====================
void wifiConnect() {
  Serial.println(F("WiFi connecting..."));
  WIFI_SERIAL.println("AT+CWMODE=1");
  waitForResponse("OK", 2000);
  WIFI_SERIAL.println("AT+CIPMUX=0");
  waitForResponse("OK", 2000);
  snprintf(txBuffer, sizeof(txBuffer), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
  WIFI_SERIAL.println(txBuffer);
  if (waitForResponse("OK", 12000)) {
    wifiConnected = true;
    Serial.println(F("WiFi connected"));
  } else {
    wifiConnected = false;
    Serial.println(F("WiFi connect failed"));
  }
}

// ===================== ДАТЧИКИ (заглушки) =====================
int readSoilMoisture() { return 75; }
int readLeak()         { return 0; }
int readWaterReservoir(){ return 0; }
void setPump(bool state)  { Serial.println(state ? F("PUMP ON") : F("PUMP OFF")); }
void setLight(bool state) { Serial.println(state ? F("LIGHT ON") : F("LIGHT OFF")); }

// ===================== POST ЗАПРОС =====================
void sendPostData(int soil_moisture, int leak, int water_reservoir) {
  if (!wifiConnected) return;

  Serial.println(F(">>> POST start"));

  // 1. Закрываем всё, что могло остаться открытым
  flushESP();
  WIFI_SERIAL.println("AT+CIPCLOSE");
  waitForResponse("OK", 2000);

  // 2. TCP подключение
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  WIFI_SERIAL.println(txBuffer);
  if (!waitForResponse("OK", 10000) && !waitForResponse("ALREADY CONNECTED", 1000)) {
    Serial.println(F("[POST] TCP error"));
    return;
  }
  Serial.println(F("[POST] TCP ok"));

  // 3. Готовим JSON
  char json[128];
  snprintf(json, sizeof(json),
    "{\"soil moisture\":%d,\"leak\":%d,\"water reservoir\":%d,\"human_name\":\"second\"}",
    soil_moisture, leak, water_reservoir);
  int jsonLen = strlen(json);

  // 4. HTTP-запрос
  char httpRequest[256];
  int reqLen = snprintf(httpRequest, sizeof(httpRequest),
    "POST /smart-watering/2/post_endpoint HTTP/1.0\r\n"
    "Host: %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
    "%s",
    SERVER_IP, jsonLen, json);

  // 5. CIPSEND
  WIFI_SERIAL.print("AT+CIPSEND=");
  WIFI_SERIAL.println(reqLen);
  if (!waitForChar('>', 5000)) {
    Serial.println(F("[POST] No >"));
    return;
  }

  // 6. Отправка данных
  WIFI_SERIAL.print(httpRequest);
  Serial.println(F("[POST] data sent"));

  // 7. Ждём SEND OK
  if (waitForResponse("SEND OK", 5000)) {
    Serial.println(F("[POST] SEND OK"));
  } else {
    Serial.println(F("[POST] no SEND OK"));
  }

  // 8. Закрываем TCP
  WIFI_SERIAL.println("AT+CIPCLOSE");
  waitForResponse("OK", 2000);
  flushESP();
  Serial.println(F("<<< POST end"));
}

// ===================== GET ЗАПРОС =====================
void getServerCommand() {
  if (!wifiConnected) return;

  Serial.println(F(">>> GET start"));

  // 1. Закрываем старое соединение
  flushESP();
  WIFI_SERIAL.println("AT+CIPCLOSE");
  waitForResponse("OK", 2000);

  // 2. TCP подключение
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  WIFI_SERIAL.println(txBuffer);
  if (!waitForResponse("OK", 10000) && !waitForResponse("ALREADY CONNECTED", 1000)) {
    Serial.println(F("[GET] TCP error"));
    return;
  }
  Serial.println(F("[GET] TCP ok"));

  // 3. Длина GET-запроса
  int len = 0;
  len += strlen("GET /smart-watering/2/get_endpoint HTTP/1.1\r\n");
  len += strlen("Host: 213.171.25.91\r\n");
  len += strlen("Connection: close\r\n");
  len += 2; // финальный \r\n

  // 4. CIPSEND
  WIFI_SERIAL.print("AT+CIPSEND=");
  WIFI_SERIAL.println(len);
  if (!waitForChar('>', 5000)) {
    Serial.println(F("[GET] No >"));
    return;
  }

  // 5. Отправляем запрос
  WIFI_SERIAL.print("GET /smart-watering/2/get_endpoint HTTP/1.1\r\n");
  WIFI_SERIAL.print("Host: 213.171.25.91\r\n");
  WIFI_SERIAL.print("Connection: close\r\n");
  WIFI_SERIAL.print("\r\n");

  Serial.println(F("[GET] request sent, read response..."));

  // 6. Читаем ответ в ЛОКАЛЬНЫЙ буфер
  char getBuffer[256];
  memset(getBuffer, 0, sizeof(getBuffer));
  int idx = 0;
  unsigned long start = millis();
  while (millis() - start < 10000) {
    while (WIFI_SERIAL.available()) {
      char c = WIFI_SERIAL.read();
      if (idx < sizeof(getBuffer) - 1) {
        getBuffer[idx++] = c;
        getBuffer[idx] = '\0';
      }
      if (strstr(getBuffer, "CLOSED")) {
        start = 0; // выход из внешнего цикла
        break;
      }
    }
    if (start == 0) break;
    delay(1);
  }

  Serial.print(F("[GET] buffer: "));
  Serial.println(getBuffer);

  // 7. Ищем JSON
  char* startJson = strchr(getBuffer, '{');
  char* endJson   = strrchr(getBuffer, '}');
  if (startJson && endJson && endJson > startJson) {
    int jsonLen = endJson - startJson + 1;
    char json[64];
    strncpy(json, startJson, jsonLen);
    json[jsonLen] = '\0';
    Serial.print(F("[GET] JSON: "));
    Serial.println(json);

    // Проверяем наличие ключа "light"
    char* lightKey = strstr(json, "\"light\":");
    if (lightKey != NULL) {
      // Ищем значение после ключа
      if (strstr(lightKey, "\"light\":\"true\"")) {
        Serial.println(F("[GET] light=true -> ON"));
        setLight(true);
      } else if (strstr(lightKey, "\"light\":\"false\"")) {
        Serial.println(F("[GET] light=false -> OFF"));
        setLight(false);
      } else {
        Serial.println(F("[GET] light value unknown, ignoring"));
      }
    } else {
      Serial.println(F("[GET] no light command"));
    }

    // Проверяем наличие ключа "pump"
    char* pumpKey = strstr(json, "\"pump\":");
    if (pumpKey != NULL) {
      if (strstr(pumpKey, "\"pump\":\"true\"")) {
        Serial.println(F("[GET] pump=true -> ON"));
        setPump(true);
      } else if (strstr(pumpKey, "\"pump\":\"false\"")) {
        Serial.println(F("[GET] pump=false -> OFF"));
        setPump(false);
      } else {
        Serial.println(F("[GET] pump value unknown, ignoring"));
      }
    } else {
      Serial.println(F("[GET] no pump command"));
    }
  } else {
    Serial.println(F("[GET] no JSON found"));
  }
  
  // 8. Закрываем TCP
  WIFI_SERIAL.println("AT+CIPCLOSE");
  waitForResponse("OK", 2000);
  flushESP();
  Serial.println(F("<<< GET end"));
}

// ===================== ЦИКЛ ОТПРАВКИ =====================
void processWiFiCycle() {
  int soil = readSoilMoisture();
  int leak = readLeak();
  int water = readWaterReservoir();

  Serial.print(F("Sensors: soil=")); Serial.print(soil);
  Serial.print(F(", leak=")); Serial.print(leak);
  Serial.print(F(", water=")); Serial.println(water);

  sendPostData(soil, leak, water);  // сначала POST
  delay(200);                       // даём ESP освободиться
  getServerCommand();               // потом GET
}

// ===================== SETUP / LOOP =====================
void setup() {
  Serial.begin(9600);
  while (!Serial) { }
  Serial.println(F("=== Arduino WiFi Module ==="));

  WIFI_SERIAL.begin(9600);
  // Принудительно устанавливаем скорость 9600
  WIFI_SERIAL.println("AT+UART_DEF=9600,8,1,0,0");
  delay(2000);
  flushESP();

  wifiConnect();
}

void loop() {
  // Никакого фонового сбора данных — ответ не воруется
  if (millis() - lastCheckTime >= TELEMETRY_INTERVAL_MS) {
    lastCheckTime = millis();
    processWiFiCycle();
  }
}