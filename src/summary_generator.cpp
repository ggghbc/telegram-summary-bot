#include "summary_generator.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace summarybot {

SummaryGenerator::SummaryGenerator(LlmClient& llm_client, const Config& config)
    : llm_client_(llm_client), config_(config) {}

std::string SummaryGenerator::format_timestamp(int64_t timestamp, int tz_offset, const std::string& tz_name) {
    if (timestamp <= 0) return "N/A";
    std::time_t t = static_cast<std::time_t>(timestamp) + static_cast<std::time_t>(tz_offset * 3600);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    // Format: dd-mm-yyyy HH:MM TIMEZONE
    oss << std::put_time(&tm_buf, "%d-%m-%Y %H:%M ") << tz_name;
    return oss.str();
}

std::string SummaryGenerator::utf8_safe_truncate(const std::string& str, size_t max_bytes) {
    if (str.size() <= max_bytes) return str;
    size_t pos = max_bytes;

    // Walk back over UTF-8 continuation bytes (10xxxxxx)
    while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
        --pos;
    }

    if (pos < max_bytes) {
        unsigned char lead = static_cast<unsigned char>(str[pos]);
        size_t char_len = 1;
        if ((lead & 0x80) == 0) {
            char_len = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            char_len = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            char_len = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            char_len = 4;
        }

        if (pos + char_len <= max_bytes) {
            pos += char_len;
        }
    }

    return str.substr(0, pos);
}

namespace {
std::string format_time_only(int64_t timestamp, int tz_offset) {
    std::time_t t = static_cast<std::time_t>(timestamp) + static_cast<std::time_t>(tz_offset * 3600);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S");
    return oss.str();
}

std::string format_date_only(int64_t timestamp, int tz_offset) {
    std::time_t t = static_cast<std::time_t>(timestamp) + static_cast<std::time_t>(tz_offset * 3600);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%d-%m-%Y");
    return oss.str();
}
} // namespace

std::string SummaryGenerator::build_transcript(const std::vector<ChatMessage>& messages, int tz_offset) const {
    std::ostringstream oss;
    std::string current_date;

    for (const auto& msg : messages) {
        // Skip messages that are purely summary commands to save tokens
        if (msg.text.rfind("/summary", 0) == 0 || msg.text.rfind("/help", 0) == 0) {
            continue;
        }

        // Print date header only when day changes (saves massive token overhead)
        std::string msg_date = format_date_only(msg.timestamp, tz_offset);
        if (msg_date != current_date) {
            current_date = msg_date;
            oss << "--- " << current_date << " ---\n";
        }

        std::string time_str = format_time_only(msg.timestamp, tz_offset);

        // Detailed sender format (Name + @username if present)
        std::string sender;
        if (!msg.first_name.empty()) {
            sender = msg.first_name;
            if (!msg.username.empty()) {
                sender += " (@" + msg.username + ")";
            }
        } else if (!msg.username.empty()) {
            sender = "@" + msg.username;
        } else {
            sender = "User" + std::to_string(msg.user_id);
        }

        std::string reply_str;
        if (!msg.reply_to_user.empty()) {
            reply_str = " (replying to " + msg.reply_to_user + ")";
        }

        // Token saving: truncate massive text dumps (> 500 chars)
        std::string text = msg.text;
        if (text.size() > 500) {
            text = utf8_safe_truncate(text, 495) + "[...]";
        }

        oss << "[" << time_str << "] " << sender << reply_str << ": " << text << "\n";
    }
    return oss.str();
}

SummaryGenerator::PrimaryLanguage SummaryGenerator::detect_language(const std::vector<ChatMessage>& messages) {
    size_t cyrillic = 0;
    size_t latin = 0;
    for (const auto& m : messages) {
        for (size_t i = 0; i < m.text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(m.text[i]);
            if (c == 0xD0 || c == 0xD1) {
                cyrillic++;
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                latin++;
            }
        }
    }
    if (cyrillic > 0 && (cyrillic >= latin || cyrillic >= 20)) {
        return PrimaryLanguage::Russian;
    }
    return PrimaryLanguage::English;
}

std::string SummaryGenerator::localize_scope_desc(const std::string& desc, PrimaryLanguage lang) {
    if (lang != PrimaryLanguage::Russian) {
        return desc;
    }
    if (desc == "today") return "сегодня";
    if (desc == "yesterday and today") return "вчера и сегодня";

    if (desc.rfind("the last ", 0) == 0) {
        std::string rest = desc.substr(9);
        if (rest.ends_with(" messages")) {
            std::string n = rest.substr(0, rest.size() - 9);
            return "последние " + n + " сообщений";
        }
        if (rest.ends_with(" hours") || rest.ends_with(" hour")) {
            size_t idx = rest.rfind(" hour");
            std::string n = rest.substr(0, idx);
            return "последние " + n + " ч";
        }
        if (rest.ends_with(" minutes") || rest.ends_with(" minute")) {
            size_t idx = rest.rfind(" minute");
            std::string n = rest.substr(0, idx);
            return "последние " + n + " мин";
        }
        if (rest.ends_with(" days") || rest.ends_with(" day")) {
            size_t idx = rest.rfind(" day");
            std::string n = rest.substr(0, idx);
            return "последние " + n + " дн";
        }
    }
    return desc;
}

