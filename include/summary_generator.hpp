#pragma once

#include <string>
#include <vector>
#include "db.hpp"
#include "llm_client.hpp"
#include "config.hpp"

namespace summarybot {

/**
 * @brief Handles transcript formatting, prompt engineering, and summary generation.
 */
class SummaryGenerator {
public:
    SummaryGenerator(LlmClient& llm_client, const Config& config);

    /**
     * @brief Build a clean transcript from a list of messages.
     */
    std::string build_transcript(const std::vector<ChatMessage>& messages) const;

    /**
     * @brief Generate a summary for the given list of messages.
     * @param messages Vector of messages in chronological order.
     * @param requested_n The original number of messages requested by the user.
     * @param out_error Output error string if generation fails.
     * @return Formatted summary markdown text, or empty string on failure.
     */
    std::string generate(
        const std::vector<ChatMessage>& messages,
        int64_t requested_n,
        std::string& out_error
    );

private:
    LlmClient& llm_client_;
    const Config& config_;

    static std::string format_timestamp(int64_t timestamp);
};

} // namespace summarybot
