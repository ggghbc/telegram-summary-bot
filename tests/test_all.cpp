#include <cassert>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>
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
    // Insert 2000 messages in general thread (0)
    for (int i = 1; i <= 2000; ++i) {
        ChatMessage msg;
        msg.chat_id = chat_id;
        msg.thread_id = 0;
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

    // Insert 10 messages in topic thread (42)
    for (int i = 1; i <= 10; ++i) {
        ChatMessage msg;
        msg.chat_id = chat_id;
        msg.thread_id = 42;
        msg.message_id = 3000 + i;
        msg.user_id = 999;
        msg.first_name = "TopicUser";
        msg.timestamp = 1700025000 + i * 10;
        msg.text = "Topic message " + std::to_string(i);
        assert(db.save_message(msg));
    }

    // Verify count in general thread
    int64_t total = db.count_messages(chat_id, 0);
    assert(total == 2000);
    (void)total;

    // Verify count in topic thread 42
    int64_t topic_total = db.count_messages(chat_id, 42);
    assert(topic_total == 10);
    (void)topic_total;

    // Fetch last 500 messages
    auto last_500 = db.get_last_messages(chat_id, 0, 500);
    assert(last_500.size() == 500);
    assert(last_500.front().message_id == 1501);
    assert(last_500.back().message_id == 2000);

    // Fetch messages since timestamp
    int64_t since_ts = 1700000000 + 1900 * 10;
    auto since_msgs = db.get_messages_since(chat_id, 0, since_ts, 500);
    assert(since_msgs.size() == 101); // from 1900 to 2000 inclusive

    // Test timezone settings
    assert(db.set_chat_timezone(chat_id, 4, "SAMT"));
    auto settings = db.get_chat_settings(chat_id);
    assert(settings.timezone_offset == 4);
    assert(settings.timezone_name == "SAMT");

    // Test pruning: keep only 1000 messages in general thread
    db.prune_chat_history(chat_id, 0, 1000);
    int64_t after_prune = db.count_messages(chat_id, 0);
    assert(after_prune == 1000);
    (void)after_prune;
    // Test update_message_text (for image analysis enrichment)
    assert(db.update_message_text(chat_id, 2000, "Updated photo message text"));
    auto updated_msgs = db.get_last_messages(chat_id, 0, 1);
    assert(!updated_msgs.empty() && updated_msgs.back().text == "Updated photo message text");

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

    // Test UTF-8 safety with Cyrillic text
    std::string cyrillic_text;
    for (int i = 0; i < 50; ++i) {
        cyrillic_text += "Параграф " + std::to_string(i) + " с подробным текстом на русском языке.\n\n";
    }
    auto split_chunks = TelegramBot::split_message(cyrillic_text, 200);
    assert(split_chunks.size() > 1);
    for (const auto& chunk : split_chunks) {
        assert(chunk.size() <= 200);
        // Ensure first byte of next chunk is not an orphan continuation byte
        assert((static_cast<unsigned char>(chunk.front()) & 0xC0) != 0x80);
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

    std::string transcript = gen.build_transcript(msgs, 3);
    assert(transcript.find("Alice") != std::string::npos);
    assert(transcript.find("(replying to Alice)") != std::string::npos);
    assert(transcript.find("Hello team, let's discuss release 2.0.") != std::string::npos);
    assert(transcript.find("I finished the tests, looks ready.") != std::string::npos);

    // Test timestamp format: dd-mm-yyyy HH:MM MSK
    std::string formatted_ts = SummaryGenerator::format_timestamp(1700000000, 3, "MSK");
    assert(formatted_ts.find("-") != std::string::npos);
    assert(formatted_ts.find("MSK") != std::string::npos);

    std::cout << "[Test] test_transcript_formatting PASSED!" << std::endl;
}

void test_utf8_truncation_and_json_safety() {
    std::cout << "[Test] Running test_utf8_truncation_and_json_safety..." << std::endl;

    // Test Cyrillic string where each char is 2 bytes
    std::string cyrillic = "Привет мир! Это проверка длинного текста.";
    for (size_t max_len = 1; max_len <= cyrillic.size() + 5; ++max_len) {
        std::string truncated = SummaryGenerator::utf8_safe_truncate(cyrillic, max_len);
        assert(truncated.size() <= max_len);
        // Ensure serialization into JSON does not throw
        nlohmann::json j = {{"text", truncated}};
        std::string dumped = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        assert(!dumped.empty());
    }

    // Test with emoji (4 bytes: 🐶 = 0xF0 0x9F 0x90 0xB6)
    std::string emoji_str = "Тест 🐶🐶🐶 проверка";
    for (size_t max_len = 1; max_len <= emoji_str.size() + 5; ++max_len) {
        std::string truncated = SummaryGenerator::utf8_safe_truncate(emoji_str, max_len);
        assert(truncated.size() <= max_len);
        nlohmann::json j = {{"text", truncated}};
        std::string dumped = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        assert(!dumped.empty());
    }

    // Test with an intentionally corrupted UTF-8 string:
    // e.g. 0xD0 followed by 0x2E (period '.') which is the exact scenario that triggered error 316
    std::string broken = "Invalid: \xD0\x2E test \xD1\x5B end";
    nlohmann::json j_broken = {{"text", broken}};
    // Under strict dump, this would throw type_error.316; with replace, it must succeed safely!
    std::string dumped_broken = j_broken.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    assert(!dumped_broken.empty());
    assert(dumped_broken.find("Invalid:") != std::string::npos);

    std::cout << "[Test] test_utf8_truncation_and_json_safety PASSED!" << std::endl;
}

void test_language_localization() {
    std::cout << "[Test] Running test_language_localization..." << std::endl;

    // 1. Language detection test
    std::vector<ChatMessage> ru_msgs;
    ChatMessage m1;
    m1.text = "Привет всем, как дела с релизом?";
    ru_msgs.push_back(m1);
    assert(SummaryGenerator::detect_language(ru_msgs) == SummaryGenerator::PrimaryLanguage::Russian);

    std::vector<ChatMessage> en_msgs;
    ChatMessage m2;
    m2.text = "Hello everyone, how is release going?";
    en_msgs.push_back(m2);
    assert(SummaryGenerator::detect_language(en_msgs) == SummaryGenerator::PrimaryLanguage::English);

    // 2. Scope localization test
    assert(SummaryGenerator::localize_scope_desc("the last 80 messages", SummaryGenerator::PrimaryLanguage::Russian) == "последние 80 сообщений");
    assert(SummaryGenerator::localize_scope_desc("the last 24 hours", SummaryGenerator::PrimaryLanguage::Russian) == "последние 24 ч");
    assert(SummaryGenerator::localize_scope_desc("today", SummaryGenerator::PrimaryLanguage::Russian) == "сегодня");
    assert(SummaryGenerator::localize_scope_desc("yesterday and today", SummaryGenerator::PrimaryLanguage::Russian) == "вчера и сегодня");
    assert(SummaryGenerator::localize_scope_desc("the last 80 messages", SummaryGenerator::PrimaryLanguage::English) == "the last 80 messages");

    // 3. Header normalization test
    std::string sample = "*Summary:*\nSome summary\n\n*Key Topics & Discussion:*\n- Topic 1";
    SummaryGenerator::normalize_summary_headers(sample, SummaryGenerator::PrimaryLanguage::Russian);
    assert(sample.find("*Сводка:*") != std::string::npos);
    assert(sample.find("*Ключевые темы и обсуждение:*") != std::string::npos);
    assert(sample.find("*Summary:*") == std::string::npos);
    assert(sample.find("*Key Topics & Discussion:*") == std::string::npos);

    std::cout << "[Test] test_language_localization PASSED!" << std::endl;
}

void test_voice_transcription_config_and_pipeline() {
    std::cout << "[Test] Running test_voice_transcription_config_and_pipeline..." << std::endl;

    // 1. Config defaults
    Config cfg;
    assert(cfg.enable_voice_transcription == true);
    assert(cfg.system_prompt.find("VOICE & VIDEO NOTE TRANSCRIPTIONS") != std::string::npos);

    // 2. Database update of voice & video note messages
    Database db;
    std::string test_db = "test_voice.db";
    std::remove(test_db.c_str());
    assert(db.init(test_db));

    ChatMessage vm;
    vm.chat_id = -100999;
    vm.message_id = 501;
    vm.user_id = 42;
    vm.first_name = "Alice";
    vm.timestamp = 1700000000;
    vm.text = "[Voice message]";
    assert(db.save_message(vm));

    // Verify initial message
    auto msgs = db.get_last_messages(-100999, 0, 10);
    assert(msgs.size() == 1);
    assert(msgs[0].text == "[Voice message]");

    // Update with transcription
    assert(db.update_message_text(-100999, 501, "[Voice message: \"Привет, созвон переносится на 15:00\"]"));
    msgs = db.get_last_messages(-100999, 0, 10);
    assert(msgs.size() == 1);
    assert(msgs[0].text == "[Voice message: \"Привет, созвон переносится на 15:00\"]");

    std::remove(test_db.c_str());
    std::cout << "[Test] test_voice_transcription_config_and_pipeline PASSED!" << std::endl;
}

int main() {
    std::cout << "Running all test suites..." << std::endl;
    test_database();
    test_message_splitter();
    test_transcript_formatting();
    test_utf8_truncation_and_json_safety();
    test_language_localization();
    test_voice_transcription_config_and_pipeline();
    std::cout << "All test suites PASSED successfully!" << std::endl;
    return 0;
}
