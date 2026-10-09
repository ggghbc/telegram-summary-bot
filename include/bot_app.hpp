#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include "config.hpp"
#include "db.hpp"
#include "telegram_bot.hpp"
#include "llm_client.hpp"
#include "summary_generator.hpp"

namespace summarybot {

/**
 * @brief Main bot application controller.
 * Handles update polling, database storage, command dispatching,
 * and asynchronous summary generation with typing indicators.
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

    void process_update(const TelegramUpdate& update);
    void handle_message(const TelegramMessage& msg);

    bool parse_bot_invocation(
        const TelegramMessage& msg,
        int64_t& out_n,
        bool& out_is_help
    ) const;

    void execute_summary_async(
        int64_t chat_id,
        int64_t request_msg_id,
        int64_t count
    );

    void send_help(int64_t chat_id, int64_t reply_to_id);
};

} // namespace summarybot
