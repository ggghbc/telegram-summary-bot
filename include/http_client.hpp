#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <curl/curl.h>

namespace summarybot {

/**
 * @brief Simple HTTP response wrapper.
 */
struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string error_message;

    bool is_success() const {
        return status_code >= 200 && status_code < 300;
    }
};

/**
 * @brief Thread-safe libcurl-based HTTP client supporting GET, POST, JSON payloads, and proxies.
 * Creates an independent CURL handle per request to guarantee thread safety.
 */
class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient() = default;

    // Disable copying
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /**
     * @brief Global initialization and cleanup for libcurl.
     */
    static void global_init();
    static void global_cleanup();

    /**
     * @brief Set proxy URL (e.g., "http://127.0.0.1:8080" or "socks5h://127.0.0.1:10808").
     */
    void set_proxy(const std::string& proxy_url);

    /**
     * @brief Execute an HTTP GET request (thread-safe).
     */
    HttpResponse get(
        const std::string& url,
        const std::map<std::string, std::string>& headers = {},
        int timeout_seconds = 30
    ) const;

    /**
     * @brief Execute an HTTP POST request with JSON payload (thread-safe).
     */
    HttpResponse post_json(
        const std::string& url,
        const std::string& json_payload,
        const std::map<std::string, std::string>& headers = {},
        int timeout_seconds = 60
    ) const;

private:
    std::string proxy_url_;

    void setup_curl_common(
        CURL* handle,
        const std::string& url,
        int timeout_seconds,
        std::string& response_buffer
    ) const;
};

} // namespace summarybot
