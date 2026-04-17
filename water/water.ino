/*
 * ============================================================================
 *  АВТОПОЛИВ РАСТЕНИЙ С ЗАЩИТОЙ ОТ ПРОТЕЧКИ И УПРАВЛЕНИЕМ ФИТОЛАМПОЙ
 * ============================================================================
 *
 *  Особенности:
 *   - Измерение влажности почвы раз в 30 секунд, вывод в процентах.
 *   - Автоматический полив: если почва сухая (<300) и вода есть – помпа включается.
 *   - Максимальное время полива – 5 секунд.
 *   - Аварийная остановка при отсутствии воды или срабатывании датчика протечки.
 *   - Красный светодиод: горит постоянно при отсутствии воды,
 *     мигает при протечке.
 *   - Кнопка управляет фитолампой (реле), антидребезг.
 *   - Все события выводятся в Serial-монитор.
 *
 *  Автор: [Ваше имя]
 *  Дата:  28.03.2026
 */

// ============================================================================
//  КОНФИГУРАЦИЯ – МЕНЯЙТЕ ЗНАЧЕНИЯ ПОД СВОИ ДАТЧИКИ
// ============================================================================

// ---- Пины Arduino ----------------------------------------------------------
const int PIN_BUTTON         = 6;   // кнопка (замыкает на GND)
const int PIN_PUMP           = 11;  // реле помпы
const int PIN_WATER_SENSOR   = 7;   // датчик уровня воды (цифровой)
const int PIN_LAMP_RELAY     = 4;   // реле фитолампы
const int PIN_RED_LED        = 10;  // красный светодиод
const int PIN_LEAK_SENSOR    = 2;   // датчик протечки (цифровой)
const int PIN_SOIL_MOISTURE  = A5;  // датчик влажности почвы (аналоговый)

// ---- Пороги и логика датчиков ----------------------------------------------
const int SOIL_DRY_THRESHOLD   = 300;   // ниже – сухая почва (включаем полив)
const int SOIL_WET_THRESHOLD   = 300;   // выше/равно – влажная (останавливаем)
const int WATER_PRESENT_VALUE  = LOW;   // LOW = вода есть, HIGH = воды нет
const int LEAK_DETECT_VALUE    = LOW;   // LOW = протечка, HIGH = норма

// ---- Временные интервалы (миллисекунды) ------------------------------------
const unsigned long MEASURE_INTERVAL    = 30000;   // 30 сек – измерение влажности
const unsigned long MAX_PUMP_DURATION   = 5000;    // 5 сек – макс. время полива
const unsigned long DEBOUNCE_DELAY      = 50;      // 50 мс – антидребезг кнопки
const unsigned long LED_BLINK_INTERVAL  = 500;     // 500 мс – период мигания при протечке

// ============================================================================
//  ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ – НЕ МЕНЯТЬ БЕЗ ПОНИМАНИЯ
// ============================================================================

enum SystemState { STATE_IDLE, STATE_WATERING };
SystemState currentState = STATE_IDLE;

unsigned long lastMeasureTime = 0;
unsigned long wateringStartTime = 0;
int lastSoilRaw = 0;                     // последнее сырое значение влажности (0..1023)

// Для кнопки (антидребезг)
int lastButtonStable = HIGH;
int lastButtonRaw = HIGH;
unsigned long lastDebounceTime = 0;

bool lampState = false;                  // состояние фитолампы
bool leakDetected = false;
int lastLeakState = HIGH;

// Для мигания светодиода при протечке
unsigned long lastLedBlinkTime = 0;
bool ledBlinkState = false;

// ============================================================================
//  НАСТРОЙКА
// ============================================================================

void setup() {
  // Конфигурация пинов
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_WATER_SENSOR, INPUT);
  pinMode(PIN_LAMP_RELAY, OUTPUT);
  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_LEAK_SENSOR, INPUT_PULLUP);

  // Начальное состояние выходов
  digitalWrite(PIN_PUMP, LOW);
  digitalWrite(PIN_LAMP_RELAY, LOW);
  digitalWrite(PIN_RED_LED, LOW);

  // Инициализация Serial
  Serial.begin(9600);
  Serial.println("=== Система автополива запущена ===");

  // Первое измерение влажности будет выполнено сразу в loop()
  lastMeasureTime = millis() - MEASURE_INTERVAL;

  // Проверка датчика протечки при старте
  int leakRaw = digitalRead(PIN_LEAK_SENSOR);
  leakDetected = (leakRaw == LEAK_DETECT_VALUE);
  lastLeakState = leakRaw;
  if (leakDetected) {
    Serial.println("ВНИМАНИЕ: Обнаружена протечка воды! Полив заблокирован.");
  }
}

// ============================================================================
//  ОСНОВНОЙ ЦИКЛ
// ============================================================================

void loop() {
  // 1. Проверка протечки (высший приоритет)
  checkLeak();

  // 2. Обработка кнопки (фитолампа)
  handleButton();

  // 3. Управление красным светодиодом (мигает при протечке, иначе индикатор воды)
  updateRedLed();

  // 4. Автоматический полив (только если нет протечки)
  if (!leakDetected) {
    autoWatering();
  }

  // 5. Если полив активен – мониторим условия остановки
  if (currentState == STATE_WATERING) {
    checkWateringStop();
  }
}

// ============================================================================
//  ФУНКЦИИ ДЛЯ ДАТЧИКОВ И ИСПОЛНИТЕЛЬНЫХ УСТРОЙСТВ
// ============================================================================

/**
 * Проверка датчика протечки.
 * При обнаружении протечки блокируется полив, при устранении – разблокируется.
 */
