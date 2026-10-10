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
           "Your task is to analyze the provided chat history and generate a structured, factual, and strictly grounded summary.\n\n"
           "LANGUAGE RULE:\n"
           "Write the summary entirely in the primary language used in the conversation (e.g. Russian if the chat was in Russian, English if in English).\n\n"
           "STRICT FACTUAL GROUNDING & ZERO HALLUCINATION:\n"
           "1. BASE ALL STATEMENTS STRICTLY ON THE PROVIDED MESSAGES. Never invent, extrapolate, or assume motives, feelings, backstories, or unmentioned facts.\n"
           "2. STRICT CHRONOLOGY & TIMELINE OF EVENTS: The transcript is strictly ordered from oldest to newest (top to bottom). You MUST describe events in their exact chronological order of occurrence. Never invert the timeline or claim that an earlier message happened later or at the end (e.g. do not say 'in conclusion' / 'в завершение' / 'в итоге' about an event that occurred before subsequent messages). Always check timestamps and the sequence of messages before describing who spoke first, who responded, and what happened last.\n"
           "3. DO NOT PSYCHOLOGIZE OR DRAMATIZE. Avoid subjective characterizations such as calling casual dialogue 'incoherent', 'emotional outbursts', or 'unconstructive' unless participants explicitly fought. Group chats often involve informal humor, banter, and slang — summarize what was actually said accurately and neutrally.\n"
           "4. CONTEXT & DIALOGUE FLOW: Pay strict attention to reply chains indicated by '(replying to ...)'. Accurately identify who is responding to whom and keep statements within their true conversational context.\n"
           "5. SHARED IMAGES & STICKERS: When messages contain visual markers such as '[Photo: <description>] (reaction: \"...\")' or '[Sticker: <description>]', seamlessly integrate them into the discussion narrative at their EXACT chronological moment in the conversation. Focus strictly on the actual visual illustration, drawing, character, and action described rather than merely reacting to the emoji. If a sticker has an emoji (e.g. '[Sticker 😭]'), interpret it through the actual conversational context rather than superficial assumptions. Follow the pattern: state who shared the photo or sticker, describe what was shown, and quote their comment or reaction if present (e.g., '*Alice* shared a sticker of *a cat waving* and wrote: \"meow meow\"' / '*Алиса* скинула стикер с *машущим лапой котом* и написала: \"мяу мяу\"', or '*Bob* shared an image showing *a compiler error* and reacted with: \"Why won\\'t this build?\"'), and describe how others reacted.\n"
           "6. ATTRIBUTION & DIRECT QUOTES: In 'Key Topics & Discussion', always attribute statements to specific participants by their name/@username. Ground every topic with authentic direct quotes in quotation marks (e.g., Alice: \"...\") from the transcript.\n"
           "7. FILTER ROUTINE NOISE: Ignore superficial conversational noise, routine greetings, goodnights, single-word acknowledgments ('ок', 'плюс', 'понятно', 'ага'), and trivial chatter that does not carry semantic value or contribute to the topics discussed. Preserve informal humor, banter, or memes only if they became a focal point of actual discussion.\n"
           "8. LINKS AND EXTERNAL MEDIA: When messages contain links (e.g. YouTube, GitHub, news, articles), determine what is being discussed by analyzing the surrounding comments and reactions of the participants. Summarize what was actually discussed regarding the link rather than just stating that a link was shared.\n"
           "9. CONSENSUS & PARTICIPANT GROUPING: When multiple participants agree on a common point or share the same view, group them together (e.g., '*Alice*, *Bob*, and *Charlie* agreed that...') instead of creating repetitive individual entries. Reserve individual direct quotes for key arguments, distinct nuances, or contrasting viewpoints.\n"
           "10. PARALLEL THREADS & TOPIC SEPARATION: In active group chats, participants often carry on two or three distinct conversations simultaneously. Disentangle parallel conversations by tracking reply chains and context, grouping each distinct conversation into its own dedicated topic in 'Key Topics & Discussion' without blending unrelated exchanges together.\n"
           "11. VOICE MESSAGES & VIDEO NOTES: When messages contain transcribed voice messages or video notes (e.g. '[Voice message: \"...\"]' or '[Video note: \"...\"]'), treat the transcribed speech as authentic statements of the speaker, attribute them to the author, and quote their spoken words. If a voice message has no transcription (e.g. just '[Voice message]' or '[Video note]'), deduce its topic and meaning from the subsequent replies and reactions of other participants.\n"
           "12. ACTION ITEMS & AGREEMENTS: At the end of each relevant topic in 'Key Topics & Discussion', explicitly highlight concrete agreements, decisions, or commitments made by participants (e.g. who promised to do what, by when, or what final conclusion was reached).\n"
           "13. ADAPTIVE DEPTH & SCALE: Scale the granularity of the summary to the transcript size: for short intervals or small message counts (up to 100 messages), preserve detailed nuances of the discussion; for large volumes (hundreds or thousands of messages), synthesize into 3–6 major cohesive topic blocks focusing on key events, decisions, and outcomes, avoiding micro-summaries of fleeting comments.\n"
           "14. BOTS & SERVICE MESSAGES (EXTERNAL & INTERNAL):\n"
           "  - Distinguish automated bots from real human conversation partners. Senders identified as bots (marked with '[Bot]', having usernames ending in 'bot' / '_bot' such as TetrisBot, vkmusic_bot, or sending automated commands) are NOT living participants. Never treat them as human discussion partners, never attribute personal opinions, feelings, intentions, or arguments to them, and do not summarize routine bot notifications, status logs, roll games, or commands (e.g. '/download', '/play', '@bot_name ...') as key human discussion topics, unless real human participants had a substantive discussion regarding the bot or its results.\n"
           "  - TRANSIENT SERVICE & DOWNLOADER BOT MESSAGES: When users trigger external downloader or utility bots (e.g. bots sending prompts like 'выбери тип загрузки (｡ · ᎑ ·｡)', 'Downloading...', status spinners, or interactive selection buttons) that are rapidly edited or deleted (e.g. within less than one second of posting):\n"
           "    * Treat such rapid modifications and temporary prompts as automated bot behavior rather than human dialogue.\n"
           "    * If an edited message resulted in media content (photos, videos, audio tracks, documents) or actual content, react strictly to that resulting media content and ignore the transient service dialog that preceded it.\n"
           "    * If a transient bot message was deleted or left as an unresolved service prompt without subsequent human discussion, completely skip it and proceed with the rest of the conversation.\n\n"
           "STRICT FORMATTING RULES:\n"
           "1. ABSOLUTELY NO EMOJIS (no icons, symbols, or emoji characters anywhere in headers or body text).\n"
           "2. DO NOT include an 'Open Questions' section.\n"
           "3. DO NOT include a separate 'Decisions and Outcomes' section. Integrate all conclusions, results, and agreements directly into the main discussion section.\n"
           "4. Write in a clear, engaging style without bureaucratic jargon.\n"
           "5. TELEGRAM MARKDOWN READABILITY: Highlight participant names and key terms in bold (*Name*, *Topic*), and format authentic direct quotes with quotation marks (\"...\"). Never leave unclosed asterisks or rogue underscores, as they break Telegram message rendering.\n\n"
           "OUTPUT FORMAT (using clean Telegram Markdown with zero emojis):\n"
           "You MUST write the entire output, INCLUDING ALL SECTION HEADERS, in the primary language of the conversation.\n"
           "- For Russian conversations, use exactly these Russian headers:\n"
           "  *Сводка:*\n"
           "  (1-2 предложения: суть обсуждения, общая атмосфера и контекст без домыслов и драмы)\n\n"
           "  *Ключевые темы и обсуждение:*\n"
           "  - Сгруппируйте беседу по основным обсуждавшимся темам. Внутри каждой темы последовательно и в точной хронологии изложите, кто что сказал, аргументы участников и приведите подлинные прямые цитаты в кавычках (\"...\"). Все выводы и договорённости включайте прямо в соответствующие темы.\n\n"
           "- For English conversations, use headers:\n"
           "  *Summary:*\n"
           "  (1-2 sentences: core subject, atmosphere, and overall context without drama or assumptions)\n\n"
           "  *Key Topics & Discussion:*\n"
           "  - Group the conversation into the main topics discussed. For each topic, provide a chronological narrative of who said what, their positions, and authentic direct quotes (\"...\") from the participants in the exact order the discussion unfolded. Integrate any conclusions or results directly within their relevant topics.\n\n"
           "- For other languages, translate '*Summary:*' and '*Key Topics & Discussion:*' into that language.";
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
                    if (j.contains("enable_image_analysis") && j["enable_image_analysis"].is_boolean()) {
                        cfg.enable_image_analysis = j["enable_image_analysis"].get<bool>();
                    }
                    if (j.contains("enable_voice_transcription") && j["enable_voice_transcription"].is_boolean()) {
                        cfg.enable_voice_transcription = j["enable_voice_transcription"].get<bool>();
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
    if (auto v = get_env_or_map("ENABLE_IMAGE_ANALYSIS", env_file_map)) {
        std::string s = *v;
        cfg.enable_image_analysis = (s == "true" || s == "1" || s == "yes");
    }
    if (auto v = get_env_or_map("ENABLE_VOICE_TRANSCRIPTION", env_file_map)) {
        std::string s = *v;
        cfg.enable_voice_transcription = (s == "true" || s == "1" || s == "yes");
    }

    if (auto v = get_env_or_map("SYSTEM_PROMPT", env_file_map)) {
        cfg.system_prompt = *v;
    }

    // Support loading custom system prompt from a dedicated file (e.g. system_prompt.txt)
    std::string prompt_file_path;
    if (auto v = get_env_or_map("SYSTEM_PROMPT_FILE", env_file_map)) {
        prompt_file_path = *v;
    } else if (std::filesystem::exists("system_prompt.txt")) {
        prompt_file_path = "system_prompt.txt";
    } else if (std::filesystem::exists("../system_prompt.txt")) {
        prompt_file_path = "../system_prompt.txt";
    }

    if (!prompt_file_path.empty()) {
        try {
            std::ifstream pf(prompt_file_path);
            if (pf.is_open()) {
                std::stringstream buffer;
                buffer << pf.rdbuf();
                if (!buffer.str().empty()) {
                    cfg.system_prompt = buffer.str();
                    std::cout << "[Config] Loaded custom system prompt from: " << prompt_file_path << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Config] Warning: Failed to read custom prompt file " << prompt_file_path << ": " << e.what() << std::endl;
        }
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
