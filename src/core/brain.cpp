#include "core/brain.h"

#include "core/http.h"
#include "core/json.h"
#include "core/story.h"
#include "core/utf8.h"

namespace kare {

namespace {

std::string trim_base(std::string base) {
    while (!base.empty() && (base.back() == '/' || base.back() == ' ')) base.pop_back();
    return base;
}

std::string endpoint_for(std::string base) {
    base = trim_base(base);
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

std::string auth_headers(const std::string& key) {
    std::string h = "Content-Type: application/json\r\n";
    if (!key.empty()) h += "Authorization: Bearer " + key + "\r\n";
    return h;
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
        "\"aspect\":\"16:9|9:16|1:1\",\"music\":\"auto|ambient|pulse|drive|lofi|none\","
        "\"scenes\":[{\"title\":\"string\",\"text\":\"string\",\"kicker\":\"string\",\"seconds\":2.5,"
        "\"layout\":\"auto|hero|statement|list|stat|quote|split|terminal|bars|end\","
        "\"transition\":\"auto|fade|slide|wipe|zoom|flash|glitch|cut\","
        "\"anim\":\"auto|rise|fade|type|reveal|spring\",\"motion\":\"drift|zoom|rise|pan|still\"}]} "
        "Edit like a professional motion designer: open with a hero scene, vary layouts, end with an end card. "
        "Layout rules: list = text with one item per line (\\n); bars = lines like \"Label 72%\"; "
        "terminal = shell commands one per line; stat = title is a number such as 98% or 1.2k with a short text; "
        "quote = text is a quotation; end = call to action, title is a URL or handle. "
        "kicker is an optional 1-3 word label shown above the title. Wrap one key word of a title in *asterisks* "
        "to highlight it. Use 3 to 10 scenes, each between 1.5 and 8 seconds. On-screen text must be in the "
        "user's language and short enough to read on a frame. Every enum field must be one of the listed values.";
    std::string user = in.prompt;
    if (in.seconds > 0) user += "\nTarget duration seconds: " + std::to_string(static_cast<int>(in.seconds));
    if (!in.aspect.empty()) user += "\nAspect: " + in.aspect;
    if (!in.style.empty()) user += "\nStyle: " + in.style;
    std::string payload = std::string("{\"model\":\"") + json_escape(in.api_model) +
                          "\",\"temperature\":0.4,\"messages\":[{\"role\":\"system\",\"content\":\"" +
                          json_escape(system) + "\"},{\"role\":\"user\",\"content\":\"" + json_escape(user) + "\"}]}";
    HttpResult res = http_request("POST", endpoint_for(in.api_base), payload, auth_headers(in.api_key), 90000);
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

bool test_api(const std::string& base, const std::string& key, std::string& message) {
    if (trim_base(base).empty()) {
        message = "API address is empty";
        return false;
    }
    HttpResult res = http_request("GET", trim_base(base) + "/models", "", auth_headers(key), 8000);
    if (res.status == 0) {
        message = res.error.empty() ? "no response" : res.error;
        return false;
    }
    if (res.status >= 400) {
        message = "HTTP " + std::to_string(res.status);
        return false;
    }
    Json root;
    std::string perr;
    int count = 0;
    if (json_parse(res.body, root, perr) && root.type == Json::Obj) {
        const Json* data = root.field("data");
        if (data && data->type == Json::Arr) count = static_cast<int>(data->a.size());
    }
    message = std::to_string(count) + " model";
    return true;
}

}  // namespace kare
