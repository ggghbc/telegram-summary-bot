# Telegram Conversation Summary Bot (C++20)

A high-performance Telegram bot that maintains a persistent local chat history and generates concise, structured, and lively conversation summaries on demand for up to 1,500 messages.

---

## Highlights & Architecture

- **Performance**: Built with modern C++20.
- **Multimodal Vision & Image Analysis**: Analyzes photos, screenshots, diagrams, and error traces using multimodal LLMs (Gemini / OpenAI Vision). Automatically enriches chat history with concise image descriptions so summaries understand visual context without hallucination.
- **Strict Factual Grounding**: Follows reply chains with quoted snippets, attributes statements with authentic participant quotes (`"..."`), and prohibits psychologizing or dramatic extrapolations.
- **SQLite 3 with WAL Mode**: Embedded SQLite 3 database operating in Write-Ahead Logging (`WAL`) mode. Message insertion takes `< 0.1 ms`.
- **Universal LLM Compatibility**: Connects to any OpenAI-compatible API, including Google Gemini (`gemini-3.1-flash-lite`, `gemini-3-flash-preview`), OpenAI (`gpt-4o-mini`), Groq (`llama-3.3-70b-versatile`), DeepSeek (`deepseek-chat`), OpenRouter, and local Ollama or vLLM instances.
- **Language Matching**: The bot automatically detects the predominant language spoken in the conversation (Russian, English, Spanish, etc.) and generates the summary in that same language.
- **Smart Message Splitting**: Intelligently breaks long summaries exceeding Telegram's 4,096-character limit into clean paragraph-aligned chunks.
- **Proxy Support**: Native HTTP and SOCKS5 proxy support via `libcurl`.

## Group Chat Usage

The bot responds to mentions and commands with counts, time intervals, topic filters, or images (hard limit: **1,500** messages):

| Command                             | Description                                                                            |
| ----------------------------------- | -------------------------------------------------------------------------------------- |
| `@bot_username 500`                 | Summarize the last 500 messages                                                        |
| `@bot_username 24h`                 | Summarize all messages from the last 24 hours (also supports `12h`, `2h`, `30m`, `1d`) |
| `@bot_username today`               | Summarize all messages since midnight today in chat timezone                           |
| `@bot_username 300 about release`   | Summarize messages focusing on a specific topic                                        |
| `@bot_username 24h about database`  | Time-window summary with topic focus                                                   |
| `@bot_username` (reply to photo)    | Analyze and explain the image in detail                                                |
| `[Photo] + @bot_username <prompt>`  | Inspect attached photo and answer user's question                                       |
| `/photo` or `/image` (on photo)     | Explicit command to analyze an image                                                   |
| `/timezone +3` or `/timezone MSK`   | Set or inspect custom timezone offset for the chat                                     |
| `@bot_username`                     | Summarize with default count (100 messages)                                            |
| `/summary 200`                      | Slash command alternative                                                              |
| `/help` or `@bot_username help`     | Display help and usage instructions                                                    |

> **Forum Topics / Threads:** When invoked inside a Telegram Topic / Thread, the bot automatically isolates and summarizes messages exclusively within that topic.

---

## Important Telegram Setup: Privacy Mode

By default, Telegram bots in groups only receive commands starting with `/` or direct mentions. To allow the bot to record chat messages for subsequent summaries:

1. Open [@BotFather](https://t.me/BotFather) in Telegram.
2. Send the `/setprivacy` command.
3. Select your bot from the list.
4. Click **`Disable`**.
5. @BotFather will confirm: `Privacy mode is disabled for [your_bot]`.

_(Alternatively, promote the bot to Group Administrator with message-reading privileges)._

---

## Building and Running

### 1. Prerequisites

- C++20 compatible compiler (`GCC 11+` or `Clang 14+`)
- `CMake 3.20+`
- `Ninja` or `Make`
- `libcurl` and `sqlite3` development packages

**Install dependencies:**

```bash
# Arch Linux
sudo pacman -S base-devel cmake ninja curl sqlite

# Ubuntu / Debian
sudo apt update && sudo apt install -y build-essential cmake ninja-build libcurl4-openssl-dev libsqlite3-dev
```

### 2. Build the Project

```bash
git clone https://github.com/your-username/telegram-summary-bot.git
cd telegram-summary-bot

# Configure and compile in Release mode
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run unit test suite
./build/test_all
```

### 3. Configuration

Create a `.env` file from the provided template:

```bash
cp .env.example .env
vim .env # 
```

Configure your credentials:

```ini
# Bot token from @BotFather
TELEGRAM_BOT_TOKEN=123456789:ABCdefGHIjklMNOpqrSTUvwxYZ

# LLM API Key
LLM_API_KEY=your_llm_api_key_here

# LLM API Endpoint and Model
LLM_API_URL=https://generativelanguage.googleapis.com/v1beta/openai/chat/completions
LLM_MODEL=gemini-3.1-flash-lite

# Optional proxy (HTTP or SOCKS5)
# PROXY_URL=socks5h://127.0.0.1:10808
```

#### Provider Examples

- **Google Gemini (Recommended)**:

    ```ini
    LLM_API_URL=https://generativelanguage.googleapis.com/v1beta/openai/chat/completions
    LLM_MODEL=gemini-3.1-flash-lite
    LLM_API_KEY=your_gemini_key
    ```

- **Groq (Ultra-fast)**:

    ```ini
    LLM_API_URL=https://api.groq.com/openai/v1/chat/completions
    LLM_MODEL=llama-3.3-70b-versatile
    LLM_API_KEY=your_groq_key
    ```

- **OpenAI**:

    ```ini
    LLM_API_URL=https://api.openai.com/v1/chat/completions
    LLM_MODEL=gpt-4o-mini
    LLM_API_KEY=your_openai_key
    ```

- **DeepSeek**:

    ```ini
    LLM_API_URL=https://api.deepseek.com/chat/completions
    LLM_MODEL=deepseek-chat
    LLM_API_KEY=your_deepseek_key
    ```

- **Local Ollama**:
    ```ini
    LLM_API_URL=http://localhost:11434/v1/chat/completions
    LLM_MODEL=llama3.1
    LLM_API_KEY=ollama
    ```

### 4. Running the Bot

Run directly from either the project root or the `build` directory:

```bash
cd /home/jdn/dev/telegram-summary-bot/build
./telegram-summary-bot
```

#### Running 24/7 as a systemd Service

```bash
sudo tee /etc/systemd/system/telegram-summary-bot.service > /dev/null <<EOF
[Unit]
Description=Telegram Conversation Summary Bot
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=/home/jdn/dev/telegram-summary-bot
ExecStart=/home/jdn/dev/telegram-summary-bot/build/telegram-summary-bot
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now telegram-summary-bot
```

### 5. Running via Docker / Docker Compose

```bash
# Start container in detached mode
docker compose up -d --build

# View live logs
docker compose logs -f
```

---

## License

[MIT License](./LICENSE). Free to use, modify, and distribute.
