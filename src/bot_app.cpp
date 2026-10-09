#include "bot_app.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace summarybot {

namespace {

std::string to_lower(std::string_view s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

std::vector<std::string> split_words(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool parse_time_token(
    const std::string& token,
    int tz_offset,
    int64_t& out_seconds,
    std::string& out_desc
) {
    std::string lower = to_lower(token);
    int64_t now = std::time(nullptr);

    if (lower == "today" || lower == "сегодня") {
        int64_t local_now = now + tz_offset * 3600;
        int64_t local_midnight = (local_now / 86400) * 86400;
        int64_t midnight_utc = local_midnight - tz_offset * 3600;
        out_seconds = std::max<int64_t>(1, now - midnight_utc);
        out_desc = "today";
        return true;
    }

    if (lower == "yesterday" || lower == "вчера") {
        int64_t local_now = now + tz_offset * 3600;
        int64_t local_midnight = (local_now / 86400) * 86400;
        int64_t yesterday_utc = (local_midnight - 86400) - tz_offset * 3600;
        out_seconds = std::max<int64_t>(1, now - yesterday_utc);
        out_desc = "yesterday and today";
        return true;
    }

    // Check for suffix h, m, d
    if (lower.size() >= 2) {
        std::string suffix;
        int64_t multiplier = 0;
        std::string unit_desc;
        if (lower.ends_with("h")) {
            suffix = "h"; multiplier = 3600; unit_desc = "hour";
        } else if (lower.ends_with("ч")) {
            suffix = "ч"; multiplier = 3600; unit_desc = "hour";
        } else if (lower.ends_with("m")) {
            suffix = "m"; multiplier = 60; unit_desc = "minute";
        } else if (lower.ends_with("м")) {
            suffix = "м"; multiplier = 60; unit_desc = "minute";
        } else if (lower.ends_with("d")) {
            suffix = "d"; multiplier = 86400; unit_desc = "day";
        } else if (lower.ends_with("д")) {
            suffix = "д"; multiplier = 86400; unit_desc = "day";
        }

        if (multiplier > 0) {
            std::string num_part = lower.substr(0, lower.size() - suffix.size());
            bool all_digits = !num_part.empty() && std::all_of(num_part.begin(), num_part.end(), ::isdigit);
            if (all_digits) {
                try {
                    int64_t val = std::stoll(num_part);
                    if (val > 0) {
                        out_seconds = val * multiplier;
                        out_desc = "the last " + std::to_string(val) + " " + unit_desc + (val > 1 ? "s" : "");
                        return true;
                    }
                } catch (...) {}
            }
        }
    }

    return false;
}

bool extract_integer(const std::string& token, int64_t& out_num) {
    if (token.empty()) return false;
    bool all_digits = std::all_of(token.begin(), token.end(), ::isdigit);
    if (all_digits) {
        try {
            out_num = std::stoll(token);
            return true;
        } catch (...) {}
    }
    return false;
}

} // namespace

BotApp::BotApp(Config config) : config_(std::move(config)) {}

BotApp::~BotApp() {
    stop();
}

bool BotApp::init() {
    std::cout << "[BotApp] Initializing database at: " << config_.db_path << std::endl;
    if (!db_.init(config_.db_path)) {
        std::cerr << "[BotApp] Failed to initialize database." << std::endl;
        return false;
    }

    bot_ = std::make_unique<TelegramBot>(
        config_.telegram_bot_token,
        config_.telegram_api_base_url,
        config_.proxy_url
    );

    std::cout << "[BotApp] Connecting to Telegram..." << std::endl;
    if (!bot_->init()) {
        std::cerr << "[BotApp] Failed to authenticate Telegram bot." << std::endl;
        return false;
    }

    llm_ = std::make_unique<LlmClient>(
        config_.llm_api_key,
        config_.llm_api_url,
        config_.llm_model,
        config_.llm_temperature,
        config_.llm_timeout_seconds,
        config_.proxy_url
    );

    generator_ = std::make_unique<SummaryGenerator>(*llm_, config_);

    std::cout << "[BotApp] Bot ready: @" << bot_->bot_user().username 
              << " | LLM: " << config_.llm_model 
              << " (" << config_.llm_api_url << ")" << std::endl;

    return true;
}

void BotApp::stop() {
    running_ = false;
}

void BotApp::run() {
    running_ = true;
    int64_t offset = 0;

    std::cout << "[BotApp] Starting update polling loop..." << std::endl;

    while (running_) {
        try {
            auto updates = bot_->get_updates(offset, 25);
            for (const auto& update : updates) {
                if (update.update_id >= offset) {
                    offset = update.update_id + 1;
                }
                process_update(update);
            }
        } catch (const std::exception& e) {
            std::cerr << "[BotApp] Exception in polling loop: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    std::cout << "[BotApp] Polling loop stopped." << std::endl;
}

void BotApp::process_update(const TelegramUpdate& update) {
    if (update.message) {
        handle_message(*update.message);
    }
}

bool BotApp::parse_bot_invocation(
    const TelegramMessage& msg,
    const ChatSettings& settings,
    SummaryRequest& out_req,
    bool& out_is_help,
    bool& out_is_tz_cmd
) const {
    out_is_help = false;
    out_is_tz_cmd = false;
    out_req = SummaryRequest{};
    out_req.count = config_.default_messages_to_process;
    out_req.window_desc = "the last " + std::to_string(out_req.count) + " messages";

    std::string text = msg.get_effective_text();
    if (text.empty()) return false;

    std::string text_lower = to_lower(text);
    std::string bot_username_lower = to_lower(bot_->bot_user().username);
    std::string mention = "@" + bot_username_lower;

    bool mentioned = (!bot_username_lower.empty() && text_lower.find(mention) != std::string::npos);
    bool starts_with_cmd = (text_lower.rfind("/summary", 0) == 0 ||
                            text_lower.rfind("/start", 0) == 0 ||
                            text_lower.rfind("/help", 0) == 0 ||
                            text_lower.rfind("/timezone", 0) == 0);

    bool replied_to_bot = (msg.reply_to_message &&
                           msg.reply_to_message->from.id == bot_->bot_user().id);

    if (!mentioned && !starts_with_cmd && !replied_to_bot) {
        return false;
    }

    // Timezone command check
    if (text_lower.rfind("/timezone", 0) == 0 ||
        (mentioned && text_lower.find("timezone") != std::string::npos)) {
        out_is_tz_cmd = true;
        return true;
    }

    // Help/Start command check
    if (text_lower.rfind("/start", 0) == 0 ||
        text_lower.rfind("/help", 0) == 0 ||
        text_lower.find("help") != std::string::npos ||
        text_lower.find("помощь") != std::string::npos) {
        out_is_help = true;
        return true;
    }

    // Tokenize text to extract count, time window, or topic focus
    auto tokens = split_words(text);
    int64_t now = std::time(nullptr);
    bool has_explicit_scope = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        std::string token_clean = token;
        // Strip trailing punctuation
        while (!token_clean.empty() && (token_clean.back() == ',' || token_clean.back() == '.' || token_clean.back() == ':')) {
            token_clean.pop_back();
        }

        std::string token_clean_lower = to_lower(token_clean);

        // Skip bot mention and /summary command
        if (token_clean_lower == mention || token_clean_lower.rfind("/summary", 0) == 0) {
            continue;
        }

        // Check for topic focus markers
        if (token_clean_lower == "about" || token_clean_lower == "focus" || token_clean_lower == "topic" ||
            token_clean_lower == "про" || token_clean_lower == "о" || token_clean_lower == "тема") {
            // Remaining words are topic
            std::ostringstream topic_oss;
            for (size_t j = i + 1; j < tokens.size(); ++j) {
                if (j > i + 1) topic_oss << " ";
                topic_oss << tokens[j];
            }
            out_req.topic_filter = topic_oss.str();
            break;
        }

        // Check for time window (e.g. 24h, today, 30m)
        int64_t seconds = 0;
        std::string time_desc;
        if (!has_explicit_scope && parse_time_token(token_clean, settings.timezone_offset, seconds, time_desc)) {
            out_req.type = QueryType::TimeWindow;
            out_req.since_timestamp = now - seconds;
            out_req.window_desc = time_desc;
            has_explicit_scope = true;
            continue;
        }

        // Check for message count number
        int64_t num = 0;
        if (!has_explicit_scope && extract_integer(token_clean, num)) {
            out_req.type = QueryType::Count;
            out_req.count = num;
            out_req.window_desc = "the last " + std::to_string(num) + " messages";
            has_explicit_scope = true;
            continue;
        }
    }

    return true;
}

void BotApp::handle_timezone_cmd(const TelegramMessage& msg) {
    auto tokens = split_words(msg.get_effective_text());
    auto current_settings = db_.get_chat_settings(msg.chat.id);

    // If no argument provided, show current timezone
    if (tokens.size() <= 1 || (tokens.size() == 2 && tokens[0].find("@") != std::string::npos)) {
        std::string reply = std::string("Current chat timezone: UTC") +
                            (current_settings.timezone_offset >= 0 ? "+" : "") +
                            std::to_string(current_settings.timezone_offset) +
                            " (" + current_settings.timezone_name + ").\n\n"
                            "To change timezone, use: /timezone <offset_or_name>\n"
                            "Examples: /timezone +3, /timezone -5, /timezone UTC+2, /timezone MSK";
        bot_->send_message(msg.chat.id, reply, msg.message_id, "", msg.thread_id);
        return;
    }

    std::string arg = tokens.back();
    std::string arg_upper = arg;
    std::transform(arg_upper.begin(), arg_upper.end(), arg_upper.begin(), ::toupper);

    int offset = 3;
    std::string tz_name = "MSK";

    if (arg_upper == "MSK" || arg_upper == "MOSCOW") {
        offset = 3; tz_name = "MSK";
    } else if (arg_upper == "UTC" || arg_upper == "GMT") {
        offset = 0; tz_name = "UTC";
    } else if (arg_upper == "EST") {
        offset = -5; tz_name = "EST";
    } else if (arg_upper == "EDT") {
        offset = -4; tz_name = "EDT";
    } else if (arg_upper == "CST") {
        offset = -6; tz_name = "CST";
    } else if (arg_upper == "PST") {
        offset = -8; tz_name = "PST";
    } else if (arg_upper == "PDT") {
        offset = -7; tz_name = "PDT";
    } else if (arg_upper == "CET") {
        offset = 1; tz_name = "CET";
    } else if (arg_upper == "CEST") {
        offset = 2; tz_name = "CEST";
    } else {
        // Parse numerical offset, e.g., +3, -5, UTC+2
        std::string num_str = arg;
        if (num_str.rfind("UTC", 0) == 0 || num_str.rfind("GMT", 0) == 0) {
            num_str = num_str.substr(3);
        }
        try {
            offset = std::stoi(num_str);
            if (offset < -12 || offset > 14) {
                bot_->send_message(msg.chat.id, "Invalid offset. Timezone offset must be between -12 and +14.", msg.message_id, "", msg.thread_id);
                return;
            }
            tz_name = std::string("UTC") + (offset >= 0 ? "+" : "") + std::to_string(offset);
        } catch (...) {
            bot_->send_message(msg.chat.id, "Invalid timezone argument. Examples: /timezone +3, /timezone -5, /timezone MSK", msg.message_id, "", msg.thread_id);
            return;
        }
    }

    db_.set_chat_timezone(msg.chat.id, offset, tz_name);
    std::string reply = std::string("Chat timezone updated to UTC") + (offset >= 0 ? "+" : "") + std::to_string(offset) + " (" + tz_name + ").";
    bot_->send_message(msg.chat.id, reply, msg.message_id, "", msg.thread_id);
}

void BotApp::handle_message(const TelegramMessage& msg) {
    if (msg.from.is_bot) return;

    std::string effective_text = msg.get_effective_text();
    if (effective_text.empty()) return;

    ChatSettings chat_settings = db_.get_chat_settings(msg.chat.id);

    SummaryRequest req;
    bool is_help = false;
    bool is_tz_cmd = false;
    bool is_invocation = parse_bot_invocation(msg, chat_settings, req, is_help, is_tz_cmd);

    if (is_invocation) {
        if (is_help) {
            send_help(msg.chat.id, msg.thread_id, msg.message_id);
            return;
        }

        if (is_tz_cmd) {
            handle_timezone_cmd(msg);
            return;
        }

        // Admin-only check
        if (config_.admin_only_summaries && !bot_->is_chat_admin(msg.chat.id, msg.from.id)) {
            bot_->send_message(
                msg.chat.id,
                "Only chat administrators are authorized to request summaries in this group.",
                msg.message_id,
                "",
                msg.thread_id
            );
            return;
        }

        // Rate limiting check per (chat_id, thread_id)
        int64_t now = std::time(nullptr);
        {
            std::lock_guard<std::mutex> lock(rate_limit_mutex_);
            auto key = std::make_pair(msg.chat.id, msg.thread_id);
            auto it = last_summary_time_.find(key);
            if (it != last_summary_time_.end()) {
                int64_t elapsed = now - it->second;
                if (elapsed < config_.rate_limit_seconds) {
                    int64_t wait = config_.rate_limit_seconds - elapsed;
                    bot_->send_message(
                        msg.chat.id,
                        "Rate limit active. Please wait " + std::to_string(wait) + " seconds before requesting another summary.",
                        msg.message_id,
                        "",
                        msg.thread_id
                    );
                    return;
                }
            }
            last_summary_time_[key] = now;
        }

        if (req.type == QueryType::Count && req.count <= 0) {
            bot_->send_message(
                msg.chat.id,
                "The message count must be greater than 0 (max 1500).\nExample usage: @" + bot_->bot_user().username + " 300",
                msg.message_id,
                "",
                msg.thread_id
            );
            return;
        }

        std::cout << "[BotApp] Summary requested for chat " << msg.chat.id 
                  << " (thread: " << msg.thread_id << ", title: " << msg.chat.title 
                  << "), scope: " << req.window_desc << std::endl;

        execute_summary_async(msg.chat.id, msg.thread_id, msg.message_id, req);
    } else {
        // Regular conversation message: store in database
        ChatMessage cm;
        cm.chat_id = msg.chat.id;
        cm.thread_id = msg.thread_id;
        cm.message_id = msg.message_id;
        cm.user_id = msg.from.id;
        cm.username = msg.from.username;
        cm.first_name = msg.from.display_name();
        cm.timestamp = msg.date;

        if (msg.reply_to_message) {
            cm.reply_to_message_id = msg.reply_to_message->message_id;
            std::string reply_name = msg.reply_to_message->from.display_name();
            if (!msg.reply_to_message->from.username.empty()) {
                reply_name += " (@" + msg.reply_to_message->from.username + ")";
            }
            std::string snippet = msg.reply_to_message->get_effective_text();
            if (!snippet.empty()) {
                std::replace(snippet.begin(), snippet.end(), '\n', ' ');
                if (snippet.size() > 50) {
                    snippet = snippet.substr(0, 47) + "...";
                }
                cm.reply_to_user = reply_name + ": \"" + snippet + "\"";
            } else {
                cm.reply_to_user = reply_name;
            }
        }

        cm.text = effective_text;
        db_.save_message(cm);

        // Asynchronous image analysis for chat context if a photo was attached
        if (config_.enable_image_analysis && !msg.photo_file_id.empty()) {
            analyze_and_update_image_async(msg.chat.id, msg.message_id, msg.photo_file_id, msg.caption);
        }

        // Periodically prune messages to keep DB lean
        if (++message_counter_ % 50 == 0) {
            db_.prune_chat_history(msg.chat.id, msg.thread_id, config_.max_stored_messages_per_chat);
        }
    }
}

void BotApp::send_help(int64_t chat_id, int64_t thread_id, int64_t reply_to_id) {
    std::string bot_name = bot_->bot_user().username;
    std::string help_text =
        "*Telegram Conversation Summary Bot*\n\n"
        "*Summary Options:*\n"
        "• `@" + bot_name + " 500` — summarize the last 500 messages\n"
        "• `@" + bot_name + " 24h` — summarize the last 24 hours\n"
        "• `@" + bot_name + " today` — summarize all discussions from today\n"
        "• `@" + bot_name + " 300 about release` — summarize messages with topic focus\n"
        "• `/timezone +3` — set chat timezone offset\n\n"
        "*Limits & Features:*\n"
        "• Hard limit: 1500 messages per request\n"
        "• Auto-scans images in conversation history to provide visual context\n"
        "• Strict factual grounding and reply-chain tracing\n"
        "• Automatically scopes to Forum Topics / Threads if invoked inside one\n"
        "• Strictly emoji-free and language-adaptive output\n\n"
        "*Important Group Setup:*\n"
        "To allow the bot to read messages in groups:\n"
        "1. Open @BotFather\n"
        "2. Send `/setprivacy`\n"
        "3. Choose this bot and click *Disable*\n"
        "*(or promote the bot to Group Administrator)*.";

    bot_->send_message(chat_id, help_text, reply_to_id, "Markdown", thread_id);
}

void BotApp::execute_summary_async(
    int64_t chat_id,
    int64_t thread_id,
    int64_t request_msg_id,
    const SummaryRequest& req
) {
    std::thread([this, chat_id, thread_id, request_msg_id, req]() {
        try {
            auto chat_settings = db_.get_chat_settings(chat_id);

            // Send initial progress status message without emoji
            std::string status_text = "Collecting messages and generating summary for " + req.window_desc + "...";
            int64_t status_msg_id = bot_->send_message(chat_id, status_text, request_msg_id, "Markdown", thread_id);

            // Start typing indicator loop using std::jthread
            std::atomic<bool> is_generating{true};
            std::jthread typing_thread([this, chat_id, thread_id, &is_generating](std::stop_token st) {
                while (!st.stop_requested() && is_generating.load()) {
                    bot_->send_chat_action(chat_id, "typing", thread_id);
                    for (int i = 0; i < 40 && !st.stop_requested() && is_generating.load(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            });

            // Retrieve messages from database (filtered by chat_id and thread_id)
            std::vector<ChatMessage> messages;
            if (req.type == QueryType::TimeWindow) {
                messages = db_.get_messages_since(chat_id, thread_id, req.since_timestamp, config_.max_messages_to_process);
            } else {
                int64_t effective_count = std::min(req.count, config_.max_messages_to_process);
                messages = db_.get_last_messages(chat_id, thread_id, effective_count);
            }

            if (messages.empty()) {
                is_generating.store(false);
                typing_thread.request_stop();
                if (status_msg_id != 0) bot_->delete_message(chat_id, status_msg_id);

                std::string empty_msg =
                    std::string("No messages found in this chat") +
                    (thread_id != 0 ? " topic" : "") +
                    " for the requested scope.\n\n"
                    "Ensure the bot is added to the chat and has Privacy Mode disabled in @BotFather (/setprivacy -> Disable).";
                bot_->send_message(chat_id, empty_msg, request_msg_id, "Markdown", thread_id);
                return;
            }

            std::string error;
            std::string summary = generator_->generate(
                messages,
                req.window_desc,
                req.topic_filter,
                chat_settings.timezone_offset,
                chat_settings.timezone_name,
                error
            );

            is_generating.store(false);
            typing_thread.request_stop();

            // Delete temporary status indicator
            if (status_msg_id != 0) {
                bot_->delete_message(chat_id, status_msg_id);
            }

            if (!summary.empty()) {
                bot_->send_message(chat_id, summary, request_msg_id, "Markdown", thread_id);
            } else {
                std::string err_msg = "Error generating summary: " + error;
                bot_->send_message(chat_id, err_msg, request_msg_id, "", thread_id);
            }
        } catch (const std::exception& e) {
            std::cerr << "[BotApp] Exception in execute_summary_async: " << e.what() << std::endl;
        }
    }).detach();
}

void BotApp::analyze_and_update_image_async(
    int64_t chat_id,
    int64_t message_id,
    const std::string& file_id,
    const std::string& caption
) {
    std::thread([this, chat_id, message_id, file_id, caption]() {
        try {
            std::string file_path = bot_->get_file_path(file_id);
            if (file_path.empty()) return;

            std::string image_bytes = bot_->download_file(file_path);
            if (image_bytes.empty()) return;

            std::string mime_type = "image/jpeg";
            if (file_path.ends_with(".png")) mime_type = "image/png";
            else if (file_path.ends_with(".webp")) mime_type = "image/webp";

            std::string err;
            std::string prompt = "Describe what is shown in this image in one concise phrase or short sentence (e.g. 'скриншот с ошибкой компиляции', 'фото кота', 'мем про работу'). Do not include conversational filler or emojis.";
            std::string description = llm_->describe_image(image_bytes, mime_type, prompt, err);

            if (!description.empty()) {
                std::replace(description.begin(), description.end(), '\n', ' ');
                while (!description.empty() && description.back() == ' ') description.pop_back();
                if (description.size() > 200) {
                    description = description.substr(0, 195) + "...";
                }

                std::string updated_text = "[Photo: " + description + "]";
                if (!caption.empty()) {
                    updated_text += " (reaction: \"" + caption + "\")";
                }

                db_.update_message_text(chat_id, message_id, updated_text);
                std::cout << "[BotApp] Auto-scanned photo " << message_id 
                          << " into context: " << description << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[BotApp] Error during background image analysis: " << e.what() << std::endl;
        }
    }).detach();
}

} // namespace summarybot
