#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstdint>
#include "http_client.hpp"

namespace summarybot {

struct TelegramUser {
    int64_t id = 0;
    bool is_bot = false;
    std::string first_name;
    std::string last_name;
    std::string username;

    std::string display_name() const {
        if (!first_name.empty()) {
            if (!last_name.empty()) return first_name + " " + last_name;
            return first_name;
        }
        if (!username.empty()) return username;
        return "User" + std::to_string(id);
    }

    bool is_bot_user() const {
        if (is_bot) return true;
        if (username.size() >= 3) {
            std::string lower = username;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.ends_with("bot")) return true;
        }
        return false;
    }
};

struct TelegramChat {
    int64_t id = 0;
    std::string type; // "private", "group", "supergroup", "channel"
    std::string title;
};

struct TelegramMessage {
    int64_t message_id = 0;
    int64_t thread_id = 0; // Telegram Forum Topic / Thread ID (0 = general)
    TelegramUser from;
    TelegramChat chat;
    int64_t date = 0;
    int64_t edit_date = 0; // Timestamp when message was edited (if edited)
    bool is_edited = false;
    std::string text;
    std::string caption;
    std::string media_type; // e.g. "photo", "voice", "document", etc.
    std::string photo_file_id; // Telegram file_id if message contains a photo or image
    std::string voice_file_id; // Telegram file_id if voice message or video note
    std::shared_ptr<TelegramMessage> reply_to_message;

    std::string get_effective_text() const {
        if (!text.empty()) return text;
        if (!caption.empty()) {
            if (!media_type.empty()) return "[" + media_type + "] " + caption;
            return caption;
        }
        if (!media_type.empty()) return "[" + media_type + "]";
        return "";
    }
};

struct TelegramUpdate {
    int64_t update_id = 0;
    std::optional<TelegramMessage> message;
    bool is_edit = false;
};

/**
 * @brief High-performance client for Telegram Bot API.
 */
class TelegramBot {
public:
    TelegramBot(
        const std::string& token,
        const std::string& base_url = "https://api.telegram.org",
        const std::string& proxy = ""
    );

    /**
     * @brief Connect and verify bot credentials via getMe.
     */
    bool init();

    /**
     * @brief Return bot's own profile info.
     */
    const TelegramUser& bot_user() const { return bot_user_; }

    /**
     * @brief Fetch updates using long-polling.
     */
    std::vector<TelegramUpdate> get_updates(int64_t offset, int timeout_seconds = 25);

    /**
     * @brief Send a text message. If text exceeds 4096 characters, automatically splits it.
     * @return ID of the first sent message, or 0 on failure.
     */
    int64_t send_message(
        int64_t chat_id,
        const std::string& text,
        int64_t reply_to_message_id = 0,
        const std::string& parse_mode = "Markdown",
        int64_t thread_id = 0
    );

    /**
     * @brief Edit an existing message's text.
     */
    bool edit_message_text(
        int64_t chat_id,
        int64_t message_id,
        const std::string& text,
        const std::string& parse_mode = "Markdown"
    );

    /**
     * @brief Send chat action (e.g., "typing").
     */
    bool send_chat_action(int64_t chat_id, const std::string& action = "typing", int64_t thread_id = 0);

    /**
     * @brief Delete a message.
     */
    bool delete_message(int64_t chat_id, int64_t message_id);

    /**
     * @brief Check whether a specific user is a creator or administrator of the chat.
     */
    bool is_chat_admin(int64_t chat_id, int64_t user_id);

    /**
     * @brief Get file_path for a Telegram file_id using getFile API.
     */
    std::string get_file_path(const std::string& file_id);

    /**
     * @brief Download binary file contents for a given file_path.
     */
    std::string download_file(const std::string& file_path);

    /**
     * @brief Split long text into chunks on clean, UTF-8 safe boundaries.
     */
    static std::vector<std::string> split_message(const std::string& text, size_t max_len = 3900);

private:
    std::string token_;
    std::string base_url_;
    std::string proxy_;
    TelegramUser bot_user_;
    HttpClient http_;

    std::string build_api_url(const std::string& method) const;
    static TelegramMessage parse_message_json(const void* json_node);
};

} // namespace summarybot
