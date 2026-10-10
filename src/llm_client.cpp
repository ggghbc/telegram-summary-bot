#include "llm_client.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <chrono>

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
    // Automatically normalize OpenAI-compatible endpoint URL:
    // If user specified a base URL (e.g. "http://.../v1" or "http://..."),
    // append "/chat/completions" so both base URLs and full endpoint URLs work seamlessly.
    if (!is_gemini_native() && !api_url_.empty()) {
        if (!api_url_.ends_with("/chat/completions")) {
            if (api_url_.ends_with("/v1/")) {
                api_url_ += "chat/completions";
            } else if (api_url_.ends_with("/v1")) {
                api_url_ += "/chat/completions";
            } else if (api_url_.back() == '/') {
                api_url_ += "v1/chat/completions";
            } else {
                api_url_ += "/v1/chat/completions";
            }
        }
    }

    // Only set proxy for remote endpoints; local endpoints (127.0.0.1, localhost) connect directly.
    bool is_local = (api_url_.find("://127.0.0.1") != std::string::npos ||
                     api_url_.find("://localhost") != std::string::npos ||
                     api_url_.find("://[::1]") != std::string::npos);
    if (!proxy.empty() && !is_local) {
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
        {"messages", {
            {{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"}, {"content", user_content}}
        }},
        {"temperature", temperature_}
    };
    if (!model_.empty()) {
        payload["model"] = model_;
    }

    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + api_key_}
    };

    HttpResponse res;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        res = http_.post_json(api_url_, payload.dump(-1, ' ', false, json::error_handler_t::replace), headers, timeout_seconds_);
        if (res.is_success()) {
            break;
        }
        if ((res.status_code == 503 || res.status_code == 429 || res.status_code == 500) && attempt < 3) {
            std::cerr << "[LlmClient] Received HTTP " << res.status_code 
                      << ", retrying attempt " << (attempt + 1) << " in 2 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        break;
    }

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
        if (j.contains("model") && j["model"].is_string()) {
            std::cout << "[LlmClient] Model: " << j["model"].get<std::string>() << std::endl;
        }
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

    auto res = http_.post_json(url, payload.dump(-1, ' ', false, json::error_handler_t::replace), {}, timeout_seconds_);
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

namespace {
std::string base64_encode(const std::string& input) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(input.data());
    size_t in_len = input.size();
    std::string ret;
    ret.reserve(((in_len + 2) / 3) * 4);

    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}
} // namespace

std::string LlmClient::describe_image(
    const std::string& image_bytes,
    const std::string& mime_type,
    const std::string& prompt,
    std::string& out_error
) {
    if (image_bytes.empty()) {
        out_error = "Image binary data is empty";
        return "";
    }

    std::string b64 = base64_encode(image_bytes);
    std::string effective_mime = mime_type.empty() ? "image/jpeg" : mime_type;

    if (is_gemini_native()) {
        std::string url = api_url_;
        if (url.find("key=") == std::string::npos) {
            url += (url.find('?') == std::string::npos ? "?key=" : "&key=") + api_key_;
        }

        json payload = {
            {"contents", {
                {
                    {"role", "user"},
                    {"parts", {
                        {{"text", prompt}},
                        {
                            {"inline_data", {
                                {"mime_type", effective_mime},
                                {"data", b64}
                            }}
                        }
                    }}
                }
            }},
            {"generationConfig", {
                {"temperature", 0.2},
                {"maxOutputTokens", 500}
            }}
        };

        HttpResponse res;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            res = http_.post_json(url, payload.dump(-1, ' ', false, json::error_handler_t::replace), {}, timeout_seconds_);
            if (res.is_success()) break;
            if ((res.status_code == 503 || res.status_code == 429) && attempt < 3) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            break;
        }

        if (!res.is_success()) {
            out_error = "Gemini Vision Error (HTTP " + std::to_string(res.status_code) + "): " +
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
            out_error = "Malformed Gemini vision response: missing content parts";
        } catch (const std::exception& e) {
            out_error = std::string("JSON parsing error on Gemini vision response: ") + e.what();
        }
        return "";
    } else {
        // OpenAI-compatible Chat Completions format with image_url
        json payload = {
            {"messages", {
                {
                    {"role", "user"},
                    {"content", json::array({
                        {{"type", "text"}, {"text", prompt}},
                        {{"type", "image_url"}, {"image_url", {
                            {"url", "data:" + effective_mime + ";base64," + b64}
                        }}}
                    })}
                }
            }},
            {"temperature", 0.2},
            {"max_tokens", 500}
        };
        if (!model_.empty()) {
            payload["model"] = model_;
        }

        std::map<std::string, std::string> headers = {
            {"Authorization", "Bearer " + api_key_}
        };

        HttpResponse res;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            res = http_.post_json(api_url_, payload.dump(-1, ' ', false, json::error_handler_t::replace), headers, timeout_seconds_);
            if (res.is_success()) break;
            if ((res.status_code == 503 || res.status_code == 429) && attempt < 3) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            break;
        }

        if (!res.is_success()) {
            out_error = "LLM Vision Error (HTTP " + std::to_string(res.status_code) + "): " +
                        (res.error_message.empty() ? res.body : res.error_message);
            return "";
        }

        try {
            json j = json::parse(res.body);
            if (j.contains("model") && j["model"].is_string()) {
                std::cout << "[LlmClient] Vision model: " << j["model"].get<std::string>() << std::endl;
            }
            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                const auto& first = j["choices"][0];
                if (first.contains("message") && first["message"].contains("content")) {
                    return first["message"]["content"].get<std::string>();
                }
            }
            out_error = "Malformed vision response: missing choices[0].message.content";
        } catch (const std::exception& e) {
            out_error = std::string("JSON parsing error on vision response: ") + e.what();
        }
        return "";
    }
}

