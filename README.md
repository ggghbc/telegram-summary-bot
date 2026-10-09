# Telegram Conversation Summary Bot (C++20)

Высокопроизводительный Telegram-бот на C++20, который ведет локальную историю бесед и по требованию генерирует емкие, структурированные выжимки (самари) последних $N$ сообщений (до 1500 сообщений).

---

## ⚡ Особенности и архитектура

- **Язык разработки**: Modern C++20 с флагами оптимизации `-O3`.
- **База данных**: Встроенная **SQLite 3** в режиме **WAL (Write-Ahead Logging)** — запись каждого сообщения занимает менее `0.1 мс`, хранение сохраняется между перезапусками.
- **Асинхронная обработка**: Запросы к нейросети выполняются в отдельных потоках — бот не блокирует получение новых сообщений и параллельно показывает индикатор *"печатает..."* в чате.
- **Поддержка любых LLM**: Работает через универсальный OpenAI-совместимый протокол (OpenAI `gpt-4o-mini`, Google Gemini, Groq `llama-3.3-70b`, DeepSeek `deepseek-chat`, OpenRouter, а также локальный Ollama / vLLM).
- **Автоматический сплиттер сообщений**: Если итоговая сводка превышает лимит Telegram (4096 символов), бот аккуратно делит текст по смысловым абзацам и отправляет последовательными частями.
- **Защита от флуда и спама**:
  - Длинные «простыни» текста от отдельных пользователей безопасно усекаются в транскрипте, чтобы не переполнять контекст LLM.
  - Сообщения ботов автоматически игнорируются, исключая рекурсивные зацикливания.
  - Скользящее окно хранения истории (настраивается, по умолчанию 3000 сообщений на чат).
- **Поддержка прокси**: Встроенная поддержка HTTP и SOCKS5 прокси для обхода сетевых ограничений.

---

## 🚀 Как бот вызывается в группе

Бот реагирует на обращение к нему с указанием числа сообщений $N$ (максимум **1500**):

| Команда | Описание |
|---|---|
| `@bot_username 500` | Сгенерировать самари последних 500 сообщений беседы |
| `@bot_username 1500` | Самари последних 1500 сообщений (максимальный лимит) |
| `@bot_username` | Самари со значением по умолчанию (100 сообщений) |
| `/summary 300` | Альтернативная slash-команда |
| `/help` или `@bot_username help` | Вызов справочной информации |

> 💡 **Примечание о лимите:** Если пользователь указывает число больше 1500 (например, `@bot 3000`), бот автоматически ограничивает запрос 1500 сообщениями и вежливо уведомляет об этом.

---

## ⚙️ Важная настройка Telegram: Отключение Privacy Mode

По умолчанию Telegram-боты в группах получают **только** команды и прямые обращения. Чтобы бот мог сохранять контекст всей беседы:

1. Откройте диалог с [@BotFather](https://t.me/BotFather).
2. Отправьте команду `/setprivacy`.
3. Выберите вашего бота.
4. Нажмите кнопку **`Disable`**.
*(Альтернатива: просто назначьте бота администратором группы).*

---

## 🛠 Сборка и запуск

### 1. Требования

- Компилятор с поддержкой C++20 (`GCC 11+` или `Clang 14+`)
- `CMake 3.20+`
- `Ninja` или `Make`
- `libcurl` и `sqlite3`

**Установка зависимостей:**
```bash
# Arch Linux
sudo pacman -S base-devel cmake ninja curl sqlite

# Ubuntu / Debian
sudo apt update && sudo apt install -y build-essential cmake ninja-build libcurl4-openssl-dev libsqlite3-dev
```

### 2. Сборка проекта

```bash
git clone https://github.com/your-username/telegram-summary-bot.git
cd telegram-summary-bot

# Конфигурация и сборка в Release-режиме
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Запуск тестов
./build/test_all
```

### 3. Настройка конфигурации

Создайте файл `.env` в корневой папке проекта на основе `.env.example`:

```bash
cp .env.example .env
```

Отредактируйте параметры:

```ini
# Токен вашего бота от @BotFather
TELEGRAM_BOT_TOKEN=123456789:ABCdefGHIjklMNOpqrSTUvwxYZ

# Ключ к LLM API
LLM_API_KEY=your_api_key_here

# Адрес API и модель (по умолчанию OpenAI):
LLM_API_URL=https://api.openai.com/v1/chat/completions
LLM_MODEL=gpt-4o-mini

# Опционально: прокси (если требуется)
# PROXY_URL=socks5h://127.0.0.1:10808
```

#### Примеры настроек различных LLM-провайдеров:

- **Google Gemini (OpenAI Endpoint)**:
  ```ini
  LLM_API_URL=https://generativelanguage.googleapis.com/v1beta/openai/chat/completions
  LLM_MODEL=gemini-2.5-flash
  LLM_API_KEY=AIzaSy...
  ```
- **Groq (Молниеносная генерация)**:
  ```ini
  LLM_API_URL=https://api.groq.com/openai/v1/chat/completions
  LLM_MODEL=llama-3.3-70b-versatile
  LLM_API_KEY=gsk_...
  ```
- **DeepSeek**:
  ```ini
  LLM_API_URL=https://api.deepseek.com/chat/completions
  LLM_MODEL=deepseek-chat
  LLM_API_KEY=sk-...
  ```
- **Локальный Ollama**:
  ```ini
  LLM_API_URL=http://localhost:11434/v1/chat/completions
  LLM_MODEL=llama3.1
  LLM_API_KEY=ollama
  ```

### 4. Запуск

```bash
./build/telegram-summary-bot
```

---

## 🐳 Запуск через Docker / Docker Compose

```bash
# 1. Создайте .env
cp .env.example .env
nano .env

# 2. Запустите контейнер в фоне
docker compose up -d --build

# Просмотр логов
docker compose logs -f
```

---

## 📂 Структура проекта

```
telegram-summary-bot/
├── include/
│   ├── bot_app.hpp            # Управление циклом событий и фоновыми задачами
│   ├── config.hpp             # Парсинг настроек из .env, config.json и env vars
│   ├── db.hpp                 # SQLite3 слой с пулом подготовленных запросов и WAL
│   ├── http_client.hpp        # Клиент на libcurl с поддержкой прокси и SSL
│   ├── llm_client.hpp         # Универсальный клиент к LLM API
│   ├── summary_generator.hpp  # Сборка стека диалога и генерация промпта
│   └── telegram_bot.hpp       # Telegram Bot API (long polling, сплиттер сообщений)
├── src/
│   ├── bot_app.cpp
│   ├── config.cpp
│   ├── db.cpp
│   ├── http_client.cpp
│   ├── llm_client.cpp
│   ├── summary_generator.cpp
│   ├── telegram_bot.cpp
│   └── main.cpp
├── tests/
│   └── test_all.cpp           # Набор модульных тестов
├── external/
│   └── nlohmann/json.hpp      # Библиотека JSON для C++
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── .env.example
├── config.example.json
└── README.md
```

---

## 📜 Лицензия

MIT License.
