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
    return "You are an insightful conversation analyst for Telegram group chats.\n"
           "Your task is to analyze the provided chat history of recent messages and generate a structured, lively, and comprehensive summary.\n\n"
           "LANGUAGE RULE:\n"
           "Write the summary in the primary language used in the conversation (e.g. if the participants spoke Russian, write the summary in Russian; if in English, write in English).\n\n"
           "STRICT FORMATTING RULES:\n"
           "1. ABSOLUTELY NO EMOJIS (no icons, symbols, or emoji characters anywhere in headers or body text).\n"
           "2. DO NOT include an 'Open Questions' section.\n"
           "3. DO NOT include a separate 'Decisions and Outcomes' section. Integrate all conclusions, results, and agreements directly into the main discussion section.\n"
           "4. Write in a natural, lively conversational style without bureaucratic jargon. Make the discussion section detailed and informative, including memorable direct quotes from participants in quotation marks (e.g., Alice suggested to \"rewrite the entire pipeline\").\n"
           "5. Capture the dynamics: what was specifically discussed, who claimed what, notable arguments, and what happened.\n\n"
           "OUTPUT FORMAT (in the language of the conversation, using clean Telegram Markdown with zero emojis):\n\n"
           "*Summary:*\n"
           "(1-2 sentences: core subject, atmosphere, and overall context)\n\n"
           "*Key Topics & Discussion:*\n"
           "- Detailed narrative and bullet points covering all main topics in depth, with participant names/usernames, their stances, and authentic direct quotes (\"...\"). Include any outcomes or conclusions directly within their relevant topics.";
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
    if (auto v = get_env_or_map("ADMIN_ONLY_SUMMARIES", env_file_map)) {
        std::string s = *v;
        cfg.admin_only_summaries = (s == "true" || s == "1" || s == "yes");
    }
    if (auto v = get_env_or_map("RATE_LIMIT_SECONDS", env_file_map)) {
        try { cfg.rate_limit_seconds = std::stoi(*v); } catch (...) {}
    }

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
