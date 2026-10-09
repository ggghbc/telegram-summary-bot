#include "telegram_bot.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>

namespace summarybot {

using json = nlohmann::json;

namespace {

TelegramUser parse_user(const json& j) {
    TelegramUser u;
    if (j.contains("id") && j["id"].is_number()) u.id = j["id"].get<int64_t>();
    if (j.contains("is_bot") && j["is_bot"].is_boolean()) u.is_bot = j["is_bot"].get<bool>();
    if (j.contains("first_name") && j["first_name"].is_string()) u.first_name = j["first_name"].get<std::string>();
    if (j.contains("last_name") && j["last_name"].is_string()) u.last_name = j["last_name"].get<std::string>();
    if (j.contains("username") && j["username"].is_string()) u.username = j["username"].get<std::string>();
    return u;
}

TelegramChat parse_chat(const json& j) {
    TelegramChat c;
    if (j.contains("id") && j["id"].is_number()) c.id = j["id"].get<int64_t>();
    if (j.contains("type") && j["type"].is_string()) c.type = j["type"].get<std::string>();
    if (j.contains("title") && j["title"].is_string()) c.title = j["title"].get<std::string>();
    return c;
}

TelegramMessage parse_message(const json& j) {
    TelegramMessage m;
    if (j.contains("message_id") && j["message_id"].is_number()) m.message_id = j["message_id"].get<int64_t>();
    if (j.contains("date") && j["date"].is_number()) m.date = j["date"].get<int64_t>();
    if (j.contains("from") && j["from"].is_object()) m.from = parse_user(j["from"]);
    if (j.contains("chat") && j["chat"].is_object()) m.chat = parse_chat(j["chat"]);
    if (j.contains("text") && j["text"].is_string()) m.text = j["text"].get<std::string>();
    if (j.contains("caption") && j["caption"].is_string()) m.caption = j["caption"].get<std::string>();

    if (j.contains("reply_to_message") && j["reply_to_message"].is_object()) {
        m.reply_to_message = std::make_shared<TelegramMessage>(parse_message(j["reply_to_message"]));
    }
    return m;
}

} // namespace

TelegramBot::TelegramBot(
    const std::string& token,
    const std::string& base_url,
    const std::string& proxy
) : token_(token), base_url_(base_url), proxy_(proxy) {
    if (!proxy_.empty()) {
        http_.set_proxy(proxy_);
    }
}

std::string TelegramBot::build_api_url(const std::string& method) const {
    std::string base = base_url_;
    if (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + "/bot" + token_ + "/" + method;
}

bool TelegramBot::init() {
    std::string url = build_api_url("getMe");
    auto res = http_.get(url, {}, 15);
    if (!res.is_success()) {
        std::cerr << "[TelegramBot] getMe failed with HTTP " << res.status_code 
                  << ": " << res.error_message << " | " << res.body << std::endl;
        return false;
    }

    try {
        json j = json::parse(res.body);
        if (j.value("ok", false) && j.contains("result")) {
            bot_user_ = parse_user(j["result"]);
            std::cout << "[TelegramBot] Authenticated as @" << bot_user_.username
                      << " (ID: " << bot_user_.id << ", Name: " << bot_user_.first_name << ")" << std::endl;
            return true;
        } else {
            std::cerr << "[TelegramBot] getMe returned error: " << j.value("description", "unknown error") << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[TelegramBot] JSON parse error in getMe: " << e.what() << std::endl;
    }
    return false;
}

std::vector<TelegramUpdate> TelegramBot::get_updates(int64_t offset, int timeout_seconds) {
    std::vector<TelegramUpdate> updates;
    std::string url = build_api_url("getUpdates");

    json payload = {
        {"offset", offset},
        {"timeout", timeout_seconds},
        {"allowed_updates", {"message"}}
    };

    auto res = http_.post_json(url, payload.dump(), {}, timeout_seconds + 10);
    if (!res.is_success()) {
        if (res.status_code != -1) {
            std::cerr << "[TelegramBot] getUpdates failed: " << res.status_code << " | " << res.body << std::endl;
        }
        return updates;
    }

    try {
        json j = json::parse(res.body);
        if (j.value("ok", false) && j.contains("result") && j["result"].is_array()) {
            for (const auto& item : j["result"]) {
                TelegramUpdate u;
                if (item.contains("update_id")) {
                    u.update_id = item["update_id"].get<int64_t>();
                }
                if (item.contains("message") && item["message"].is_object()) {
                    u.message = parse_message(item["message"]);
                }
                updates.push_back(std::move(u));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[TelegramBot] Error parsing getUpdates: " << e.what() << std::endl;
    }

    return updates;
}

std::vector<std::string> TelegramBot::split_message(const std::string& text, size_t max_len) {
    std::vector<std::string> chunks;
    if (text.empty()) return chunks;
    if (text.size() <= max_len) {
        chunks.push_back(text);
        return chunks;
    }

    size_t start = 0;
    while (start < text.size()) {
        size_t remaining = text.size() - start;
        if (remaining <= max_len) {
            chunks.push_back(text.substr(start));
            break;
        }

        // Search backwards from (start + max_len) for clean split point
        size_t search_end = start + max_len;
        size_t split_pos = std::string::npos;

        // Try double newline
        split_pos = text.rfind("\n\n", search_end);
        if (split_pos != std::string::npos && split_pos > start + max_len / 2) {
            split_pos += 2; // Include newlines
        } else {
            // Try single newline
            split_pos = text.rfind('\n', search_end);
            if (split_pos != std::string::npos && split_pos > start + max_len / 2) {
                split_pos += 1;
            } else {
                // Try space
                split_pos = text.rfind(' ', search_end);
                if (split_pos != std::string::npos && split_pos > start + max_len / 2) {
                    split_pos += 1;
                } else {
                    // Hard split
                    split_pos = search_end;
                }
            }
        }

        chunks.push_back(text.substr(start, split_pos - start));
        start = split_pos;
    }

    return chunks;
}

int64_t TelegramBot::send_message(
    int64_t chat_id,
    const std::string& text,
    int64_t reply_to_message_id,
    const std::string& parse_mode
) {
    std::string url = build_api_url("sendMessage");
    auto chunks = split_message(text, 3900);
    if (chunks.empty()) return 0;

    int64_t first_sent_id = 0;

    for (size_t i = 0; i < chunks.size(); ++i) {
        const auto& chunk = chunks[i];
        json payload = {
            {"chat_id", chat_id},
            {"text", chunk}
        };

        if (!parse_mode.empty()) {
            payload["parse_mode"] = parse_mode;
        }
        if (i == 0 && reply_to_message_id != 0) {
            payload["reply_to_message_id"] = reply_to_message_id;
        }

        auto res = http_.post_json(url, payload.dump(), {}, 20);

        // Fallback: If Telegram rejected due to markdown formatting syntax, retry in plain text
        if (!res.is_success() && !parse_mode.empty()) {
            std::cerr << "[TelegramBot] Markdown parse failed for sendMessage, retrying plain text..." << std::endl;
            payload.erase("parse_mode");
            res = http_.post_json(url, payload.dump(), {}, 20);
        }

        if (res.is_success()) {
            try {
                json j = json::parse(res.body);
                if (j.value("ok", false) && j.contains("result") && j["result"].contains("message_id")) {
                    int64_t mid = j["result"]["message_id"].get<int64_t>();
                    if (first_sent_id == 0) first_sent_id = mid;
                }
            } catch (...) {}
        } else {
            std::cerr << "[TelegramBot] Failed to send message: " << res.status_code << " | " << res.body << std::endl;
        }
    }

    return first_sent_id;
}

bool TelegramBot::edit_message_text(
    int64_t chat_id,
    int64_t message_id,
    const std::string& text,
    const std::string& parse_mode
) {
    std::string url = build_api_url("editMessageText");
    json payload = {
        {"chat_id", chat_id},
        {"message_id", message_id},
        {"text", text}
    };
    if (!parse_mode.empty()) {
        payload["parse_mode"] = parse_mode;
    }

    auto res = http_.post_json(url, payload.dump(), {}, 20);
    if (!res.is_success() && !parse_mode.empty()) {
        payload.erase("parse_mode");
        res = http_.post_json(url, payload.dump(), {}, 20);
    }
    return res.is_success();
}

bool TelegramBot::send_chat_action(int64_t chat_id, const std::string& action) {
    std::string url = build_api_url("sendChatAction");
    json payload = {
        {"chat_id", chat_id},
        {"action", action}
    };
    auto res = http_.post_json(url, payload.dump(), {}, 10);
    return res.is_success();
}

bool TelegramBot::delete_message(int64_t chat_id, int64_t message_id) {
    std::string url = build_api_url("deleteMessage");
    json payload = {
        {"chat_id", chat_id},
        {"message_id", message_id}
    };
    auto res = http_.post_json(url, payload.dump(), {}, 10);
    return res.is_success();
}

} // namespace summarybot