std::string LlmClient::transcribe_audio(
    const std::string& audio_bytes,
    const std::string& mime_type,
    const std::string& prompt,
    std::string& out_error
) {
    if (audio_bytes.empty()) {
        out_error = "Audio binary data is empty";
        return "";
    }

    std::string b64 = base64_encode(audio_bytes);
    std::string effective_mime = mime_type.empty() ? "audio/ogg" : mime_type;

    if (is_gemini_native()) {
        std::string url = api_url_;
        if (url.find("key=") == std::string::npos) {
            url += (url.find('?') == std::string::npos ? "?key=" : "&key=") + api_key_;
        }

        json payload = {
            {"contents", {
                {
                    {"role", "user"},
                    {"parts", {
                        {{"text", prompt}},
                        {
                            {"inline_data", {
                                {"mime_type", effective_mime},
                                {"data", b64}
                            }}
                        }
                    }}
                }
            }},
            {"generationConfig", {
                {"temperature", 0.1},
                {"maxOutputTokens", 1000}
            }}
        };

        HttpResponse res;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            res = http_.post_json(url, payload.dump(-1, ' ', false, json::error_handler_t::replace), {}, timeout_seconds_);
            if (res.is_success()) break;
            if ((res.status_code == 503 || res.status_code == 429) && attempt < 3) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            break;
        }

        if (!res.is_success()) {
            out_error = "Gemini Audio Error (HTTP " + std::to_string(res.status_code) + "): " +
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
            out_error = "Malformed Gemini audio response: missing content parts";
        } catch (const std::exception& e) {
            out_error = std::string("JSON parsing error on Gemini audio response: ") + e.what();
        }
        return "";
    } else {
        // OpenAI-compatible Chat Completions format with data URI
        json payload = {
            {"messages", {
                {
                    {"role", "user"},
                    {"content", json::array({
                        {{"type", "text"}, {"text", prompt}},
                        {{"type", "image_url"}, {"image_url", {
                            {"url", "data:" + effective_mime + ";base64," + b64}
                        }}}
                    })}
                }
            }},
            {"temperature", 0.1},
            {"max_tokens", 1000}
        };
        if (!model_.empty()) {
            payload["model"] = model_;
        }

        std::map<std::string, std::string> headers = {
            {"Authorization", "Bearer " + api_key_}
        };

        HttpResponse res;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            res = http_.post_json(api_url_, payload.dump(-1, ' ', false, json::error_handler_t::replace), headers, timeout_seconds_);
            if (res.is_success()) break;
            if ((res.status_code == 503 || res.status_code == 429) && attempt < 3) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            break;
        }

        // If data URI failed with client error (400), attempt fallback to standard OpenAI input_audio
        if (!res.is_success() && res.status_code == 400) {
            std::string audio_fmt = "wav";
            if (effective_mime.find("mp3") != std::string::npos || effective_mime.find("mpeg") != std::string::npos) {
                audio_fmt = "mp3";
            }
            json fallback_payload = {
                {"messages", {
                    {
                        {"role", "user"},
                        {"content", json::array({
                            {{"type", "text"}, {"text", prompt}},
                            {{"type", "input_audio"}, {"input_audio", {
                                {"data", b64},
                                {"format", audio_fmt}
                            }}}
                        })}
                    }
                }},
                {"temperature", 0.1},
                {"max_tokens", 1000}
            };
            if (!model_.empty()) {
                fallback_payload["model"] = model_;
            }
            res = http_.post_json(api_url_, fallback_payload.dump(-1, ' ', false, json::error_handler_t::replace), headers, timeout_seconds_);
        }

        if (!res.is_success()) {
            out_error = "LLM Audio Error (HTTP " + std::to_string(res.status_code) + "): " +
                        (res.error_message.empty() ? res.body : res.error_message);
            return "";
        }

        try {
            json j = json::parse(res.body);
            if (j.contains("model") && j["model"].is_string()) {
                std::cout << "[LlmClient] Audio transcription model: " << j["model"].get<std::string>() << std::endl;
            }
            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                const auto& first = j["choices"][0];
                if (first.contains("message") && first["message"].contains("content")) {
                    return first["message"]["content"].get<std::string>();
                }
            }
            out_error = "Malformed audio response: missing choices[0].message.content";
        } catch (const std::exception& e) {
            out_error = std::string("JSON parsing error on audio response: ") + e.what();
        }
        return "";
    }
}

} // namespace summarybot
