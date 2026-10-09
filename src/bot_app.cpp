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

bool extract_first_integer(const std::vector<std::string>& tokens, int64_t& out_num) {
    for (const auto& token : tokens) {
        std::string digits;
        for (char c : token) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                digits.push_back(c);
            } else if (!digits.empty()) {
                break;
            }
        }
        if (!digits.empty()) {
            try {
                out_num = std::stoll(digits);
                return true;
            } catch (...) {}
        }
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
    int64_t& out_n,
    bool& out_is_help
) const {
    out_is_help = false;
    out_n = config_.default_messages_to_process;

    std::string text = msg.get_effective_text();
    if (text.empty()) return false;

    std::string text_lower = to_lower(text);
    std::string bot_username_lower = to_lower(bot_->bot_user().username);
    std::string mention = "@" + bot_username_lower;

    bool mentioned = (!bot_username_lower.empty() && text_lower.find(mention) != std::string::npos);
    bool starts_with_cmd = (text_lower.rfind("/summary", 0) == 0 ||
                            text_lower.rfind("/start", 0) == 0 ||
                            text_lower.rfind("/help", 0) == 0);

    bool replied_to_bot = (msg.reply_to_message &&
                           msg.reply_to_message->from.id == bot_->bot_user().id);

    if (!mentioned && !starts_with_cmd && !replied_to_bot) {
        return false;
    }

    // Check for help/start commands
    if (text_lower.rfind("/start", 0) == 0 ||
        text_lower.rfind("/help", 0) == 0 ||
        text_lower.find("help") != std::string::npos ||
        text_lower.find("помощь") != std::string::npos) {
        out_is_help = true;
        return true;
    }

    // Extract message count number
    auto tokens = split_words(text);
    int64_t parsed_num = 0;
    if (extract_first_integer(tokens, parsed_num)) {
        out_n = parsed_num;
    } else {
        out_n = config_.default_messages_to_process;
    }

    return true;
}

void BotApp::handle_message(const TelegramMessage& msg) {
    // Ignore messages from bots to prevent feedback loops and context pollution
    if (msg.from.is_bot) return;

    std::string effective_text = msg.get_effective_text();
    if (effective_text.empty()) return;

    int64_t n = 0;
    bool is_help = false;
    bool is_invocation = parse_bot_invocation(msg, n, is_help);

    if (is_invocation) {
        if (is_help) {
            send_help(msg.chat.id, msg.message_id);
            return;
        }

        // Summary requested
        if (n <= 0) {
            bot_->send_message(
                msg.chat.id,
                "Количество сообщений должно быть больше 0 (максимум 1500).\n"
                "Пример использования: @" + bot_->bot_user().username + " 300",
                msg.message_id
            );
            return;
        }

        std::cout << "[BotApp] Summary requested for chat " << msg.chat.id 
                  << " (" << msg.chat.title << "), requested count: " << n << std::endl;

        execute_summary_async(msg.chat.id, msg.message_id, n);
    } else {
        // Regular conversation message: store in database
        ChatMessage cm;
        cm.chat_id = msg.chat.id;
        cm.message_id = msg.message_id;
        cm.user_id = msg.from.id;
        cm.username = msg.from.username;
        cm.first_name = msg.from.display_name();
        cm.timestamp = msg.date;

        if (msg.reply_to_message) {
            cm.reply_to_message_id = msg.reply_to_message->message_id;
            cm.reply_to_user = msg.reply_to_message->from.display_name();
        }

        cm.text = effective_text;
        db_.save_message(cm);

        // Periodically prune messages to keep DB lean
        if (++message_counter_ % 50 == 0) {
            db_.prune_chat_history(msg.chat.id, config_.max_stored_messages_per_chat);
        }
    }
}

void BotApp::send_help(int64_t chat_id, int64_t reply_to_id) {
    std::string bot_name = bot_->bot_user().username;
    std::string help_text =
        "*Бот для создания кратких сводок (самари) бесед.*\n\n"
        "*Как пользоваться:*\n"
        "• `@" + bot_name + " 500` — сделать самари последних 500 сообщений\n"
        "• `@" + bot_name + " 100` — сделать самари последних 100 сообщений\n"
        "• `/summary 200` — альтернативная команда со счетчиком сообщений\n"
        "• `@" + bot_name + "` — самари с дефолтным числом (" +
        std::to_string(config_.default_messages_to_process) + " сообщений)\n\n"
        "*Ограничения:*\n"
        "• Максимальное число сообщений для анализа: *1500*\n\n"
        "*Важная настройка для работы в группах:*\n"
        "Telegram-боты по умолчанию видят только команды. Чтобы бот читал все сообщения чата и мог делать по ним сводку:\n"
        "1. Откройте @BotFather\n"
        "2. Вызовите команду `/setprivacy`\n"
        "3. Выберите этого бота и нажмите *Disable*\n"
        "*(либо просто назначьте бота администратором группы)*.";

    bot_->send_message(chat_id, help_text, reply_to_id, "Markdown");
}

void BotApp::execute_summary_async(
    int64_t chat_id,
    int64_t request_msg_id,
    int64_t count
) {
    // Run summary generation in a detached thread so update polling is never blocked
    std::thread([this, chat_id, request_msg_id, count]() {
        try {
            int64_t effective_count = std::min(count, config_.max_messages_to_process);

            // Send initial progress status message without emoji
            std::string status_text = "Сбор последних " + std::to_string(effective_count) + " сообщений и составление самари...";
            int64_t status_msg_id = bot_->send_message(chat_id, status_text, request_msg_id, "Markdown");

            // Start typing indicator loop using std::jthread
            std::atomic<bool> is_generating{true};
            std::jthread typing_thread([this, chat_id, &is_generating](std::stop_token st) {
                while (!st.stop_requested() && is_generating.load()) {
                    bot_->send_chat_action(chat_id, "typing");
                    for (int i = 0; i < 40 && !st.stop_requested() && is_generating.load(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            });

            // Retrieve messages from database
            auto messages = db_.get_last_messages(chat_id, effective_count);

            if (messages.empty()) {
                is_generating.store(false);
                typing_thread.request_stop();
                if (status_msg_id != 0) bot_->delete_message(chat_id, status_msg_id);

                std::string empty_msg =
                    "В истории этого чата пока нет сохраненных сообщений.\n\n"
                    "Чтобы бот мог читать историю беседы:\n"
                    "1. Убедитесь, что бот добавлен в чат.\n"
                    "2. Отключите Privacy Mode в @BotFather (`/setprivacy` -> *Disable*) "
                    "или назначьте бота администратором группы.\n"
                    "3. Отправляйте сообщения в чат, и бот будет сохранять их для последующих самари!";
                bot_->send_message(chat_id, empty_msg, request_msg_id, "Markdown");
                return;
            }

            std::string error;
            std::string summary = generator_->generate(messages, count, error);

            is_generating.store(false);
            typing_thread.request_stop();

            // Delete temporary status indicator
            if (status_msg_id != 0) {
                bot_->delete_message(chat_id, status_msg_id);
            }

            if (!summary.empty()) {
                bot_->send_message(chat_id, summary, request_msg_id, "Markdown");
            } else {
                std::string err_msg = "Ошибка при генерации самари: " + error;
                bot_->send_message(chat_id, err_msg, request_msg_id, "");
            }
        } catch (const std::exception& e) {
            std::cerr << "[BotApp] Exception in execute_summary_async: " << e.what() << std::endl;
        }
    }).detach();
}

} // namespace summarybot
