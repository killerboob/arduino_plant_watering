# 🌱 Plant Watering Monitoring System — Technical Wiki

Система мониторинга растений с Telegram-ботом, веб-интерфейсом и API для IoT-устройств.

---

## 1. 🌐 АРХИТЕКТУРА И ПОТОКИ ДАННЫХ

### 1.1 Описание проекта

Система предназначена для:
- 📊 Мониторинга данных с IoT-устройств (датчики влажности, температуры и т.д.)
- 🔔 Уведомления пользователей о состоянии устройств через Telegram
- 🖥️ Управления конфигурацией устройств и сборок через веб-интерфейс
- 🤖 Взаимодействия с пользователями через Telegram-бота

### 1.2 Схема взаимодействия компонентов

```mermaid
graph TB
    subgraph "Frontend"
        WEB[Веб-интерфейс<br/>React + TailwindCSS]
    end
    
    subgraph "Backend"
        API[FastAPI Server<br/>Port 8000]
        DB[(MariaDB Database)]
    end
    
    subgraph "Telegram Bot"
        BOT[Python Telegram Bot]
        TOKEN_MONITOR[Token Monitor]
    end
    
    subgraph "External"
        DEVICES[IoT Devices]
        TELEGRAM[Telegram API]
    end
    
    WEB -->|REST API /api/*| API
    DEVICES -->|HTTP POST/GET endpoint| API
    API <-->|SQLAlchemy| DB
    BOT <-->|Polling| TELEGRAM
    BOT <-->|SQLAlchemy| DB
    TOKEN_MONITOR -.->|Check every 30s| DB
```

**Текстовый flow:**
```
IoT Device → HTTP Request → Nginx → Backend API → Database
                              ↓
                        Bot Manager ← Database (token)
                              ↓
                        Telegram API → User
                              
Web Browser → Nginx → Frontend Static → Backend API → Database
```

### 1.3 Протоколы передачи данных

| Компонент | Протокол | Описание |
|-----------|----------|----------|
| Web ↔ Backend | REST API (HTTP/JSON) | Все запросы через `/api/*` |
| IoT Devices ↔ Backend | HTTP POST/GET | Endpoint: `/{machine_name}/{device_id}/{post\|get}_endpoint` |
| IoT Devices ↔ Backend (Commands) | HTTP GET | Устройство опрашивает `/{machine_name}/{device_id}/get_endpoint`, получает команды в формате `{"commands": {"cmd1": "val1", ...}}` |
| Bot ↔ Database | SQLAlchemy (Direct SQL) | Чтение токена, настроек пользователей, запись команд в device_commands |
| Bot ↔ Telegram | python-telegram-bot (Polling) | Long-polling для получения обновлений |
| Nginx ↔ Backend | HTTP Proxy | Reverse proxy для API и статики |

### 1.4 Стек технологий

| Компонент | Технология | Версия |
|-----------|------------|--------|
| **Backend** | FastAPI | 0.115.0 |
| | Python | 3.10 |
| | SQLAlchemy | 2.0.30 |
| | PyJWT | 2.8.0 |
| | PyMySQL | 1.1.0 |
| **Bot** | python-telegram-bot | 22.0 |
| | SQLAlchemy | 2.0.30 |
| | PyMySQL | 1.1.0 |
| **Frontend** | React | 18.2.0 (CDN) |
| | Axios | 1.5.0 (CDN) |
| | TailwindCSS | Latest (CDN) |
| | Babel Standalone | 7.22.5 (CDN) |
| **Database** | MariaDB | 10.6 |
| **Web Server** | Nginx | Latest |

### 1.5 Жизненный цикл типового запроса

#### Запрос от IoT устройства (POST — отправка данных):
```
1. Device → POST /{machine_name}/{device_id}/post_endpoint
2. Nginx → Проксирует на backend:8000
3. Backend → Валидирует JSON
4. Backend → get_or_create_device() — проверяет/создает устройство в БД
5. Backend → Сохраняет данные в device_data
6. Backend → Возвращает {status: "success", ...}
```

#### Запрос от IoT устройства (GET — получение команд):
```
1. Device → GET /{machine_name}/{device_id}/get_endpoint
2. Nginx → Проксирует на backend:8000
3. Backend → get_or_create_device() — проверяет/создает устройство
4. Backend → Запрашивает невыполненные команды из device_commands
5. Backend → Формирует плоский JSON {commands: {cmd1: val1, cmd2: val2}}
6. Backend → Помечает команды как выполненные (is_executed = True)
7. Backend → Возвращает {status: "success", commands: {...}}
```

#### Действие пользователя в боте (отправка команды устройству):
```
1. User → Нажимает кнопку команды в Telegram (task_cmd_exec_...)
2. Bot → handle_task_command_execution() парсит callback_data
3. Bot → Извлекает device_id, command, value
4. Bot → INSERT INTO device_commands (device_id, command, value, is_executed=0)
5. Bot → Отправляет подтверждение пользователю
6. [Device → GET /{machine_name}/{device_id}/get_endpoint → Получает команду]
```

