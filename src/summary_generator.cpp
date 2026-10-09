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
    oss << std::put_time(&tm_buf, "%H:%M");
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
            text = text.substr(0, 495) + "[...]";
        }

        oss << "[" << time_str << "] " << sender << reply_str << ": " << text << "\n";
    }
    return oss.str();
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
                << "- Ground every statement strictly in the transcript above; do NOT invent or assume unmentioned facts or drama.\n"
                << "- Accurately follow reply chains to preserve conversational context.\n"
                << "- In Key Topics & Discussion, cite participants and use authentic direct quotes (\"...\") for all key points.\n"
                << "- If images/photos are described in the transcript (e.g. [Photo: ...]), reflect their context accurately.\n"
                << "- Write in the primary language of the conversation, strictly no emojis, no decisions section, and no open questions.";

    std::string summary = llm_client_.generate_summary(config_.system_prompt, user_prompt.str(), out_error);
    if (summary.empty()) {
        return "";
    }

    std::ostringstream final_msg;
    final_msg << "*Summary of " << requested_desc << " (" << messages.size() << " messages)*\n"
              << "Time: " << start_time << " — " << end_time << "\n";

    if (!topic_filter.empty()) {
        final_msg << "*Topic Focus:* " << topic_filter << "\n";
    }

    final_msg << "\n" << summary;
    return final_msg.str();
}

} // namespace summarybot
