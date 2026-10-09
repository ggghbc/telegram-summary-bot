#pragma once

#include <string>
#include <optional>
#include "http_client.hpp"

namespace summarybot {

/**
 * @brief LLM API Client supporting OpenAI-compatible endpoints
 * (OpenAI, Groq, DeepSeek, OpenRouter, Ollama, Gemini OpenAI endpoint)
 * as well as Google Gemini native REST endpoints.
 */
class LlmClient {
public:
    LlmClient(
        const std::string& api_key,
        const std::string& api_url,
        const std::string& model,
        double temperature = 0.4,
        int timeout_seconds = 120,
        const std::string& proxy = ""
    );

    /**
     * @brief Request completion for a system prompt and user input.
     * @return Generated text summary or error description.
     */
    std::string generate_summary(
        const std::string& system_prompt,
        const std::string& user_content,
        std::string& out_error
    );

    /**
     * @brief Analyze an image using vision LLM capabilities.
     * @param image_bytes Raw image file binary data.
     * @param mime_type MIME type of the image (e.g. "image/jpeg", "image/png").
     * @param prompt Prompt / instruction for image analysis.
     * @param out_error Output error string if request fails.
     * @return Generated image description or analysis.
     */
    std::string describe_image(
        const std::string& image_bytes,
        const std::string& mime_type,
        const std::string& prompt,
        std::string& out_error
    );

private:
    std::string api_key_;
    std::string api_url_;
    std::string model_;
    double temperature_;
    int timeout_seconds_;
    HttpClient http_;

    bool is_gemini_native() const;
    std::string call_openai_compatible(
        const std::string& system_prompt,
        const std::string& user_content,
        std::string& out_error
    );
    std::string call_gemini_native(
        const std::string& system_prompt,
        const std::string& user_content,
        std::string& out_error
    );
};

} // namespace summarybot