#### Действие пользователя в боте (общий flow):
```
1. User → Нажимает кнопку в Telegram
2. Telegram API → Отправляет callback_query боту
3. Bot → handle_device_callback() обрабатывает callback_data
4. Bot → DeviceService → SQL запрос к БД
5. Bot → Формирует ответное сообщение
6. Bot → Отправляет ответ через Telegram API
```

#### Запрос от веб-интерфейса:
```
1. User → Кликает кнопку в UI
2. Frontend → axios.post('/api/auth/login', {...})
3. Nginx → Проксирует на backend:8000/api/auth/login
4. Backend → Проверяет credentials в БД
5. Backend → Генерирует JWT токен
6. Backend → Возвращает {token: "..."}
7. Frontend → Сохраняет токен в localStorage
```

---

## 2. 🤖 TELEGRAM-БОТ

### 2.1 Роль в системе

Бот выступает как интерфейс пользователя для:
- Просмотра списка подключенных устройств
- Добавления/удаления устройств из персонального списка
- Управления настройками уведомлений
- Получения статусов онлайн/оффлайн устройств

**Интеграции:**
- **База данных**: Прямое подключение через SQLAlchemy (чтение/запись)
- **Сервер**: [TODO: уточнить из кода — прямых вызовов API сервера не обнаружено]
- **Сайт**: Косвенная связь через общую БД (настройки токена, пользовательские данные)

### 2.2 Команды бота

| Команда | Scope | Описание | Права/Фильтры |
|---------|-------|----------|---------------|
| `/start` | Global | Запуск бота, показ приветствия с клавиатурой | Все пользователи |
| `/help` | Global | Показать список доступных команд | Все пользователи |
| `/status` | Global | Проверка статуса работы бота | Все пользователи |
| `/devices` | Global | Список устройств пользователя | Все пользователи |
| `/add_device` | Global | Начало процесса добавления устройства | Все пользователи |
| `/remove_device` | Global | Список устройств для удаления | Все пользователи |
| `/start_notifications` | Global | Включение уведомлений | Все пользователи |
| `/stop_notifications` | Global | Выключение уведомлений | Все пользователи |
| `/test_notification` | Global | Отправка тестового уведомления | Все пользователи |

### 2.3 Обработчики (Handlers)

| Файл/Модуль | Тип | Входные данные | Бизнес-логика | Выход/Ответ | Вызовы сервера/БД |
|-------------|-----|----------------|---------------|-------------|-------------------|
| `handlers/commands.py` | | | | | |
| `help_command` | message | Update, Context | Формирование текста справки | Текст с командами | Нет |
| `status_command` | message | Update, Context | Проверка статуса | "✅ Бот работает" | Нет |
| `handlers/menu_handlers.py` | | | | | |
| `start_command` | message | Update, Context | Приветствие + ReplyKeyboard | Текст + главное меню (📊 Данные, 📝 Задачи, ⚙️ Настройки) | Нет |
| `handle_main_menu` | message (filter) | Text="⚙️ Настройки" | Показ inline меню настроек | InlineKeyboard: Уведомления, Устройства | Нет |
| `handle_data_section` | message (filter) | Text="📊 Данные" | Показ заглушки раздела данных | Текст: "Раздел данных: здесь будет агрегироваться история показаний датчиков, статусы устройств и аналитика. Функционал в разработке." + главное меню | Нет |
| `handle_tasks_section` | message (filter) | Text="📝 Задачи" | Показ заглушки раздела задач | Текст: "Раздел задач: здесь появится управление расписанием, автоматические сценарии и журнал действий. Функционал в разработке." + главное меню | Нет |
| `handle_menu_callback` | callback_query | pattern: `^menu_`, `^enable_`, `^disable_`, `^devices_` | Навигация по меню настроек | Edit message с новым контентом | NotificationService (БД) |
| `handlers/device_handlers.py` | | | | | |
| `devices_list_command` | command/callback | Update, Context | Получение списка устройств | Список устройств с кнопками | DeviceService → БД |
| `add_device_command` | command/callback | Update, Context | Установка состояния 'waiting_for_device_id' | Запрос ID устройства | Нет |
| `handle_device_id_input` | message (text filter) | Text (число) | Валидация и сохранение устройства | Подтверждение/ошибка | DeviceService → БД |
| `remove_device_command` | command/callback | Update, Context | Список устройств для удаления | Кнопки подтверждения | DeviceService → БД |
| `handle_device_callback` | callback_query | pattern: device_* | Обработка всех device callback'ов | Различные ответы | DeviceService → БД |
| `handlers/notification_handlers.py` | | | | | |
| `start_notifications_command` | command | Update, Context | Включение уведомлений | Подтверждение | NotificationService → БД |
| `stop_notifications_command` | command | Update, Context | Выключение уведомлений | Подтверждение | NotificationService → БД |
| `test_notification_command` | command | Update, Context | Отправка тестового сообщения | Тестовое уведомление | NotificationService → БД |
| `handlers/data_handlers.py` | | | | | |
| `handle_data_list` | callback_query | pattern: `^data_list_p\d+` | Пагинация списка устройств пользователя | InlineKeyboard: устройства, ◀️, ▶️ | Database → user_devices, devices |
| `handle_device_select` | callback_query | pattern: `^data_dev_\d+_\d+` | Загрузка датчиков устройства | Заголовок + список датчиков (пагинация) | Database → builds.post_fields, device_data |
| `handle_field_select` | callback_query | pattern: `^data_field_\d+_\d+_.+` | Получение последних 20 показаний датчика | Текст: 🕒 timestamp \| 📏 value (список) + кнопки Excel/Анализ | Database → device_data |
| `handle_fields_pagination` | callback_query | pattern: `^data_fields_\d+_\d+_p\d+` | Пагинация списка датчиков | InlineKeyboard: датчики, ◀️, ▶️, 🔙 | Database → builds.post_fields, device_data |
| `handle_data_excel` | callback_query | pattern: `^data_excel_\d+_\d+_.+` | Генерация и отправка Excel-файла | Файл .xlsx + сообщение с кнопками навигации | Database → device_data |
| `handle_data_analyze` | callback_query | pattern: `^data_analyze_\d+_\d+_.+` | Генерация графика анализа | send_photo() с графиком + edit_message_text() исходного | Database → device_data, utils.data_charts.generate_analysis_chart() |
| `handlers/task_handlers.py` | | | | | |
| `handle_tasks_section` | message (filter) | Text="📝 Задачи" | Показ списка устройств пользователя для управления задачами | InlineKeyboard: устройства, пагинация | Database → user_devices |
| `handle_tasks_pagination` | callback_query | pattern: `^task_(list|prev|next)_p\d+$` | Пагинация списка устройств | InlineKeyboard: устройства, ◀️, ▶️ | Database → user_devices |
| `handle_task_device_select` | callback_query | pattern: `^task_dev_\d+_\d+$` | Выбор устройства, загрузка GET-команд из builds.get_fields | Заголовок + список команд (пагинация) | Database → builds.get_fields, user_devices |
| `handle_commands_pagination` | callback_query | pattern: `^task_cmd_\d+_\d+_p\d+$` | Пагинация списка GET-команд | InlineKeyboard: команды, ◀️, ▶️, 🔙 | Database → builds.get_fields, user_devices |
| `handle_task_command_select` | callback_query | pattern: `^task_cmd_val_\d+_\d+_.+$` | Выбор команды, показ параметров из bot_parameters | InlineKeyboard: параметры команды (кнопки действий), 🔙 | Database → builds.get_fields, user_devices |
| `handle_task_command_execution` | callback_query | pattern: `^task_cmd_exec_\d+_\d+_.+_.+$` | Выполнение команды — запись в БД (device_commands) | Подтверждение отправки команды | Database → INSERT INTO device_commands, user_devices |

