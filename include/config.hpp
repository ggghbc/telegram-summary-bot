#pragma once

#include <string>
#include <cstdint>
#include <optional>

namespace summarybot {

/**
 * @brief Application configuration container.
 * Loads settings from environment variables, .env file, or config.json.
 */
struct Config {
    std::string telegram_bot_token;
    std::string telegram_api_base_url = "https://api.telegram.org";
    std::string proxy_url; // Optional HTTP/SOCKS proxy for curl

    std::string llm_api_key;
    std::string llm_api_url = "https://api.openai.com/v1/chat/completions";
    std::string llm_model = "gpt-4o-mini";
    double llm_temperature = 0.4;
    int llm_timeout_seconds = 120;

    int64_t max_messages_to_process = 1500;
    int64_t default_messages_to_process = 100;
    int64_t max_stored_messages_per_chat = 3000;

    bool admin_only_summaries = false;
    int rate_limit_seconds = 30; // Cooldown between summary requests per chat

    std::string db_path = "messages.db";
    std::string system_prompt;

    /**
     * @brief Load configuration prioritizing:
     *        1. Environment variables
     *        2. config.json (if present)
     *        3. .env file (if present)
     *        4. Default values
     */
    static Config load(const std::string& custom_config_path = "");

    /**
     * @brief Validate that necessary fields (like bot token and LLM key) are provided.
     * @return true if valid, false otherwise (with error description in out_error).
     */
    bool validate(std::string& out_error) const;

    /**
     * @brief Get the default system prompt for chat summarization.
     */
    static std::string get_default_system_prompt();
};

} // namespace summarybot
