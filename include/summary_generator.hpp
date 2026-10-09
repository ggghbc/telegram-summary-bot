#pragma once

#include <string>
#include <vector>
#include "db.hpp"
#include "llm_client.hpp"
#include "config.hpp"

namespace summarybot {

/**
 * @brief Handles token-optimized transcript formatting, prompt engineering, and summary generation.
 */
class SummaryGenerator {
public:
    SummaryGenerator(LlmClient& llm_client, const Config& config);

    /**
     * @brief Build a compact, token-optimized transcript from a list of messages.
     */
    std::string build_transcript(
        const std::vector<ChatMessage>& messages,
        int tz_offset = 3
    ) const;

    /**
     * @brief Generate a summary for the given list of messages.
     * @param messages Vector of messages in chronological order.
     * @param requested_desc Description of the request (e.g. "500 messages" or "24 hours").
     * @param topic_filter Optional topic focus string.
     * @param tz_offset Timezone offset in hours (default: 3).
     * @param tz_name Timezone abbreviation (default: "MSK").
     * @param out_error Output error string if generation fails.
     * @return Formatted summary markdown text, or empty string on failure.
     */
    std::string generate(
        const std::vector<ChatMessage>& messages,
        const std::string& requested_desc,
        const std::string& topic_filter,
        int tz_offset,
        const std::string& tz_name,
        std::string& out_error
    );

    /**
     * @brief Format a timestamp into dd-mm-yyyy HH:MM TIMEZONE.
     */
    static std::string format_timestamp(int64_t timestamp, int tz_offset = 3, const std::string& tz_name = "MSK");

private:
    LlmClient& llm_client_;
    const Config& config_;
};

} // namespace summarybot