### 2.4 Кнопки

#### Главное меню (ReplyKeyboard)

| Текст | Тип | Callback_data | Действие при нажатии | Изменяет состояние FSM? |
|-------|-----|---------------|----------------------|-------------------------|
| `📊 Данные` | Reply | N/A (text filter) | Показ заглушки раздела данных | Нет |
| `📝 Задачи` | Reply | N/A (text filter) | Показ заглушки раздела задач | Нет |
| `⚙️ Настройки` | Reply | N/A (text filter) | Показ меню настроек | Нет |

**Раскладка главного меню:**
```python
[["📊 Данные", "📝 Задачи"],
 ["⚙️ Настройки"]]
```

#### Inline-кнопки (меню настроек, устройства и навигация по данным)

| Текст | Тип | Callback_data | Действие при нажатии | Изменяет состояние FSM? |
|-------|-----|---------------|----------------------|-------------------------|
| `🔔 Уведомления` | Inline | `menu_notifications` | Переход в меню уведомлений | Нет |
| `📱 Мои устройства` | Inline | `menu_devices` | Переход в меню устройств | Нет |
| `✅ Включить уведомления` | Inline | `enable_notifications` | Включение уведомлений в БД | Нет |
| `❌ Выключить уведомления` | Inline | `disable_notifications` | Выключение уведомлений в БД | Нет |
| `🔙 Назад` | Inline | `menu_back_settings` | Возврат в главное меню настроек | Нет |
| `📋 Список устройств` | Inline | `devices_list` | Показ списка устройств | Нет |
| `➕ Добавить устройство` | Inline | `add_device` | Запрос ID устройства | Да (state: waiting_for_device_id) |
| `🗑️ Удалить устройство` | Inline | `remove_device` | Показ списка для удаления | Нет |
| `📱 {device_name}` | Inline | `device_info_{id}` | Показ информации об устройстве | Нет |
| `🗑️ {device_name}` | Inline | `device_confirm_remove_{id}` | Подтверждение удаления | Нет |
| `❌ Отмена` | Inline | `cancel_add_device`, `cancel_remove` | Отмена операции, очистка FSM | Да (сброс state) |
| `📊 {device_human_name}` | Inline | `data_dev_{device_id}_{build_id}` | Переход к выбору датчиков устройства | Нет |
| `◀️ {field_name}` | Inline | `data_fields_{device_id}_{build_id}_p{page}` | Пагинация списка датчиков (страница влево) | Нет |
| `▶️ {field_name}` | Inline | `data_fields_{device_id}_{build_id}_p{page}` | Пагинация списка датчиков (страница вправо) | Нет |
| `📏 {field_name}` | Inline | `data_field_{device_id}_{build_id}_{field_name}` | Просмотр показаний выбранного датчика | Нет |
| `📥 Скачать Excel` | Inline | `data_excel_{device_id}_{build_id}_{field_name}` | Скачивание Excel-файла с данными датчика | Нет |
| `📈 Получить анализ` | Inline | `data_analyze_{device_id}_{build_id}_{field_name}` | Генерация и отправка графика анализа | Нет |
| `🔙 Назад к датчикам` | Inline | `data_fields_{device_id}_{build_id}_p1` | Возврат к списку датчиков | Нет |
| `🔙 Назад к устройствам` | Inline | `data_list_p1` | Возврат к списку устройств | Нет |
| `◀️` | Inline | `data_list_p{page}` | Пагинация списка устройств (страница влево) | Нет |
| `▶️` | Inline | `data_list_p{page}` | Пагинация списка устройств (страница вправо) | Нет |

