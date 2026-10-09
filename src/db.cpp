#include "db.hpp"
#include <iostream>
#include <algorithm>

namespace summarybot {

Database::Database() = default;

Database::~Database() {
    close();
}

bool Database::init(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    db_path_ = db_path;

    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[Database] Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        close();
        return false;
    }

    // Set performance PRAGMAs
    char* err_msg = nullptr;
    const char* pragma_sql =
        "PRAGMA journal_mode = WAL;"
        "PRAGMA synchronous = NORMAL;"
        "PRAGMA cache_size = -64000;"
        "PRAGMA temp_store = MEMORY;";
    
    if (sqlite3_exec(db_, pragma_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "[Database] Warning setting pragmas: " << (err_msg ? err_msg : "unknown") << std::endl;
        sqlite3_free(err_msg);
    }

    // Create table & indices
    const char* schema_sql =
        "CREATE TABLE IF NOT EXISTS messages ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    chat_id INTEGER NOT NULL,"
        "    message_id INTEGER NOT NULL,"
        "    user_id INTEGER NOT NULL,"
        "    username TEXT,"
        "    first_name TEXT,"
        "    timestamp INTEGER NOT NULL,"
        "    reply_to_message_id INTEGER DEFAULT 0,"
        "    reply_to_user TEXT,"
        "    text TEXT NOT NULL,"
        "    UNIQUE(chat_id, message_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_messages_chat_msg ON messages(chat_id, message_id DESC);"
        "CREATE INDEX IF NOT EXISTS idx_messages_chat_time ON messages(chat_id, timestamp);";

    if (sqlite3_exec(db_, schema_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "[Database] Error creating schema: " << (err_msg ? err_msg : "unknown") << std::endl;
        sqlite3_free(err_msg);
        return false;
    }

    return prepare_statements();
}

bool Database::prepare_statements() {
    const char* sql_insert =
        "INSERT INTO messages (chat_id, message_id, user_id, username, first_name, timestamp, reply_to_message_id, reply_to_user, text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(chat_id, message_id) DO UPDATE SET "
        "text = excluded.text, "
        "username = excluded.username, "
        "first_name = excluded.first_name;";

    if (sqlite3_prepare_v2(db_, sql_insert, -1, &stmt_insert_, nullptr) != SQLITE_OK) {
        std::cerr << "[Database] Failed to prepare insert stmt: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    const char* sql_query_last =
        "SELECT id, chat_id, message_id, user_id, username, first_name, timestamp, reply_to_message_id, reply_to_user, text "
        "FROM messages WHERE chat_id = ? ORDER BY message_id DESC LIMIT ?;";

    if (sqlite3_prepare_v2(db_, sql_query_last, -1, &stmt_query_last_, nullptr) != SQLITE_OK) {
        std::cerr << "[Database] Failed to prepare query last stmt: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    const char* sql_count = "SELECT COUNT(*) FROM messages WHERE chat_id = ?;";
    if (sqlite3_prepare_v2(db_, sql_count, -1, &stmt_count_, nullptr) != SQLITE_OK) {
        std::cerr << "[Database] Failed to prepare count stmt: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    const char* sql_prune =
        "DELETE FROM messages WHERE chat_id = ? AND message_id NOT IN ("
        "    SELECT message_id FROM messages WHERE chat_id = ? ORDER BY message_id DESC LIMIT ?"
        ");";

    if (sqlite3_prepare_v2(db_, sql_prune, -1, &stmt_prune_, nullptr) != SQLITE_OK) {
        std::cerr << "[Database] Failed to prepare prune stmt: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    return true;
}

void Database::finalize_statements() {
    if (stmt_insert_) { sqlite3_finalize(stmt_insert_); stmt_insert_ = nullptr; }
    if (stmt_query_last_) { sqlite3_finalize(stmt_query_last_); stmt_query_last_ = nullptr; }
    if (stmt_count_) { sqlite3_finalize(stmt_count_); stmt_count_ = nullptr; }
    if (stmt_prune_) { sqlite3_finalize(stmt_prune_); stmt_prune_ = nullptr; }
}

void Database::close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    finalize_statements();
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::save_message(const ChatMessage& msg) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_ || !stmt_insert_) return false;

    sqlite3_reset(stmt_insert_);
    sqlite3_clear_bindings(stmt_insert_);

    sqlite3_bind_int64(stmt_insert_, 1, msg.chat_id);
    sqlite3_bind_int64(stmt_insert_, 2, msg.message_id);
    sqlite3_bind_int64(stmt_insert_, 3, msg.user_id);
    sqlite3_bind_text(stmt_insert_, 4, msg.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt_insert_, 5, msg.first_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt_insert_, 6, msg.timestamp);
    sqlite3_bind_int64(stmt_insert_, 7, msg.reply_to_message_id);
    sqlite3_bind_text(stmt_insert_, 8, msg.reply_to_user.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt_insert_, 9, msg.text.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt_insert_);
    if (rc != SQLITE_DONE) {
        std::cerr << "[Database] Insert failed: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return true;
}

std::vector<ChatMessage> Database::get_last_messages(int64_t chat_id, int64_t limit) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<ChatMessage> results;
    if (!db_ || !stmt_query_last_ || limit <= 0) return results;

    results.reserve(static_cast<size_t>(limit));

    sqlite3_reset(stmt_query_last_);
    sqlite3_clear_bindings(stmt_query_last_);

    sqlite3_bind_int64(stmt_query_last_, 1, chat_id);
    sqlite3_bind_int64(stmt_query_last_, 2, limit);

    while (sqlite3_step(stmt_query_last_) == SQLITE_ROW) {
        ChatMessage msg;
        msg.id = sqlite3_column_int64(stmt_query_last_, 0);
        msg.chat_id = sqlite3_column_int64(stmt_query_last_, 1);
        msg.message_id = sqlite3_column_int64(stmt_query_last_, 2);
        msg.user_id = sqlite3_column_int64(stmt_query_last_, 3);

        const unsigned char* u = sqlite3_column_text(stmt_query_last_, 4);
        if (u) msg.username = reinterpret_cast<const char*>(u);

        const unsigned char* fn = sqlite3_column_text(stmt_query_last_, 5);
        if (fn) msg.first_name = reinterpret_cast<const char*>(fn);

        msg.timestamp = sqlite3_column_int64(stmt_query_last_, 6);
        msg.reply_to_message_id = sqlite3_column_int64(stmt_query_last_, 7);

        const unsigned char* ru = sqlite3_column_text(stmt_query_last_, 8);
        if (ru) msg.reply_to_user = reinterpret_cast<const char*>(ru);

        const unsigned char* tx = sqlite3_column_text(stmt_query_last_, 9);
        if (tx) msg.text = reinterpret_cast<const char*>(tx);

        results.push_back(std::move(msg));
    }

    // Reverse to chronological order (oldest to newest)
    std::reverse(results.begin(), results.end());
    return results;
}

int64_t Database::count_messages(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_ || !stmt_count_) return 0;

    sqlite3_reset(stmt_count_);
    sqlite3_clear_bindings(stmt_count_);
    sqlite3_bind_int64(stmt_count_, 1, chat_id);

    if (sqlite3_step(stmt_count_) == SQLITE_ROW) {
        return sqlite3_column_int64(stmt_count_, 0);
    }
    return 0;
}

void Database::prune_chat_history(int64_t chat_id, int64_t keep_count) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_ || !stmt_prune_ || keep_count <= 0) return;

    sqlite3_reset(stmt_prune_);
    sqlite3_clear_bindings(stmt_prune_);

    sqlite3_bind_int64(stmt_prune_, 1, chat_id);
    sqlite3_bind_int64(stmt_prune_, 2, chat_id);
    sqlite3_bind_int64(stmt_prune_, 3, keep_count);

    int rc = sqlite3_step(stmt_prune_);
    if (rc != SQLITE_DONE) {
        std::cerr << "[Database] Prune error: " << sqlite3_errmsg(db_) << std::endl;
    }
}

} // namespace summarybot