void SummaryGenerator::normalize_summary_headers(std::string& text, PrimaryLanguage lang) {
    if (lang != PrimaryLanguage::Russian) return;

    const std::vector<std::pair<std::string, std::string>> replacements = {
        {"**Key Topics & Discussion:**", "*Ключевые темы и обсуждение:*"},
        {"**Key Topics and Discussion:**", "*Ключевые темы и обсуждение:*"},
        {"*Key Topics & Discussion:*", "*Ключевые темы и обсуждение:*"},
        {"*Key Topics & Discussion*:", "*Ключевые темы и обсуждение:*"},
        {"*Key Topics and Discussion:*", "*Ключевые темы и обсуждение:*"},
        {"*Key Topics and Discussion*:", "*Ключевые темы и обсуждение:*"},
        {"**Key Topics:**", "*Ключевые темы и обсуждение:*"},
        {"*Key Topics:*", "*Ключевые темы и обсуждение:*"},
        {"*Key Topics*:", "*Ключевые темы и обсуждение:*"},
        {"Key Topics & Discussion:", "*Ключевые темы и обсуждение:*"},
        {"Key Topics and Discussion:", "*Ключевые темы и обсуждение:*"},
        {"Key Topics:", "*Ключевые темы и обсуждение:*"},
        {"**Summary:**", "*Сводка:*"},
        {"*Summary:*", "*Сводка:*"},
        {"*Summary*:", "*Сводка:*"},
        {"Summary:", "*Сводка:*"}
    };

    for (const auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
}

std::string SummaryGenerator::generate(
    const std::vector<ChatMessage>& messages,
    const std::string& requested_desc,
    const std::string& topic_filter,
    int tz_offset,
    const std::string& tz_name,
    std::string& out_error
) {
    if (messages.empty()) {
        out_error = "Message history for analysis is empty.";
        return "";
    }

    PrimaryLanguage lang = detect_language(messages);
    std::string localized_desc = localize_scope_desc(requested_desc, lang);

    std::string start_time = format_timestamp(messages.front().timestamp, tz_offset, tz_name);
    std::string end_time = format_timestamp(messages.back().timestamp, tz_offset, tz_name);

    std::string transcript = build_transcript(messages, tz_offset);

    std::ostringstream user_prompt;
    user_prompt << "Chat history containing " << messages.size() << " messages ("
                << start_time << " to " << end_time << "):\n\n"
                << "```text\n" << transcript << "```\n\n";

    if (!topic_filter.empty()) {
        user_prompt << "SPECIAL TOPIC FOCUS: The user specifically asked to focus on the topic: \""
                    << topic_filter << "\". Prioritize discussion, statements, and quotes related to this topic.\n\n";
    }

    user_prompt << "Generate a structured, strictly factual summary of this conversation according to your system instructions.\n"
                << "Crucial guidelines:\n"
                << "- STRICT CHRONOLOGY: The transcript is ordered from oldest to newest (top to bottom) with exact timestamps [HH:MM:SS]. Follow the exact sequence of events. Never swap or invert the timeline (e.g. do NOT use 'в завершение' / 'in conclusion' for messages or stickers that took place before later exchanges).\n"
                << "- Ground every statement strictly in the transcript above; do NOT invent or assume unmentioned facts or drama.\n"
                << "- Accurately follow reply chains to preserve conversational context.\n"
                << "- In Key Topics & Discussion, cite participants and use authentic direct quotes (\"...\") in their true chronological order.\n"
                << "- If images/photos/stickers are described in the transcript (e.g. [Photo: ...], [Sticker: ...]), place them at their exact chronological moment in the discussion. Focus strictly on their visual content and drawing, NOT merely on emoji.\n"
                << "- Filter routine noise: ignore superficial greetings, routine confirmations ('ок', 'плюс'), and non-substantive filler.\n"
                << "- Contextual links: when links are shared, explain what was discussed regarding the link based on participants' comments.\n"
                << "- Participant grouping: when multiple participants share the same view, group them together instead of repeating points.\n"
                << "- Formatting: highlight names in bold (*Name*), and put direct quotes in quotation marks (\"...\").\n";

    if (lang == PrimaryLanguage::Russian) {
        user_prompt << "- LANGUAGE & HEADERS: The conversation is in RUSSIAN. Output the summary entirely in Russian with Russian section headers: '*Сводка:*' and '*Ключевые темы и обсуждение:*'. Do NOT use English headers.\n";
    } else {
        user_prompt << "- LANGUAGE & HEADERS: Write in the primary language of the conversation with translated section headers (e.g. '*Summary:*' and '*Key Topics & Discussion:*' for English).\n";
    }

    user_prompt << "- Strictly no emojis, no decisions section, and no open questions.";

    std::string summary = llm_client_.generate_summary(config_.system_prompt, user_prompt.str(), out_error);
    if (summary.empty()) {
        return "";
    }

    normalize_summary_headers(summary, lang);

    std::ostringstream final_msg;
    if (lang == PrimaryLanguage::Russian) {
        final_msg << "*Сводка за " << localized_desc << " (" << messages.size() << " сообщений)*\n"
                  << "Время: " << start_time << " — " << end_time << "\n";
        if (!topic_filter.empty()) {
            final_msg << "*Фокус на теме:* " << topic_filter << "\n";
        }
    } else {
        final_msg << "*Summary of " << requested_desc << " (" << messages.size() << " messages)*\n"
                  << "Time: " << start_time << " — " << end_time << "\n";
        if (!topic_filter.empty()) {
            final_msg << "*Topic Focus:* " << topic_filter << "\n";
        }
    }

    final_msg << "\n" << summary;
    return final_msg.str();
}

} // namespace summarybot