### 2.4.1 Навигация по разделу "Данные" (Data Flow)

**Этап 1: Список устройств**
```
User → /start или "📊 Данные" → Список устройств (пагинация)
Callback: data_list_p{page}
Кнопки: 📊 {device_human_name}, ◀️, ▶️
```

**Этап 2: Выбор датчиков устройства**
```
User → Клик по устройству → Список датчиков (пагинация)
Callback: data_dev_{device_id}_{build_id}
Источник данных: builds.post_fields (JSON) → fallback → device_data.field_name
Кнопки: 📏 {field_name}, ◀️, ▶️, 🔙 Назад к устройствам
```

**Этап 3: Показания датчика**
```
User → Клик по датчику → Последние 20 записей
Callback: data_field_{device_id}_{build_id}_{field_name}
Формат: 🕒 {DD.MM.YYYY, HH:MM:SS} | 📏 {field_value}
Кнопки: [["📥 Скачать Excel", "📈 Получить анализ"], ["🔙 Назад к датчикам", "🔙 Назад к устройствам"]]
```

**Этап 4: Скачивание Excel**
```
User → Клик по "📥 Скачать Excel" → Файл Excel с данными
Callback: data_excel_{device_id}_{build_id}_{field_name}
Формат файла: .xlsx с колонками [created_at, field_value]
Кнопки: 🔙 Назад к датчикам, 🔙 Назад к устройствам (в новом сообщении)
```

**Этап 5: Аналитика с графиками**
```
User → Клик по "📈 Получить анализ" → График + статистика
Callback: data_analyze_{device_id}_{build_id}_{field_name}
Генерация: bot/utils/data_charts.py → matplotlib (линейный график в BytesIO)
Период агрегации: Авто-определение (day/week/month/quarter/year) по диапазону данных
Агрегация SQL: GROUP BY DATE/YEARWEEK/DATE_FORMAT/QUARTER/YEAR + AVG(field_value)
Отправка: 
  1. send_photo() — новое сообщение с изображением графика
  2. send_message() — новое сообщение с кнопками навигации
  3. edit_message_text() — редактирование исходного сообщения (добавление "✅ Анализ сформирован")
Обработка малых данных: Если после агрегации < 2 точек → строится детальный график по всем записям без группировки
Недостаточно данных: Если всего < 2 записей → изображение с текстом "📭 Недостаточно данных для построения графика"
Визуализация: Линейный график с осями "Дата" / "Значение {field_name}", заголовок "{human_name} — {period}", сетка, шрифт Roboto
Логирование: [CHARTS] на каждом шаге (запрос SQL, агрегация, генерация, отправка)
```

### 2.5 Машина состояний (FSM)

**Реализация**: Custom in-memory хранилище (`user_states` dict в `device_handlers.py`)

| Состояние | Описание | Переход в | Переход из | Таймаут | Сброс |
|-----------|----------|-----------|------------|---------|-------|
| `None` (default) | Пользователь не в диалоге | `waiting_for_device_id` | - | N/A | N/A |
| `waiting_for_device_id` | Ожидание ввода ID устройства | `None` | `add_device_command` | [TODO: не реализован] | `cancel_add_device`, успешное добавление, ошибка валидации |

**Хранилище**: 
```python
user_states = {}  # user_id -> {'state': 'waiting_for_device_id'}
```

**Проблемы**:
- ❌ Отсутствует таймаут состояний
- ❌ Состояния не сохраняются между перезапусками бота
- ❌ Нет поддержки множественных одновременных состояний

### 2.6 Middleware, фильтры, обработка ошибок

**Middleware**: [TODO: не реализовано]

