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
 * @brief Thread-safe SQLite3 storage layer for chat messages.
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
     * @brief Fetch up to `limit` most recent messages for a given chat,
     *        returned in chronological order (oldest to newest).
     */
    std::vector<ChatMessage> get_last_messages(int64_t chat_id, int64_t limit);

    /**
     * @brief Count total stored messages for a specific chat.
     */
    int64_t count_messages(int64_t chat_id);

    /**
     * @brief Prune messages in a chat so only the newest `keep_count` messages remain.
     */
    void prune_chat_history(int64_t chat_id, int64_t keep_count);

private:
    sqlite3* db_ = nullptr;
    std::mutex db_mutex_;
    std::string db_path_;

    // Prepared statements for high performance
    sqlite3_stmt* stmt_insert_ = nullptr;
    sqlite3_stmt* stmt_query_last_ = nullptr;
    sqlite3_stmt* stmt_count_ = nullptr;
    sqlite3_stmt* stmt_prune_ = nullptr;

    bool prepare_statements();
    void finalize_statements();
};

} // namespace summarybot
