#include <iostream>
#include <csignal>
#include <memory>
#include "config.hpp"
#include "bot_app.hpp"
#include "http_client.hpp"

namespace {
std::atomic<summarybot::BotApp*> g_app{nullptr};

void signal_handler(int signum) {
    std::cout << "\n[Main] Caught signal " << signum << ", shutting down gracefully..." << std::endl;
    auto* app = g_app.load();
    if (app) {
        app->stop();
    }
}
} // namespace

int main(int argc, char* argv[]) {
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Telegram Conversation Summary Bot (C++20)\n\n"
                      << "Usage:\n"
                      << "  telegram-summary-bot [options]\n\n"
                      << "Options:\n"
                      << "  -c, --config <file>   Path to config.json or .env file\n"
                      << "  -h, --help            Show this help message\n\n"
                      << "Environment Variables:\n"
                      << "  TELEGRAM_BOT_TOKEN    Telegram bot token from @BotFather\n"
                      << "  LLM_API_KEY           API key for LLM provider (OpenAI, Gemini, Groq, etc.)\n"
                      << "  LLM_API_URL           Endpoint URL (default: https://api.openai.com/v1/chat/completions)\n"
                      << "  LLM_MODEL             Model name (default: gpt-4o-mini)\n"
                      << "  PROXY_URL             Optional proxy (e.g., socks5h://127.0.0.1:10808)\n"
                      << "  DB_PATH               SQLite database path (default: messages.db)\n";
            return 0;
        }
    }

    // Initialize libcurl globally
    summarybot::HttpClient::global_init();

    // Load configuration
    summarybot::Config config = summarybot::Config::load(config_path);

    std::string config_error;
    if (!config.validate(config_error)) {
        std::cerr << "========================================================\n"
                  << "Configuration Error:\n"
                  << "  " << config_error << "\n\n"
                  << "Please create a .env file or config.json with your keys.\n"
                  << "See .env.example or config.example.json for templates.\n"
                  << "========================================================" << std::endl;
        summarybot::HttpClient::global_cleanup();
        return 1;
    }

    // Register signal handlers for clean exit
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize and run bot
    summarybot::BotApp app(config);
    g_app.store(&app);

    if (!app.init()) {
        std::cerr << "[Main] Failed to start bot. Check your tokens and network connection." << std::endl;
        summarybot::HttpClient::global_cleanup();
        return 1;
    }

    app.run();
    g_app.store(nullptr);

    summarybot::HttpClient::global_cleanup();
    std::cout << "[Main] Bot stopped. Goodbye!" << std::endl;
    return 0;
}
