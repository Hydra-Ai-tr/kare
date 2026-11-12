#include "core/http.h"

#include "core/utf8.h"

#ifndef _WIN32

namespace kare {

HttpResult http_request(const std::string&, const std::string&, const std::string&, const std::string&, int, size_t) {
    HttpResult r;
    r.error = "HTTP is available on Windows in this version";
    return r;
}

}  // namespace kare

#else

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace kare {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

}  // namespace

HttpResult http_request(const std::string& method, const std::string& url, const std::string& body,
                        const std::string& headers, int timeout_ms, size_t max_bytes) {
    HttpResult r;
    Handle ses;
    std::wstring agent = widen(std::string("Kare/") + KARE_VERSION + " (https://github.com/Hydra-Ai-tr/kare)");
    ses.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses.h) {
        r.error = "WinHttpOpen failed";
        return r;
    }
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[300]{};
    wchar_t path[4096]{};
    wchar_t extra[2048]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 300;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "bad URL";
        return r;
    }
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    Handle con;
    con.h = WinHttpConnect(ses.h, host, uc.nPort, 0);
    if (!con.h) {
        r.error = "could not connect";
        return r;
    }
    std::wstring full = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    std::wstring wmethod = widen(method);
    Handle req;
    req.h = WinHttpOpenRequest(con.h, wmethod.c_str(), full.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) {
        r.error = "could not open request";
        return r;
    }
    WinHttpSetTimeouts(req.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    std::wstring wheaders = widen(headers);
    BOOL ok = WinHttpSendRequest(req.h, wheaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wheaders.c_str(),
                                 wheaders.empty() ? 0 : (DWORD)-1L,
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (!ok || !WinHttpReceiveResponse(req.h, nullptr)) {
        r.error = "request failed";
        return r;
    }
    DWORD status = 0;
    DWORD slen = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &slen, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || !avail) break;
        if (r.body.size() > max_bytes) {
            r.error = "response too large";
            break;
        }
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, buf.data(), avail, &read)) break;
        r.body.append(buf.data(), buf.data() + read);
    }
    return r;
}

}  // namespace kare

#endif
