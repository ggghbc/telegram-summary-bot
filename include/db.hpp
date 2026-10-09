#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <sqlite3.h>

namespace summarybot {

/**
 * @brief Represents a stored Telegram chat message.
 */
struct ChatMessage {
    int64_t id = 0;
    int64_t chat_id = 0;
    int64_t thread_id = 0; // Telegram Forum Topic / Thread ID (0 = general chat)
    int64_t message_id = 0;
    int64_t user_id = 0;
    std::string username;
    std::string first_name;
    int64_t timestamp = 0;
    int64_t reply_to_message_id = 0;
    std::string reply_to_user;
    std::string text;
};

/**
 * @brief Per-chat configuration settings stored in database.
 */
struct ChatSettings {
    int64_t chat_id = 0;
    int timezone_offset = 3; // Default UTC+3 (Moscow)
    std::string timezone_name = "MSK";
};

/**
 * @brief Thread-safe SQLite3 storage layer for chat messages and settings.
 */
class Database {
public:
    Database();
    ~Database();

    // Disable copying
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /**
     * @brief Initialize SQLite database with WAL mode and create necessary tables/indices.
     */
    bool init(const std::string& db_path);

    /**
     * @brief Close database connection.
     */
    void close();

    /**
     * @brief Save or update a chat message.
     */
    bool save_message(const ChatMessage& msg);

    /**
     * @brief Update the text content of an existing stored message (e.g. after image analysis).
     */
    bool update_message_text(int64_t chat_id, int64_t message_id, const std::string& new_text);

    /**
     * @brief Fetch up to `limit` most recent messages for a given chat and thread,
     *        returned in chronological order (oldest to newest).
     */
    std::vector<ChatMessage> get_last_messages(int64_t chat_id, int64_t thread_id, int64_t limit);

    /**
     * @brief Fetch messages since a specific timestamp for a given chat and thread,
     *        returned in chronological order (oldest to newest).
     */
    std::vector<ChatMessage> get_messages_since(int64_t chat_id, int64_t thread_id, int64_t since_timestamp, int64_t limit);

    /**
     * @brief Count total stored messages for a specific chat and thread.
     */
    int64_t count_messages(int64_t chat_id, int64_t thread_id);

    /**
     * @brief Prune messages in a chat so only the newest `keep_count` messages remain.
     */
    void prune_chat_history(int64_t chat_id, int64_t thread_id, int64_t keep_count);

    /**
     * @brief Set or update timezone for a specific chat.
     */
    bool set_chat_timezone(int64_t chat_id, int offset, const std::string& name);

    /**
     * @brief Retrieve timezone settings for a specific chat (defaults to MSK / UTC+3 if not set).
     */
    ChatSettings get_chat_settings(int64_t chat_id);

private:
    sqlite3* db_ = nullptr;
    std::mutex db_mutex_;
    std::string db_path_;

    // Prepared statements for maximum performance
    sqlite3_stmt* stmt_insert_ = nullptr;
    sqlite3_stmt* stmt_query_last_ = nullptr;
    sqlite3_stmt* stmt_query_since_ = nullptr;
    sqlite3_stmt* stmt_count_ = nullptr;
    sqlite3_stmt* stmt_prune_ = nullptr;
    sqlite3_stmt* stmt_set_settings_ = nullptr;
    sqlite3_stmt* stmt_get_settings_ = nullptr;

    bool prepare_statements();
    void finalize_statements();
};

} // namespace summarybot