**Фильтры**:
- `filters.Text(["⚙️ Настройки", "📊 Данные", "📝 Задачи"])` — фильтр по тексту для Reply-кнопок главного меню
- `filters.TEXT & ~filters.COMMAND` — ловит все текстовые сообщения кроме команд
- `pattern="^menu_"`, `pattern="^device_"`, `pattern="^data_"`, `pattern="^task_"` — regex фильтры для callback_query

**Rate-limit**: [TODO: не реализовано]

**Обработка ошибок**:
```python
# В bot_manager.py
self.application.add_error_handler(self._error_handler)

async def _error_handler(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
    logger.error(f"💥 Bot error: {context.error}")
    if update:
        logger.error(f"📱 Update that caused error: {update}")
```

**Логирование**:
- Уровень: DEBUG (настраивается через `LOG_LEVEL`)
- Формат: `%(asctime)s | %(levelname)-8s | %(name)-20s | %(message)s`
- Вывод: stdout

### 2.7 Инициация действий на сервере

**Текущая реализация**: Бот работает напрямую с БД, без вызовов backend API.

**Поток данных**:
```
User Action → Bot Handler → Service Layer → SQLAlchemy → Database
                                                      ↓
                                              Backend читает те же данные
```

**Для обратной связи**: [TODO: не реализовано push-уведомлений от сервера к боту]

---

## 3. 🖥️ BACKEND-СЕРВЕР

### 3.1 Роль и стек

**Роль**: REST API сервер для:
- Аутентификации пользователей (JWT)
- CRUD операций для сборок (builds)
- Приёма данных от IoT устройств
- Предоставления данных устройствам и веб-клиенту

**Стек**:
- FastAPI 0.115.0
- Uvicorn 0.30.6 (ASGI server)
- SQLAlchemy 2.0.30 (ORM)
- PyMySQL 1.1.0 (MySQL/MariaDB driver)
- PyJWT 2.8.0 (токены)

**Тип**: Monolithic API server

### 3.2 Эндпоинты

| Метод | Путь | Описание | Параметры | Ответ | Авторизация | Вызывается кем |
|-------|------|----------|-----------|-------|-------------|----------------|
| `POST` | `/{machine_name}/{device_id}/post_endpoint` | Приём данных от устройства | machine_name (path), device_id (path), JSON body | `{status, message, device_human_name, build, received_data}` | Нет | IoT Devices |
| `GET` | `/{machine_name}/{device_id}/get_endpoint` | Получение команд устройством | machine_name (path), device_id (path) | `{status, device_id, device_human_name, build, commands: {cmd: value}}` | Нет | IoT Devices |
| `POST` | `/api/auth/login` | Аутентификация | `{username, password}` | `{token}` | Нет | Frontend |
| `POST` | `/api/builds` | Создание сборки | `{machine_name, human_name, post_fields, get_fields}` | Build object | JWT | Frontend |
| `GET` | `/api/builds` | Список сборок | - | List[Build] | JWT | Frontend |
| `GET` | `/api/builds/{id}` | Получение сборки | id (path) | Build object | JWT | Frontend |
| `PUT` | `/api/builds/{id}` | Обновление сборки | id (path), Build data | Build object | JWT | Frontend |
| `DELETE` | `/api/builds/{id}` | Удаление сборки | id (path) | `{status: "deleted"}` | JWT | Frontend |
| `GET` | `/api/debug/builds` | Отладочная информация | - | Расширенные данные сборок | Нет | Dev |
| `GET` | `/api/devices` | Список устройств | - | List[Device] | JWT | Frontend |
| `DELETE` | `/api/devices/{device_id}` | Удаление устройства | device_id (path) | `{status, message}` | JWT | Frontend |
| `GET` | `/api/devices/{device_id}/data` | Данные устройства | device_id (path), limit (query, optional) | `{device_id, total_records, data}` | JWT | Frontend |
| `POST` | `/api/settings/bot-token` | Сохранение токена бота | `{telegram_bot_token}` | `{status: "saved"}` | JWT | Frontend |
| `GET` | `/api/health` | Health check | - | `{status: "ok"}` | Нет | Monitoring |

### 3.3 База данных

**СУБД**: MariaDB 10.6

**Модели**:

| Таблица | Модель | Описание |
|---------|--------|----------|
| `users` | User | Пользователи системы |
| `builds` | Build | Конфигурации сборок (группы устройств) |
| `settings` | Settings | Настройки (токен бота) |
| `devices` | Device | Устройства (привязаны к сборкам) |
| `device_data` | DeviceDataRecord | Временные ряды данных с устройств |
| `device_commands` | DeviceCommand | Очередь команд для устройств (бот → устройство) |
| `user_devices` | UserDevice (в боте) | Связь пользователь-устройство |
| `user_settings` | [TODO: модель не определена в backend] | Настройки пользователей (уведомления) |

