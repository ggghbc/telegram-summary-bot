#include "llm_client.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace summarybot {

using json = nlohmann::json;

LlmClient::LlmClient(
    const std::string& api_key,
    const std::string& api_url,
    const std::string& model,
    double temperature,
    int timeout_seconds,
    const std::string& proxy
) : api_key_(api_key),
    api_url_(api_url),
    model_(model),
    temperature_(temperature),
    timeout_seconds_(timeout_seconds) {
    if (!proxy.empty()) {
        http_.set_proxy(proxy);
    }
}

bool LlmClient::is_gemini_native() const {
    return (api_url_.find("generativelanguage.googleapis.com") != std::string::npos &&
            api_url_.find("openai") == std::string::npos);
}

std::string LlmClient::generate_summary(
    const std::string& system_prompt,
    const std::string& user_content,
    std::string& out_error
) {
    if (is_gemini_native()) {
        return call_gemini_native(system_prompt, user_content, out_error);
    } else {
        return call_openai_compatible(system_prompt, user_content, out_error);
    }
}

std::string LlmClient::call_openai_compatible(
    const std::string& system_prompt,
    const std::string& user_content,
    std::string& out_error
) {
    json payload = {
        {"model", model_},
        {"messages", {
            {{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"}, {"content", user_content}}
        }},
        {"temperature", temperature_}
    };

    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + api_key_}
    };

    auto res = http_.post_json(api_url_, payload.dump(), headers, timeout_seconds_);
    if (!res.is_success()) {
        std::string err_desc;
        try {
            json err_json = json::parse(res.body);
            if (err_json.contains("error") && err_json["error"].contains("message")) {
                err_desc = err_json["error"]["message"].get<std::string>();
            }
        } catch (...) {}

        if (err_desc.empty()) {
            err_desc = res.error_message.empty() ? res.body : res.error_message;
        }

        out_error = "LLM API Error (HTTP " + std::to_string(res.status_code) + "): " + err_desc;
        return "";
    }

    try {
        json j = json::parse(res.body);
        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
            const auto& first = j["choices"][0];
            if (first.contains("message") && first["message"].contains("content")) {
                return first["message"]["content"].get<std::string>();
            }
        }
        out_error = "Malformed LLM response: missing choices[0].message.content";
    } catch (const std::exception& e) {
        out_error = std::string("JSON parsing error on LLM response: ") + e.what();
    }

    return "";
}

std::string LlmClient::call_gemini_native(
    const std::string& system_prompt,
    const std::string& user_content,
    std::string& out_error
) {
    std::string url = api_url_;
    // If URL doesn't contain ?key=, append it
    if (url.find("?key=") == std::string::npos && url.find("&key=") == std::string::npos) {
        url += (url.find('?') == std::string::npos ? "?key=" : "&key=") + api_key_;
    }

    json payload = {
        {"contents", {
            {
                {"role", "user"},
                {"parts", {{{"text", user_content}}}}
            }
        }},
        {"systemInstruction", {
            {"parts", {{{"text", system_prompt}}}}
        }},
        {"generationConfig", {
            {"temperature", temperature_}
        }}
    };

    auto res = http_.post_json(url, payload.dump(), {}, timeout_seconds_);
    if (!res.is_success()) {
        out_error = "Gemini API Error (HTTP " + std::to_string(res.status_code) + "): " +
                    (res.error_message.empty() ? res.body : res.error_message);
        return "";
    }

    try {
        json j = json::parse(res.body);
        if (j.contains("candidates") && j["candidates"].is_array() && !j["candidates"].empty()) {
            const auto& candidate = j["candidates"][0];
            if (candidate.contains("content") && candidate["content"].contains("parts")) {
                const auto& parts = candidate["content"]["parts"];
                if (parts.is_array() && !parts.empty() && parts[0].contains("text")) {
                    return parts[0]["text"].get<std::string>();
                }
            }
        }
        out_error = "Malformed Gemini response: missing candidates[0].content.parts[0].text";
    } catch (const std::exception& e) {
        out_error = std::string("JSON parsing error on Gemini response: ") + e.what();
    }

    return "";
}

} // namespace summarybot
