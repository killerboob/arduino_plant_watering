#include <SoftwareSerial.h>
#include <string.h>

SoftwareSerial mySerial(4, 5);
#define WIFI_SERIAL mySerial

const char WIFI_SSID[] = "AS47";
const char WIFI_PASS[] = "240490398002";
const char SERVER_IP[] = "213.171.25.91";

const int TELEMETRY_INTERVAL_MS = 10000;

bool wifiConnected = false;
unsigned long lastCheckTime = 0;

char txBuffer[100];
char rxBuffer[300];
int rxIndex = 0;

// ------------------------------------------------------------------
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

void wifiConnect() {
  Serial.println(F("WiFi connecting..."));
  WIFI_SERIAL.println(F("AT+CWMODE=1"));
  waitForResponse("OK", 2000);
  WIFI_SERIAL.println(F("AT+CIPMUX=0"));
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

int readSoilMoisture()  { return 75; }
int readLeak()          { return 0; }
int readWaterReservoir() { return 0; }
void setPump(bool state)  { Serial.println(state ? F("PUMP ON") : F("PUMP OFF")); }
void setLight(bool state) { Serial.println(state ? F("LIGHT ON") : F("LIGHT OFF")); }

// ------------------------------------------------------------------
void sendPostData(int soil_moisture, int leak, int water_reservoir) {
  if (!wifiConnected) return;
  Serial.println(F(">>> POST start"));
  flushESP();
  WIFI_SERIAL.println(F("AT+CIPCLOSE"));
  waitForResponse("OK", 2000);

  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  WIFI_SERIAL.println(txBuffer);
  if (!waitForResponse("OK", 10000) && !waitForResponse("ALREADY CONNECTED", 1000)) {
    Serial.println(F("[POST] TCP error"));
    return;
  }
  Serial.println(F("[POST] TCP ok"));

  // Длина JSON (вычисляем один раз, txBuffer временно хранит строку)
  int jsonLen = snprintf(txBuffer, sizeof(txBuffer),
    "{\"soil moisture\":%d,\"leak\":%d,\"water reservoir\":%d,\"human_name\":\"second\"}",
    soil_moisture, leak, water_reservoir);
  
  // Точная длина всего HTTP-запроса
  const char httpHeader[] = "POST /smart-watering/2/post_endpoint HTTP/1.0\r\nHost: ";
  const char ctHeader[]   = "\r\nContent-Type: application/json\r\nContent-Length: ";
  int hostLen = strlen(SERVER_IP);
  int numDigits = (jsonLen >= 100) ? 3 : (jsonLen >= 10) ? 2 : 1;
  int fullLen = strlen(httpHeader) + hostLen + strlen(ctHeader) + numDigits + 4 + jsonLen;
  // +4: \r\n\r\n после значения Content-Length

  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSEND=%d", fullLen);
  WIFI_SERIAL.println(txBuffer);
  if (!waitForChar('>', 5000)) {
    Serial.println(F("[POST] No >"));
    return;
  }

  // Отправляем HTTP по частям
  WIFI_SERIAL.print(httpHeader);
  WIFI_SERIAL.print(SERVER_IP);
  WIFI_SERIAL.print(ctHeader);
  WIFI_SERIAL.print(jsonLen);
  WIFI_SERIAL.print("\r\n\r\n");
  // JSON тело
  WIFI_SERIAL.print("{\"soil moisture\":");
  WIFI_SERIAL.print(soil_moisture);
  WIFI_SERIAL.print(",\"leak\":");
  WIFI_SERIAL.print(leak);
  WIFI_SERIAL.print(",\"water reservoir\":");
  WIFI_SERIAL.print(water_reservoir);
  WIFI_SERIAL.print(",\"human_name\":\"second\"}");

  Serial.println(F("[POST] data sent"));

  if (waitForResponse("SEND OK", 5000)) {
    Serial.println(F("[POST] SEND OK"));
  } else {
    Serial.println(F("[POST] no SEND OK"));
  }

  WIFI_SERIAL.println(F("AT+CIPCLOSE"));
  waitForResponse("OK", 2000);
  flushESP();
  Serial.println(F("<<< POST end"));
}

// ------------------------------------------------------------------
void getServerCommand() {
  if (!wifiConnected) return;
  Serial.println(F(">>> GET start"));
  flushESP();
  WIFI_SERIAL.println(F("AT+CIPCLOSE"));
  waitForResponse("OK", 2000);

  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSTART=\"TCP\",\"%s\",80", SERVER_IP);
  WIFI_SERIAL.println(txBuffer);
  if (!waitForResponse("OK", 10000) && !waitForResponse("ALREADY CONNECTED", 1000)) {
    Serial.println(F("[GET] TCP error"));
    return;
  }
  Serial.println(F("[GET] TCP ok"));

  const char getReq[] = "GET /smart-watering/2/get_endpoint HTTP/1.1\r\n"
                         "Host: 213.171.25.91\r\n"
                         "Connection: close\r\n\r\n";
  int len = strlen(getReq);
  snprintf(txBuffer, sizeof(txBuffer), "AT+CIPSEND=%d", len);
  WIFI_SERIAL.println(txBuffer);
  if (!waitForChar('>', 5000)) {
    Serial.println(F("[GET] No >"));
    return;
  }

  WIFI_SERIAL.print(getReq);
  Serial.println(F("[GET] request sent, reading response..."));

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
      if (strstr(rxBuffer, "CLOSED")) {
        start = 0;
        break;
      }
    }
    if (start == 0) break;
  }

  Serial.print(F("[GET] buffer: "));
  Serial.println(rxBuffer);

  char* startJson = strchr(rxBuffer, '{');
  char* endJson   = strrchr(rxBuffer, '}');
  if (startJson && endJson && endJson > startJson) {
    char saved = *(endJson + 1);
    *(endJson + 1) = '\0';

    Serial.print(F("[GET] JSON: "));
    Serial.println(startJson);

    if (strstr(startJson, "\"light\":")) {
      if (strstr(startJson, "\"light\":\"true\"")) {
        Serial.println(F("[GET] light=true -> ON"));
        setLight(true);
      } else if (strstr(startJson, "\"light\":\"false\"")) {
        Serial.println(F("[GET] light=false -> OFF"));
        setLight(false);
      } else {
        Serial.println(F("[GET] light value unknown, ignoring"));
      }
    } else {
      Serial.println(F("[GET] no light command"));
    }

    if (strstr(startJson, "\"pump\":")) {
      if (strstr(startJson, "\"pump\":\"true\"")) {
        Serial.println(F("[GET] pump=true -> ON"));
        setPump(true);
      } else if (strstr(startJson, "\"pump\":\"false\"")) {
        Serial.println(F("[GET] pump=false -> OFF"));
        setPump(false);
      } else {
        Serial.println(F("[GET] pump value unknown, ignoring"));
      }
    } else {
      Serial.println(F("[GET] no pump command"));
    }

    *(endJson + 1) = saved;
  } else {
    Serial.println(F("[GET] no JSON found"));
  }

  WIFI_SERIAL.println(F("AT+CIPCLOSE"));
  waitForResponse("OK", 2000);
  flushESP();
  Serial.println(F("<<< GET end"));
}

// ------------------------------------------------------------------
void processWiFiCycle() {
  int soil = readSoilMoisture();
  int leak = readLeak();
  int water = readWaterReservoir();

  Serial.print(F("Sensors: soil=")); Serial.print(soil);
  Serial.print(F(", leak=")); Serial.print(leak);
  Serial.print(F(", water=")); Serial.println(water);

  sendPostData(soil, leak, water);
  delay(200);
  getServerCommand();
}

// ------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println(F("=== Arduino WiFi Module ==="));

  WIFI_SERIAL.begin(9600);
  WIFI_SERIAL.println(F("AT+UART_DEF=9600,8,1,0,0"));
  delay(2000);
  flushESP();

  wifiConnect();
}

void loop() {
  if (millis() - lastCheckTime >= TELEMETRY_INTERVAL_MS) {
    lastCheckTime = millis();
    processWiFiCycle();
  }
}