**Схема**:
```sql
users:
  - id (PK)
  - username (unique, indexed)
  - password_hash

builds:
  - id (PK)
  - user_id (FK → users.id)
  - machine_name
  - human_name
  - post_fields (JSON)
  - get_fields (JSON)

settings:
  - id (PK)
  - user_id (FK → users.id)
  - telegram_bot_token

devices:
  - id (PK, part of composite key)
  - build_id (PK, part of composite key)
  - human_name
  - created_at
  - last_seen

device_data:
  - id (PK)
  - device_id (indexed)
  - build_id
  - field_name
  - field_value (Text)
  - created_at

device_commands:
  - id (PK)
  - device_id (indexed, FK → devices.id)
  - command (VARCHAR)
  - value (VARCHAR)
  - created_at
  - is_executed (BOOLEAN, default=False)

user_devices:
  - id (PK)
  - user_id (FK → users.id)
  - device_id
  - build_id
  - device_human_name
  - created_at

user_settings:
  - id (PK) [TODO: уточнить схему]
  - user_id
  - chat_id
  - notifications_enabled (BOOLEAN)
  - updated_at
```

**Миграции**: [TODO: не реализовано — используется `Base.metadata.create_all()`]

**Кэширование**: [TODO: не реализовано]

**Индексы**:
- `users.username` — unique index
- `users.id` — primary key index
- `builds.id` — primary key index
- `device_data.device_id` — index (для быстрого поиска)

### 3.4 Фоновые задачи

**Backend**: [TODO: не реализовано — нет очередей, Celery, RQ]

**Bot Token Monitor**:
- Расположение: `bot/core/token_monitor.py`
- Интервал проверки: 30 секунд (настраивается через `TOKEN_CHECK_INTERVAL`)
- Логика: polling БД на изменение `telegram_bot_token` в таблице `settings`
- При изменении: graceful restart бота с новым токеном

**Retry механизм**: [TODO: не реализовано]

**Мониторинг**: Health check endpoint `/api/health`

### 3.5 Интеграции

**С ботом**:
- Общая база данных
- Бот читает токен из `settings.telegram_bot_token`
- Бот читает/пишет пользовательские настройки
- [TODO: нет прямого API для communication]

**С сайтом**:
- REST API через Nginx proxy
- JWT аутентификация
- CORS: [TODO: не настроено явно]

### 3.6 Безопасность

**Аутентификация**:
- JWT tokens (HS256)
- Время жизни: 24 часа
- Secret: из env `JWT_SECRET`

**CORS**: [TODO: не настроено]

**Rate-limit**: [TODO: не реализовано]

**Healthchecks**: `GET /api/health` → `{status: "ok"}`

---

## 4. 💻 FRONTEND-САЙТ

### 4.1 Стек и сборка

**Стек**:
- React 18.2.0 (через CDN)
- Axios 1.5.0 (через CDN)
- TailwindCSS (через CDN)
- Babel Standalone 7.22.5 (для JSX in-browser)

**Сборка**: Pre-built static files в `/frontend/build/`

**Роутинг**: Client-side routing через `window.history.pushState` и обработчик `popstate`

**SSR/CSR**: Pure CSR (Client-Side Rendering)

### 4.2 Страницы/Компоненты

| Маршрут | Компонент | Назначение | Запрашиваемые API | Состояние/Валидация | Связь с ботом/сервером |
|---------|-----------|------------|-------------------|---------------------|------------------------|
| `/login` | Login | Форма входа | `POST /api/auth/login` | username, password | Сервер (auth) |
| `/` (home) | Home | Главная страница | - | onAddAssembly callback | Сервер (builds) |
| `/assemblies` | Assemblies | Список сборок | `GET /api/builds` | onEditBuild callback | Сервер (CRUD builds) |
| `/devices` | Devices | Список устройств | `GET /api/devices` | - | Сервер (devices) |
| `/device-data` | DeviceData | Данные устройства | `GET /api/devices/:id/data` | device_id, limit | Сервер (device data) |
| `/settings` | Settings | Настройки | `POST /api/settings/bot-token` | telegram_bot_token | Сервер (settings) |

**Поп-ап компоненты**:
- `CreateBuild` — создание новой сборки
- `EditBuild` — редактирование существующей сборки

### 4.3 Инициация действий

**Формы**:
- Login форма → `axios.post('/api/auth/login')`
- Настройки бота → `axios.post('/api/settings/bot-token')`
- Создание/редактирование сборки → [TODO: уточнить из кода компонентов]

**Real-time обновления**: [TODO: не реализовано — нет WebSocket/SSE/polling]

### 4.4 Управление состоянием

**Глобальное состояние**: `window.authState` объект
```javascript
window.authState = {
  isAuthenticated: false,
  token: null,
  listeners: []
};
```

**Auth Context**:
- `AuthProvider` — провайдер контекста
- `useAuth()` — хук для доступа к состоянию
- `login()`, `logout()` — глобальные функции

**Токен**: Хранится в `localStorage`

**Обработка ошибок UI**:
- Ошибки аутентификации → отображение сообщения в Login компоненте
- [TODO: toast/modal уведомления не реализованы]

### 4.5 Статика, SEO, PWA, деплой

**Статика**:
- Расположение: `/frontend/build/`
- Nginx раздает из `/usr/share/nginx/html`

**SEO**: 
- Meta tags: charset, viewport, title
- Favicon набор (ico, svg, png, manifest)