void checkLeak() {
  int leakRaw = digitalRead(PIN_LEAK_SENSOR);
  if (leakRaw != lastLeakState) {
    lastLeakState = leakRaw;
    if (leakRaw == LEAK_DETECT_VALUE) {
      leakDetected = true;
      Serial.println("ВНИМАНИЕ: Обнаружена протечка воды! Полив заблокирован.");
      if (currentState == STATE_WATERING) {
        stopWatering("Аварийная остановка из-за протечки.");
      }
    } else {
      leakDetected = false;
      Serial.println("Протечка устранена. Полив разблокирован.");
    }
  }
}

/**
 * Обработка кнопки с антидребезгом.
 * Нажатие переключает фитолампу.
 */
void handleButton() {
  int raw = digitalRead(PIN_BUTTON);
  if (raw != lastButtonRaw) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (raw != lastButtonStable) {
      lastButtonStable = raw;
      if (lastButtonStable == LOW) {
        toggleLamp();
      }
    }
  }
  lastButtonRaw = raw;
}

/**
 * Переключение фитолампы.
 */
void toggleLamp() {
  lampState = !lampState;
  digitalWrite(PIN_LAMP_RELAY, lampState ? HIGH : LOW);
  Serial.print("Фитолампа ");
  Serial.println(lampState ? "включена" : "выключена");
}

/**
 * Управление красным светодиодом:
 * - если протечка – мигает;
 * - иначе горит постоянно при отсутствии воды, выключен при наличии воды.
 */
void updateRedLed() {
  bool waterPresent = (digitalRead(PIN_WATER_SENSOR) == WATER_PRESENT_VALUE);

  if (leakDetected) {
    // Мигание при протечке
    if (millis() - lastLedBlinkTime >= LED_BLINK_INTERVAL) {
      lastLedBlinkTime = millis();
      ledBlinkState = !ledBlinkState;
      digitalWrite(PIN_RED_LED, ledBlinkState ? HIGH : LOW);
    }
  } else {
    // Обычный режим: индикатор отсутствия воды
    digitalWrite(PIN_RED_LED, waterPresent ? LOW : HIGH);
    // Сброс мигающих переменных
    ledBlinkState = false;
    lastLedBlinkTime = 0;
  }
}

/**
 * Автоматический полив: измерение влажности раз в 30 секунд
 * и запуск полива, если почва сухая и вода есть.
 */
void autoWatering() {
  if (millis() - lastMeasureTime >= MEASURE_INTERVAL) {
    lastMeasureTime = millis();
    measureAndControl();
  }
}

/**
 * Измерение влажности и принятие решения о поливе.
 */
void measureAndControl() {
  bool waterPresent = (digitalRead(PIN_WATER_SENSOR) == WATER_PRESENT_VALUE);
  lastSoilRaw = readSoilMoisture();   // обновляем глобальное значение

  // Вывод влажности в процентах
  int percent = map(lastSoilRaw, 0, 1023, 0, 100);
  Serial.print("Влажность почвы: ");
  Serial.print(percent);
  Serial.print("% (");
  Serial.print(lastSoilRaw);
  Serial.println(")");
  Serial.print("Наличие воды: ");
  Serial.println(waterPresent ? "есть" : "нет");

  if (currentState == STATE_IDLE) {
    if (waterPresent && lastSoilRaw < SOIL_DRY_THRESHOLD) {
      startWatering();
    } else {
      if (!waterPresent) {
        Serial.println("Нет воды, полив невозможен.");
      } else if (lastSoilRaw >= SOIL_DRY_THRESHOLD) {
        Serial.println("Почва достаточно влажная, полив не требуется.");
      }
    }
  }
  // Случай, если полив идёт дольше 30 сек (теоретически маловероятно, но для надёжности)
  else if (currentState == STATE_WATERING) {
    if (!waterPresent || lastSoilRaw >= SOIL_WET_THRESHOLD) {
      stopWatering(!waterPresent ?
        "Вода закончилась (проверка по таймеру)" :
        "Влажность достигла порога (проверка по таймеру)");
    }
  }
}

/**
 * Включение помпы.
 */
void startWatering() {
  if (leakDetected) {
    Serial.println("Невозможно включить помпу из-за протечки!");
    return;
  }
  currentState = STATE_WATERING;
  wateringStartTime = millis();
  digitalWrite(PIN_PUMP, HIGH);
  Serial.println("Полив начат (макс. 5 сек).");
}

/**
 * Остановка помпы с указанием причины.
 */
void stopWatering(const char* reason) {
  digitalWrite(PIN_PUMP, LOW);
  currentState = STATE_IDLE;
  Serial.println(reason);
}

/**
 * Проверка условий остановки полива во время его работы.
 * Вызывается из основного цикла.
 */
void checkWateringStop() {
  bool waterPresent = (digitalRead(PIN_WATER_SENSOR) == WATER_PRESENT_VALUE);

  // Нет воды
  if (!waterPresent) {
    stopWatering("Вода закончилась! Помпа отключена.");
    return;
  }
  // Превышение времени
  if (millis() - wateringStartTime >= MAX_PUMP_DURATION) {
    stopWatering("Достигнуто максимальное время полива (5 сек).");
    return;
  }
  // Достигнута влажность (по последнему измерению)
  if (lastSoilRaw >= SOIL_WET_THRESHOLD) {
    stopWatering("Почва стала влажной, полив остановлен.");
  }
}

/**
 * Чтение аналогового датчика влажности с усреднением.
 * Возвращает сырое значение 0..1023.
 */
int readSoilMoisture() {
  const int samples = 5;
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(PIN_SOIL_MOISTURE);
    delay(1);             // короткая задержка для стабильности
  }
  return sum / samples;
}