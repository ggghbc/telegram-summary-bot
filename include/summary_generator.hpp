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

    /**
     * @brief Truncate a UTF-8 string to at most max_bytes without splitting multi-byte sequences.
     */
    static std::string utf8_safe_truncate(const std::string& str, size_t max_bytes);

    enum class PrimaryLanguage {
        Russian,
        English
    };

    /**
     * @brief Detect whether the conversation is primarily Russian or English/other based on character scripts.
     */
    static PrimaryLanguage detect_language(const std::vector<ChatMessage>& messages);

    /**
     * @brief Localize requested scope description according to language.
     */
    static std::string localize_scope_desc(const std::string& desc, PrimaryLanguage lang);

    /**
     * @brief Normalize summary section headings to the conversation language.
     */
    static void normalize_summary_headers(std::string& text, PrimaryLanguage lang);

private:
    LlmClient& llm_client_;
    const Config& config_;
};

} // namespace summarybot
