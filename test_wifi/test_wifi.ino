// ============================================================
// Arduino UNO + Troyka Wi-Fi (пины 0,1) + LED "Пиранья" (пин 6)
// GET запрос /api/health
// Вся индикация – на светодиод
// ============================================================

const char* SSID     = "AS47";
const char* PASSWORD = "240490398002";
const char* HOST     = "213.171.25.91";
const int   PORT     = 80;

const int LED_PIN = 6;

void ledBlink(int on, int off, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(on);
    digitalWrite(LED_PIN, LOW);
    delay(off);
  }
}

void ledSet(bool state) {
  digitalWrite(LED_PIN, state ? HIGH : LOW);
}

// Отправить AT-команду, не ждать ответа
void sendCmd(const char* cmd) {
  Serial.println(cmd);
  delay(200);
}

// Ожидать строку в ответе от модуля (таймаут мс)
bool waitFor(const char* target, int timeout) {
  unsigned long start = millis();
  String buf = "";
  while (millis() - start < timeout) {
    while (Serial.available()) {
      char c = Serial.read();
      buf += c;
      if (buf.indexOf(target) != -1) return true;
      if (buf.length() > 100) buf = "";
    }
  }
  return false;
}

// Очистка буфера
void flush() {
  while (Serial.available()) Serial.read();
}

// Бесконечный цикл ошибки
void errorLoop() {
  while (1) {
    ledBlink(200, 200, 5);
    delay(2000);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  ledSet(false);
  
  // Старт: 3 короткие вспышки
  ledBlink(200, 200, 3);
  delay(500);
  
  // Инициализация Serial (пины 0-1) для связи с WiFi модулем
  Serial.begin(115200);
  delay(2000);
  
  // 1. Проверка AT
  flush();
  sendCmd("AT");
  if (!waitFor("OK", 2000)) errorLoop();
  
  // 2. Режим станции
  sendCmd("AT+CWMODE_DEF=1");
  if (!waitFor("OK", 1000)) errorLoop();
  
  // 3. Подключение к WiFi (медленное мигание)
  Serial.print("AT+CWJAP_DEF=\"");
  Serial.print(SSID);
  Serial.print("\",\"");
  Serial.print(PASSWORD);
  Serial.println("\"");
  
  bool wifiOk = false;
  unsigned long start = millis();
  while (millis() - start < 15000) {
    ledBlink(1000, 1000, 1);   // одно медленное мигание
    if (waitFor("WIFI GOT IP", 500)) {
      wifiOk = true;
      break;
    }
  }
  if (!wifiOk) errorLoop();
  
  // WiFi готов: 2 короткие вспышки + постоянное горение
  ledBlink(300, 300, 2);
  delay(1000);
  ledSet(true);
  
  // 4. Отправляем GET запрос
  flush();
  
  // TCP соединение
  Serial.print("AT+CIPSTART=0,\"TCP\",\"");
  Serial.print(HOST);
  Serial.print("\",");
  Serial.println(PORT);
  if (!waitFor("CONNECT", 5000)) errorLoop();
  
  // Формируем GET запрос
  String request = "GET /api/health HTTP/1.1\r\n";
  request += "Host: ";
  request += HOST;
  request += "\r\n";
  request += "Connection: close\r\n";
  request += "\r\n";
  
  // Отправляем команду CIPSEND
  Serial.print("AT+CIPSEND=0,");
  Serial.println(request.length());
  if (!waitFor(">", 3000)) errorLoop();
  
  // Шлём сам запрос
  Serial.print(request);
  
  // Ждём успешной отправки и хоть какого-то ответа сервера
  if (waitFor("SEND OK", 5000) && waitFor("+IPD", 3000)) {
    // Успех: длинная вспышка 2 секунды
    ledBlink(2000, 0, 1);
  } else {
    errorLoop();
  }
  
  // Закрываем соединение
  Serial.println("AT+CIPCLOSE=0");
  delay(500);
  ledSet(false);
  
  // Всё, больше ничего не делаем
}

void loop() {
  // Ничего не делаем повторно
  delay(86400000);
}