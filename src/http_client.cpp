#include "http_client.hpp"
#include <iostream>

namespace summarybot {

namespace {

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto* mem = static_cast<std::string*>(userp);
    mem->append(static_cast<char*>(contents), total_size);
    return total_size;
}

} // namespace

void HttpClient::global_init() {
    curl_global_init(CURL_GLOBAL_ALL);
}

void HttpClient::global_cleanup() {
    curl_global_cleanup();
}

HttpClient::HttpClient() {
    curl_ = curl_easy_init();
}

HttpClient::~HttpClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

void HttpClient::set_proxy(const std::string& proxy_url) {
    proxy_url_ = proxy_url;
}

void HttpClient::setup_curl_common(
    CURL* handle,
    const std::string& url,
    int timeout_seconds,
    std::string& response_buffer
) {
    curl_easy_reset(handle);
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response_buffer);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);

    if (!proxy_url_.empty()) {
        curl_easy_setopt(handle, CURLOPT_PROXY, proxy_url_.c_str());
    }
}

HttpResponse HttpClient::get(
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    int timeout_seconds
) {
    HttpResponse response;
    CURL* handle = curl_ ? curl_ : curl_easy_init();
    if (!handle) {
        response.error_message = "Failed to initialize CURL handle";
        return response;
    }

    std::string response_buffer;
    setup_curl_common(handle, url, timeout_seconds, response_buffer);
    curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);

    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : headers) {
        std::string header_line = k + ": " + v;
        chunk = curl_slist_append(chunk, header_line.c_str());
    }
    if (chunk) {
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, chunk);
    }

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);

    CURLcode res = curl_easy_perform(handle);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
        response.status_code = static_cast<int>(http_code);
        response.body = std::move(response_buffer);
    } else {
        response.status_code = -1;
        response.error_message = errbuf[0] != '\0' ? std::string(errbuf) : curl_easy_strerror(res);
    }

    if (chunk) {
        curl_slist_free_all(chunk);
    }
    if (!curl_) {
        curl_easy_cleanup(handle);
    }

    return response;
}

HttpResponse HttpClient::post_json(
    const std::string& url,
    const std::string& json_payload,
    const std::map<std::string, std::string>& headers,
    int timeout_seconds
) {
    HttpResponse response;
    CURL* handle = curl_ ? curl_ : curl_easy_init();
    if (!handle) {
        response.error_message = "Failed to initialize CURL handle";
        return response;
    }

    std::string response_buffer;
    setup_curl_common(handle, url, timeout_seconds, response_buffer);
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json_payload.c_str());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_payload.size()));

    struct curl_slist* chunk = nullptr;
    chunk = curl_slist_append(chunk, "Content-Type: application/json");
    for (const auto& [k, v] : headers) {
        std::string header_line = k + ": " + v;
        chunk = curl_slist_append(chunk, header_line.c_str());
    }
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, chunk);

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);

    CURLcode res = curl_easy_perform(handle);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
        response.status_code = static_cast<int>(http_code);
        response.body = std::move(response_buffer);
    } else {
        response.status_code = -1;
        response.error_message = errbuf[0] != '\0' ? std::string(errbuf) : curl_easy_strerror(res);
    }

    if (chunk) {
        curl_slist_free_all(chunk);
    }
    if (!curl_) {
        curl_easy_cleanup(handle);
    }

    return response;
}

} // namespace summarybot
