#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include "config.hpp"
#include "db.hpp"
#include "telegram_bot.hpp"
#include "llm_client.hpp"
#include "summary_generator.hpp"

namespace summarybot {

enum class QueryType {
    Count,
    TimeWindow
};

struct SummaryRequest {
    QueryType type = QueryType::Count;
    int64_t count = 100;
    int64_t since_timestamp = 0;
    std::string window_desc;
    std::string topic_filter;
};

/**
 * @brief Main bot application controller.
 * Handles update polling, database storage, command dispatching,
 * rate limiting, timezone management, and asynchronous summary generation.
 */
class BotApp {
public:
    explicit BotApp(Config config);
    ~BotApp();

    /**
     * @brief Initialize database, Telegram client, and LLM connections.
     */
    bool init();

    /**
     * @brief Run the main polling loop. Blocks until stop() is called.
     */
    void run();

    /**
     * @brief Signal the bot to gracefully stop running.
     */
    void stop();

private:
    Config config_;
    Database db_;
    std::unique_ptr<TelegramBot> bot_;
    std::unique_ptr<LlmClient> llm_;
    std::unique_ptr<SummaryGenerator> generator_;
    std::atomic<bool> running_{false};
    int64_t message_counter_ = 0;

    // Rate limiter: (chat_id, thread_id) -> last_summary_timestamp
    std::map<std::pair<int64_t, int64_t>, int64_t> last_summary_time_;
    std::mutex rate_limit_mutex_;

    void process_update(const TelegramUpdate& update);
    void handle_message(const TelegramMessage& msg);

    bool parse_bot_invocation(
        const TelegramMessage& msg,
        const ChatSettings& settings,
        SummaryRequest& out_req,
        bool& out_is_help,
        bool& out_is_tz_cmd,
        bool& out_is_start
    ) const;

    void handle_timezone_cmd(const TelegramMessage& msg);

    void execute_summary_async(
        int64_t chat_id,
        int64_t thread_id,
        int64_t request_msg_id,
        const SummaryRequest& req
    );

    void analyze_and_update_image_async(
        int64_t chat_id,
        int64_t message_id,
        const std::string& file_id,
        const std::string& caption,
        bool is_sticker = false
    );

    void transcribe_and_update_audio_async(
        int64_t chat_id,
        int64_t message_id,
        const std::string& file_id,
        const std::string& media_type,
        const std::string& caption = ""
    );

    void send_start(int64_t chat_id, int64_t thread_id, int64_t reply_to_id);
    void send_help(int64_t chat_id, int64_t thread_id, int64_t reply_to_id);
};

} // namespace summarybot
