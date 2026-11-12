#include "core/brain.h"

#include "core/json.h"
#include "core/story.h"
#include "core/utf8.h"

#ifndef _WIN32

namespace kare {

bool plan_with_api(const PlanInput&, Storyboard&, std::string& err) {
    err = "API planning is available on Windows in this version";
    return false;
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

struct HttpResult {
    int status = 0;
    std::string body;
    std::string error;
};

std::wstring trim_slash_copy(std::wstring s) {
    while (!s.empty() && (s.back() == L'/' || s.back() == L' ')) s.pop_back();
    return s;
}

HttpResult http_post(const std::string& url, const std::string& body, const std::wstring& headers, int timeout_ms) {
    HttpResult r;
    HINTERNET ses = WinHttpOpen(L"Kare/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) {
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
        WinHttpCloseHandle(ses);
        return r;
    }
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) {
        r.error = "could not connect";
        WinHttpCloseHandle(ses);
        return r;
    }
    std::wstring full = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    HINTERNET req = WinHttpOpenRequest(con, L"POST", full.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        r.error = "could not open request";
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return r;
    }
    WinHttpSetTimeouts(req, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    BOOL ok = WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                 headers.empty() ? 0 : (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (!ok || !WinHttpReceiveResponse(req, nullptr)) {
        r.error = "request failed";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return r;
    }
    DWORD status = 0;
    DWORD slen = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &slen, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (!avail) break;
        if (r.body.size() > 1024 * 1024) {
            r.error = "response too large";
            break;
        }
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &read)) break;
        r.body.append(buf.data(), buf.data() + read);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return r;
}

std::string endpoint_for(std::string base) {
    while (!base.empty() && (base.back() == '/' || base.back() == ' ')) base.pop_back();
    const std::string tail = "/chat/completions";
    if (base.size() >= tail.size() && base.compare(base.size() - tail.size(), tail.size(), tail) == 0) return base;
    return base + tail;
}

std::string extract_object(const std::string& text) {
    auto a = text.find('{');
    auto b = text.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b <= a) return {};
    return text.substr(a, b - a + 1);
}

}  // namespace

bool plan_with_api(const PlanInput& in, Storyboard& board, std::string& err) {
    if (in.api_base.empty()) {
        err = "API address is empty";
        return false;
    }
    if (in.api_model.empty()) {
        err = "API model is empty";
        return false;
    }
    const char* system =
        "You are the director of a local video studio. Convert the user prompt into a storyboard. "
        "Reply with JSON only, no markdown. Schema: "
        "{\"title\":\"string\",\"style\":\"ember|dusk|night|sea|forest|neon|paper|minimal\","
        "\"aspect\":\"16:9|9:16|1:1\",\"scenes\":[{\"title\":\"string\",\"text\":\"string\","
        "\"seconds\":2.5,\"motion\":\"drift|zoom|rise\"}]} "
        "Use 1 to 8 scenes, each between 1 and 12 seconds. On-screen title and text must be in the "
        "user's language, short enough to read on a frame. style and motion must be one of the listed values.";
    std::string user = in.prompt;
    if (in.seconds > 0) user += "\nTarget duration seconds: " + std::to_string(static_cast<int>(in.seconds));
    if (!in.aspect.empty()) user += "\nAspect: " + in.aspect;
    if (!in.style.empty()) user += "\nStyle: " + in.style;
    std::string payload = std::string("{\"model\":\"") + json_escape(in.api_model) +
                          "\",\"temperature\":0.4,\"messages\":[{\"role\":\"system\",\"content\":\"" +
                          json_escape(system) + "\"},{\"role\":\"user\",\"content\":\"" + json_escape(user) + "\"}]}";
    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!in.api_key.empty()) headers += L"Authorization: Bearer " + widen(in.api_key) + L"\r\n";
    HttpResult res = http_post(endpoint_for(in.api_base), payload, headers, 90000);
    if (!res.error.empty() && res.body.empty()) {
        err = res.error;
        return false;
    }
    Json root;
    std::string perr;
    if (!json_parse(res.body, root, perr) || root.type != Json::Obj) {
        err = res.error.empty() ? "API did not return JSON" : res.error;
        return false;
    }
    if (res.status >= 400 || root.field("error")) {
        const Json* e = root.field("error");
        err = e ? e->str("message", "API error") : "API error";
        return false;
    }
    const Json* choices = root.field("choices");
    if (!choices || choices->type != Json::Arr || choices->a.empty()) {
        err = "API returned no choices";
        return false;
    }
    const Json* msg = choices->a[0].field("message");
    std::string content = msg ? msg->str("content") : "";
    std::string obj = extract_object(content);
    if (obj.empty()) {
        err = "model did not return a storyboard";
        return false;
    }
    Storyboard parsed;
    if (!story_from_json(obj, parsed, err)) return false;
    board = std::move(parsed);
    err.clear();
    return true;
}

}  // namespace kare

#endif
