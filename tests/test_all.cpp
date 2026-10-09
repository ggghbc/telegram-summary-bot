#include <cassert>
#include <iostream>
#include <filesystem>
#include "db.hpp"
#include "telegram_bot.hpp"
#include "summary_generator.hpp"
#include "config.hpp"
#include "llm_client.hpp"

using namespace summarybot;

void test_database() {
    std::cout << "[Test] Running test_database..." << std::endl;
    std::string test_db = "test_messages.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    Database db;
    assert(db.init(test_db));

    int64_t chat_id = -1001234567;
    // Insert 2000 messages
    for (int i = 1; i <= 2000; ++i) {
        ChatMessage msg;
        msg.chat_id = chat_id;
        msg.message_id = i;
        msg.user_id = 100 + (i % 5);
        msg.username = "user" + std::to_string(i % 5);
        msg.first_name = "User " + std::to_string(i % 5);
        msg.timestamp = 1700000000 + i * 10;
        msg.text = "Message number " + std::to_string(i);
        if (i % 3 == 0) {
            msg.reply_to_message_id = i - 1;
            msg.reply_to_user = "User " + std::to_string((i - 1) % 5);
        }
        assert(db.save_message(msg));
    }

    // Verify count
    int64_t total = db.count_messages(chat_id);
    assert(total == 2000);
    (void)total;

    // Fetch last 500 messages
    auto last_500 = db.get_last_messages(chat_id, 500);
    assert(last_500.size() == 500);
    // Chronological order: first message should be 1501, last should be 2000
    assert(last_500.front().message_id == 1501);
    assert(last_500.back().message_id == 2000);

    // Fetch last 1500 messages (maximum allowed limit)
    auto last_1500 = db.get_last_messages(chat_id, 1500);
    assert(last_1500.size() == 1500);
    assert(last_1500.front().message_id == 501);
    assert(last_1500.back().message_id == 2000);

    // Test pruning: keep only 1000 messages
    db.prune_chat_history(chat_id, 1000);
    int64_t after_prune = db.count_messages(chat_id);
    assert(after_prune == 1000);
    (void)after_prune;

    auto remaining = db.get_last_messages(chat_id, 2000);
    assert(remaining.size() == 1000);
    assert(remaining.front().message_id == 1001);
    assert(remaining.back().message_id == 2000);

    db.close();
    std::filesystem::remove(test_db);
    std::cout << "[Test] test_database PASSED!" << std::endl;
}

void test_message_splitter() {
    std::cout << "[Test] Running test_message_splitter..." << std::endl;
    std::string short_text = "Short message";
    auto chunks = TelegramBot::split_message(short_text, 100);
    assert(chunks.size() == 1);
    assert(chunks[0] == short_text);

    std::string long_text;
    for (int i = 0; i < 50; ++i) {
        long_text += "Paragraph " + std::to_string(i) + " with some detailed discussion text.\n\n";
    }
    auto split_chunks = TelegramBot::split_message(long_text, 200);
    assert(split_chunks.size() > 1);
    for (const auto& chunk : split_chunks) {
        assert(chunk.size() <= 200);
        (void)chunk;
    }
    std::cout << "[Test] test_message_splitter PASSED!" << std::endl;
}

void test_transcript_formatting() {
    std::cout << "[Test] Running test_transcript_formatting..." << std::endl;
    Config cfg;
    LlmClient dummy_llm("dummy_key", "http://localhost", "dummy_model");
    SummaryGenerator gen(dummy_llm, cfg);

    std::vector<ChatMessage> msgs;
    ChatMessage m1;
    m1.message_id = 1;
    m1.user_id = 10;
    m1.first_name = "Alice";
    m1.username = "alice_dev";
    m1.timestamp = 1700000000;
    m1.text = "Hello team, let's discuss release 2.0.";
    msgs.push_back(m1);

    ChatMessage m2;
    m2.message_id = 2;
    m2.user_id = 20;
    m2.first_name = "Bob";
    m2.timestamp = 1700000060;
    m2.reply_to_user = "Alice";
    m2.text = "I finished the tests, looks ready.";
    msgs.push_back(m2);

    std::string transcript = gen.build_transcript(msgs);
    assert(transcript.find("Alice (@alice_dev)") != std::string::npos);
    assert(transcript.find("(в ответ Alice)") != std::string::npos);
    assert(transcript.find("Hello team, let's discuss release 2.0.") != std::string::npos);
    assert(transcript.find("I finished the tests, looks ready.") != std::string::npos);

    std::cout << "[Test] test_transcript_formatting PASSED!" << std::endl;
}

int main() {
    std::cout << "Running all test suites..." << std::endl;
    test_database();
    test_message_splitter();
    test_transcript_formatting();
    std::cout << "All test suites PASSED successfully!" << std::endl;
    return 0;
}