**PWA**:
- `site.webmanifest` присутствует
- Apple touch icon
- [TODO: service worker не реализован]

**Деплой-конфиг**:
- Docker volume: `./frontend/build:/usr/share/nginx/html:ro`
- Nginx config: `try_files $uri $uri/ /index.html` (SPA fallback)

---

## 5. 🛠 УСТАНОВКА И ЗАПУСК

### 5.1 Требования

| Компонент | Минимальная версия | Рекомендованная |
|-----------|-------------------|-----------------|
| OS | Linux (any) | Ubuntu 20.04+ |
| Docker | 20.10+ | Latest |
| Docker Compose | 2.0+ | Latest |
| RAM | 512 MB | 2 GB+ |
| Disk | 1 GB | 5 GB+ |

### 5.2 Клонирование и установка

```bash
# Клонирование репозитория
git clone <repository-url>
cd <project-directory>

# Запуск через Docker Compose
docker-compose up -d

# Проверка статуса
docker-compose ps

# Просмотр логов
docker-compose logs -f
```

### 5.3 Инициализация БД

Происходит автоматически при первом запуске:
- MariaDB создаёт базу `plant_watering`
- Backend выполняет `Base.metadata.create_all()` для создания таблиц
- [TODO: начальные данные не создаются автоматически]

### 5.4 Настройка .env

См. раздел 7.1 для полного списка переменных.

**Минимальная конфигурация**:
```bash
# Для backend
DATABASE_URL=mariadb+pymysql://user:password@db:3306/plant_watering
JWT_SECRET=your-secret-key-here

# Для bot
DATABASE_URL=mariadb+pymysql://user:password@db:3306/plant_watering
LOG_LEVEL=INFO

# Для db (в docker-compose.yml)
MYSQL_ROOT_PASSWORD=secure-root-password
MYSQL_DATABASE=plant_watering
MYSQL_USER=user
MYSQL_PASSWORD=password
```

### 5.5 Команды запуска

**Development (локально)**:

```bash
# Backend
cd backend
pip install -r requirements.txt
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000

# Bot
cd bot
pip install -r requirements.txt
python main.py

# Frontend
# Только статика — обслуживается Nginx или любым web-сервером
```

**Production (Docker Compose)**:

```bash
# Запуск всех сервисов
docker-compose up -d

# Пересборка после изменений
docker-compose up -d --build

# Остановка
docker-compose down

# Остановка с удалением volumes
docker-compose down -v
```

### 5.6 Тесты, линтеры, дебаг

**Тесты**: [TODO: не реализовано]

**Линтеры**: [TODO: не настроено]

**Дебаг**:
- Backend: uvicorn с `--reload` флагом
- Bot: `LOG_LEVEL=DEBUG` в env
- Nginx: error_log установлен в `debug` режим

**Горячая перезагрузка**:
- Backend: `--reload` флаг uvicorn
- Bot: [TODO: только через restart контейнера]
- Frontend: [TODO: только через rebuild]

---

## 6. 📁 СТРУКТУРА РЕПОЗИТОРИЯ

```
/workspace
├── backend/
│   ├── app/
│   │   └── main.py              # FastAPI приложение, все эндпоинты
│   ├── Dockerfile               # Docker образ для backend
│   └── requirements.txt         # Python зависимости
│
├── bot/
│   ├── core/
│   │   ├── bot_manager.py       # Главный менеджер бота
│   │   ├── database.py          # Database connection class
│   │   └── token_monitor.py     # Мониторинг изменений токена
│   ├── handlers/
│   │   ├── commands.py          # Обработчики команд (/start, /help, /status)
│   │   ├── menu_handlers.py     # Обработчики главного меню
│   │   ├── device_handlers.py   # Обработчики устройств
│   │   └── notification_handlers.py  # Обработчики уведомлений
│   ├── models/
│   │   └── user_device.py       # SQLAlchemy модель user_devices
│   ├── services/
│   │   ├── device_service.py    # Логика управления устройствами
│   │   ├── user_settings_service.py  # Настройки пользователей
│   │   └── notification_service.py   # Сервис уведомлений
│   ├── utils/
│   │   ├── config.py            # Загрузка конфигурации из env
│   │   └── logger.py            # Настройка логирования
│   ├── main.py                  # Точка входа бота
│   ├── Dockerfile               # Docker образ для бота
│   └── requirements.txt         # Python зависимости
│
├── frontend/
│   └── build/                   # Pre-built статические файлы
│       ├── index.html           # Главная HTML страница
│       ├── public/              # Favicon, manifest
│       └── src/
│           ├── App.jsx          # Корневой React компонент
│           ├── context/
│           │   └── AuthContext.jsx  # Auth контекст
│           └── components/      # React компоненты (Login, Home, etc.)
│
├── nginx/
│   └── nginx.conf               # Конфигурация Nginx
│
└── docker-compose.yml           # Оркестрация всех сервисов
```

### Назначение папок

