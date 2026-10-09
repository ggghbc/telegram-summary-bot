#include "config.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

namespace summarybot {

namespace {

// Helper to trim whitespace and surrounding quotes
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    std::string s = str.substr(first, (last - first + 1));
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

// Read simple KEY=VALUE pairs from a file (.env)
void parse_env_file(const std::string& filepath, std::map<std::string, std::string>& kv_map) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = trim(line.substr(0, eq_pos));
            std::string val = trim(line.substr(eq_pos + 1));
            if (!key.empty()) {
                kv_map[key] = val;
            }
        }
    }
}

std::optional<std::string> get_env_or_map(const std::string& key, const std::map<std::string, std::string>& file_map) {
    const char* val = std::getenv(key.c_str());
    if (val != nullptr && std::string(val).length() > 0) {
        return std::string(val);
    }
    auto it = file_map.find(key);
    if (it != file_map.end() && !it->second.empty()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace

std::string Config::get_default_system_prompt() {
    return "You are an expert conversation analyst and summarizer for Telegram group chats.\n"
           "Your task is to analyze the provided chat history of the last messages and generate a clear, structured, and informative summary in Russian.\n\n"
           "Format the response cleanly using standard Telegram Markdown:\n\n"
           "📌 *Краткая суть*:\n"
           "(1-2 предложения: общая тема беседы, контекст и настроение)\n\n"
           "💬 *Ключевые темы и обсуждения*:\n"
           "- Перечислите главные темы списком.\n"
           "- Кратко опишите, о чем шла речь, кто какую позицию занимал или что предлагал (указывайте участников по именам/никам).\n\n"
           "💡 *Решения, договоренности и полезное*:\n"
           "- К чему пришли, о чем договорились, важные выводы или полезные ссылки.\n\n"
           "❓ *Открытые вопросы* (если есть):\n"
           "- Вопросы без ответа или оставшиеся задачи.\n\n"
           "Правила:\n"
           "1. Будьте лаконичны, но не упускайте ключевую суть («просто о чем говорили и что к чему»).\n"
           "2. Отфильтруйте бессмысленный флуд, односложные смайлики и спам.\n"
           "3. Отвечайте строго на русском языке.";
}

Config Config::load(const std::string& custom_config_path) {
    Config cfg;
    cfg.system_prompt = get_default_system_prompt();

    std::map<std::string, std::string> env_file_map;

    // Check potential .env locations (current dir, parent dir, or custom path)
    const std::vector<std::string> env_candidates = {
        custom_config_path,
        ".env",
        "../.env",
        "../../.env"
    };

    for (const auto& candidate : env_candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate) && !std::filesystem::is_directory(candidate)) {
            parse_env_file(candidate, env_file_map);
            std::cout << "[Config] Loaded environment from: " << candidate << std::endl;
            break;
        }
    }

    // Check potential config.json locations
    std::string json_path;
    const std::vector<std::string> json_candidates = {
        custom_config_path,
        "config.json",
        "../config.json"
    };
    for (const auto& candidate : json_candidates) {
        if (!candidate.empty() && candidate.ends_with(".json") && std::filesystem::exists(candidate)) {
            json_path = candidate;
            break;
        }
    }
    if (std::filesystem::exists(json_path)) {
        try {
            std::ifstream f(json_path);
            if (f.is_open()) {
                nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
                if (!j.is_discarded()) {
                    if (j.contains("telegram_bot_token") && j["telegram_bot_token"].is_string()) {
                        cfg.telegram_bot_token = j["telegram_bot_token"].get<std::string>();
                    }
                    if (j.contains("telegram_api_base_url") && j["telegram_api_base_url"].is_string()) {
                        cfg.telegram_api_base_url = j["telegram_api_base_url"].get<std::string>();
                    }
                    if (j.contains("proxy_url") && j["proxy_url"].is_string()) {
                        cfg.proxy_url = j["proxy_url"].get<std::string>();
                    }
                    if (j.contains("llm_api_key") && j["llm_api_key"].is_string()) {
                        cfg.llm_api_key = j["llm_api_key"].get<std::string>();
                    }
                    if (j.contains("llm_api_url") && j["llm_api_url"].is_string()) {
                        cfg.llm_api_url = j["llm_api_url"].get<std::string>();
                    }
                    if (j.contains("llm_model") && j["llm_model"].is_string()) {
                        cfg.llm_model = j["llm_model"].get<std::string>();
                    }
                    if (j.contains("llm_temperature") && j["llm_temperature"].is_number()) {
                        cfg.llm_temperature = j["llm_temperature"].get<double>();
                    }
                    if (j.contains("llm_timeout_seconds") && j["llm_timeout_seconds"].is_number()) {
                        cfg.llm_timeout_seconds = j["llm_timeout_seconds"].get<int>();
                    }
                    if (j.contains("max_messages_to_process") && j["max_messages_to_process"].is_number()) {
                        cfg.max_messages_to_process = j["max_messages_to_process"].get<int64_t>();
                    }
                    if (j.contains("default_messages_to_process") && j["default_messages_to_process"].is_number()) {
                        cfg.default_messages_to_process = j["default_messages_to_process"].get<int64_t>();
                    }
                    if (j.contains("max_stored_messages_per_chat") && j["max_stored_messages_per_chat"].is_number()) {
                        cfg.max_stored_messages_per_chat = j["max_stored_messages_per_chat"].get<int64_t>();
                    }
                    if (j.contains("db_path") && j["db_path"].is_string()) {
                        cfg.db_path = j["db_path"].get<std::string>();
                    }
                    if (j.contains("system_prompt") && j["system_prompt"].is_string()) {
                        cfg.system_prompt = j["system_prompt"].get<std::string>();
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Config] Warning: Failed to parse JSON config: " << e.what() << std::endl;
        }
    }

    // Override with environment variables / .env values (highest precedence)
    if (auto v = get_env_or_map("TELEGRAM_BOT_TOKEN", env_file_map)) cfg.telegram_bot_token = *v;
    if (auto v = get_env_or_map("TELEGRAM_API_BASE_URL", env_file_map)) cfg.telegram_api_base_url = *v;
    if (auto v = get_env_or_map("PROXY_URL", env_file_map)) cfg.proxy_url = *v;
    if (cfg.proxy_url.empty()) {
        if (auto v = get_env_or_map("HTTPS_PROXY", env_file_map)) cfg.proxy_url = *v;
        else if (auto v2 = get_env_or_map("ALL_PROXY", env_file_map)) cfg.proxy_url = *v2;
    }

    if (auto v = get_env_or_map("LLM_API_KEY", env_file_map)) cfg.llm_api_key = *v;
    if (cfg.llm_api_key.empty()) {
        if (auto v = get_env_or_map("OPENAI_API_KEY", env_file_map)) cfg.llm_api_key = *v;
        else if (auto v2 = get_env_or_map("GEMINI_API_KEY", env_file_map)) cfg.llm_api_key = *v2;
    }

    if (auto v = get_env_or_map("LLM_API_URL", env_file_map)) cfg.llm_api_url = *v;
    if (auto v = get_env_or_map("LLM_MODEL", env_file_map)) cfg.llm_model = *v;

    if (auto v = get_env_or_map("MAX_MESSAGES_TO_PROCESS", env_file_map)) {
        try { cfg.max_messages_to_process = std::stoll(*v); } catch (...) {}
    }
    if (auto v = get_env_or_map("DEFAULT_MESSAGES_TO_PROCESS", env_file_map)) {
        try { cfg.default_messages_to_process = std::stoll(*v); } catch (...) {}
    }
    if (auto v = get_env_or_map("MAX_STORED_MESSAGES_PER_CHAT", env_file_map)) {
        try { cfg.max_stored_messages_per_chat = std::stoll(*v); } catch (...) {}
    }
    if (auto v = get_env_or_map("DB_PATH", env_file_map)) cfg.db_path = *v;
    if (auto v = get_env_or_map("SYSTEM_PROMPT", env_file_map)) cfg.system_prompt = *v;

    return cfg;
}

bool Config::validate(std::string& out_error) const {
    if (telegram_bot_token.empty()) {
        out_error = "TELEGRAM_BOT_TOKEN is not configured. Please provide it in .env, config.json, or environment variable.";
        return false;
    }
    if (llm_api_key.empty()) {
        out_error = "LLM_API_KEY is not configured. Please provide it in .env, config.json, or environment variable.";
        return false;
    }
    if (max_messages_to_process <= 0) {
        out_error = "max_messages_to_process must be greater than 0.";
        return false;
    }
    return true;
}

} // namespace summarybot
