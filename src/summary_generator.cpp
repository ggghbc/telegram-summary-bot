#include "summary_generator.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace summarybot {

SummaryGenerator::SummaryGenerator(LlmClient& llm_client, const Config& config)
    : llm_client_(llm_client), config_(config) {}

std::string SummaryGenerator::format_timestamp(int64_t timestamp) {
    if (timestamp <= 0) return "N/A";
    // Convert to Moscow Time (MSK, UTC+3)
    std::time_t t = static_cast<std::time_t>(timestamp) + 3 * 3600;
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M MSK");
    return oss.str();
}

std::string SummaryGenerator::build_transcript(const std::vector<ChatMessage>& messages) const {
    std::ostringstream oss;
    for (const auto& msg : messages) {
        std::string time_str = format_timestamp(msg.timestamp);

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
            reply_str = " (in reply to " + msg.reply_to_user + ")";
        }

        // Limit individual message length in transcript to prevent spam blowout
        std::string text = msg.text;
        if (text.size() > 1000) {
            text = text.substr(0, 997) + "...";
        }

        oss << "[" << time_str << "] " << sender << reply_str << ": " << text << "\n";
    }
    return oss.str();
}

std::string SummaryGenerator::generate(
    const std::vector<ChatMessage>& messages,
    int64_t requested_n,
    std::string& out_error
) {
    if (messages.empty()) {
        out_error = "Message history for analysis is empty.";
        return "";
    }

    std::string start_time = format_timestamp(messages.front().timestamp);
    std::string end_time = format_timestamp(messages.back().timestamp);

    std::string transcript = build_transcript(messages);

    std::string user_content =
        "Chat history containing " + std::to_string(messages.size()) +
        " messages (from " + start_time + " to " + end_time + "):\n\n" +
        "```text\n" + transcript + "```\n\n" +
        "Generate a structured, lively summary of this conversation according to the system instructions.\n"
        "Remember: output in the primary language used in the chat, strictly no emojis, and no open questions section.";

    std::string summary = llm_client_.generate_summary(config_.system_prompt, user_content, out_error);
    if (summary.empty()) {
        return "";
    }

    std::ostringstream final_msg;
    final_msg << "*Summary of the last " << messages.size() << " messages*\n"
              << "Time: " << start_time << " — " << end_time << "\n";

    if (requested_n > 1500) {
        final_msg << "_(Limit capped at 1500 messages)_\n";
    } else if (requested_n > static_cast<int64_t>(messages.size())) {
        final_msg << "_(Available history contained " << messages.size() << " of " << requested_n << " requested messages)_\n";
    }

    final_msg << "\n" << summary;
    return final_msg.str();
}

} // namespace summarybot