| Директория | Назначение |
|------------|------------|
| `backend/app/` | Исходный код FastAPI сервера |
| `bot/core/` | Ядро бота (менеджер, БД, мониторинг) |
| `bot/handlers/` | Обработчики команд и callback'ов Telegram |
| `bot/services/` | Бизнес-логика (устройства, настройки, уведомления) |
| `bot/models/` | SQLAlchemy модели данных |
| `bot/utils/` | Утилиты (конфиг, логгер, генерация графиков data_charts.py) |
| `frontend/build/` | Готовые к деплою статические файлы |
| `nginx/` | Конфигурация reverse proxy |

---

## 7. 🔐 ПЕРЕМЕННЫЕ ОКРУЖЕНИЯ И ЗАВИСИМОСТИ

### 7.1 Переменные окружения (.env)

| Переменная | Компонент | Тип | Обязательна | Описание | Пример |
|------------|-----------|-----|-------------|----------|--------|
| `DATABASE_URL` | backend, bot | string | ✅ | URL подключения к БД (SQLAlchemy format) | `mariadb+pymysql://user:pass@db:3306/plant_watering` |
| `JWT_SECRET` | backend | string | ✅ | Секретный ключ для подписи JWT токенов | `your-super-secret-key` |
| `LOG_LEVEL` | bot | string | ❌ | Уровень логирования | `DEBUG`, `INFO`, `WARNING` |
| `TOKEN_CHECK_INTERVAL` | bot | integer | ❌ | Интервал проверки токена (секунды) | `30` |
| `DETAILED_LOGS` | bot | boolean | ❌ | Детальное логирование | `true`, `false` |
| `MYSQL_ROOT_PASSWORD` | db | string | ✅ | Пароль root пользователя MariaDB | `secure-password` |
| `MYSQL_DATABASE` | db | string | ✅ | Имя базы данных | `plant_watering` |
| `MYSQL_USER` | db | string | ✅ | Пользователь БД для приложения | `user` |
| `MYSQL_PASSWORD` | db | string | ✅ | Пароль пользователя БД | `password` |

### 7.2 Зависимости

#### Backend

| Пакет | Версия | Назначение | Компонент |
|-------|--------|------------|-----------|
| `fastapi` | 0.115.0 | Web framework | Backend |
| `uvicorn` | 0.30.6 | ASGI server | Backend |
| `sqlalchemy` | 2.0.30 | ORM | Backend, Bot |
| `pymysql` | 1.1.0 | MySQL driver | Backend, Bot |
| `pyjwt` | 2.8.0 | JWT tokens | Backend |

#### Bot

| Пакет | Версия | Назначение | Компонент |
|-------|--------|------------|-----------|
| `python-telegram-bot` | 22.0 | Telegram Bot API | Bot |
| `sqlalchemy` | 2.0.30 | ORM | Backend, Bot |
| `pymysql` | 1.1.0 | MySQL driver | Backend, Bot |
| `matplotlib` | 3.8.4 | Генерация графиков аналитики (data_charts.py) | Bot |
| `openpyxl` | 3.1.2 | Генерация Excel-файлов | Bot |

#### Frontend (CDN)

| Библиотека | Версия | Назначение | Источник |
|------------|--------|------------|----------|
| `react` | 18.2.0 | UI framework | jsdelivr.net |
| `react-dom` | 18.2.0 | DOM rendering | jsdelivr.net |
| `axios` | 1.5.0 | HTTP client | jsdelivr.net |
| `tailwindcss` | latest | CSS framework | tailwindcss.com |
| `@babel/standalone` | 7.22.5 | JSX compilation | unpkg.com |

---

## 📝 ПРИЛОЖЕНИЕ A: Известные ограничения и TODO

### Не реализованные функции

1. **FSM для бота**: Используется примитивное in-memory хранилище без персистентности
2. **Таймауты состояний**: Отсутствуют
3. **Rate limiting**: Не реализован ни в боте, ни в backend
4. **CORS**: Не настроен явно в FastAPI
5. **Миграции БД**: Автоматическое создание таблиц, но нет миграций
6. **Тесты**: Отсутствуют unit/integration тесты
7. **Real-time updates**: Нет WebSocket/SSE для фронтенда
8. **Push уведомления от сервера к боту**: Отсутствуют
9. **Кэширование**: Не реализовано

### Проблемы безопасности

1. **Пароли**: Хранятся как plain text в поле `password_hash` (без хэширования)
2. **JWT secret**: Хардкод в docker-compose.yml (нужно менять в production)
3. **Database credentials**: Хардкод в docker-compose.yml

### Рекомендации

1. Добавить Alembic для миграций БД
2. Реализовать proper FSM с Redis storage
3. Добавить rate limiting (slowapi для backend)
4. Настроить CORS в FastAPI
5. Добавить hashing для паролей (bcrypt/passlib)
6. Реализовать тесты (pytest)
7. Добавить WebSocket для real-time обновлений
8. Вынести секреты в external secrets manager

---

*Документ сгенерирован на основе анализа исходного кода репозитория. Последнее обновление: 16.04.2026, 01:22 (добавлена документация по разделу "Данные" — этапы 1-3)*
