#pragma once

#include <string>

namespace kare {

struct HttpResult {
    int status = 0;
    std::string body;
    std::string error;
};

// headers: one "Name: value" per line, separated by "\r\n".
HttpResult http_request(const std::string& method, const std::string& url, const std::string& body,
                        const std::string& headers, int timeout_ms, size_t max_bytes = 1024 * 1024);

}  // namespace kare
