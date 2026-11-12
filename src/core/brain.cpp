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

bool chat(const PlanInput& in, const std::string& system, const std::string& user, double temperature,
          std::string& content, std::string& err) {
    if (in.api_base.empty()) {
        err = "API address is empty";
        return false;
    }
    if (in.api_model.empty()) {
        err = "API model is empty";
        return false;
    }
    std::string payload = std::string("{\"model\":\"") + json_escape(in.api_model) +
                          "\",\"temperature\":" + std::to_string(temperature) +
                          ",\"messages\":[{\"role\":\"system\",\"content\":\"" + json_escape(system) +
                          "\"},{\"role\":\"user\",\"content\":\"" + json_escape(user) + "\"}]}";
    HttpResult res = http_request("POST", endpoint_for(in.api_base), payload, auth_headers(in.api_key), 120000);
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
    content = msg ? msg->str("content") : "";
    return true;
}

}  // namespace

bool script_with_api(const PlanInput& in, const std::string& brief, const std::vector<std::string>& images,
                     const std::string& lang, double seconds, bool shorts, std::vector<ScriptBeat>& beats,
                     std::string& err) {
    int words = static_cast<int>(seconds * (lang == "tr" ? 2.1 : 2.5));
    std::string system =
        "You write the voice-over script of a documentary video from a researched article. Use only facts that are "
        "in the article. Reply with JSON only, no markdown. Schema: {\"scenes\":[{\"narration\":\"string\","
        "\"title\":\"string\",\"kicker\":\"string\",\"image\":0}]}. narration is what the narrator says in that "
        "scene: one or two natural spoken sentences, 8 to 30 words, no lists, no parentheses, numbers written as "
        "people say them. The first scene is a strong hook that makes the viewer stay; its title is the topic name "
        "in at most 4 words. Other titles are empty. kicker is an optional year or 1-2 word label. image is the "
        "index of the listed picture that best matches the scene; use each picture at most once when possible. "
        "Tell the story in chronological order and end with a memorable closing line.";
    system += std::string(" Language of narration, titles and kickers: ") + (lang == "tr" ? "Turkish." : "English.");
    std::string user = "Video format: " + std::string(shorts ? "vertical short (Shorts/Reels)" : "landscape documentary") +
                       ". Target length about " + std::to_string(static_cast<int>(seconds)) + " seconds, about " +
                       std::to_string(words) + " spoken words in total.\n\nARTICLE:\n" + brief + "\n\nPICTURES:\n";
    for (size_t i = 0; i < images.size(); i++) user += std::to_string(i) + ": " + images[i] + "\n";
    std::string content;
    if (!chat(in, system, user, 0.5, content, err)) return false;
    std::string obj = extract_object(content);
    Json root;
    std::string perr;
    if (obj.empty() || !json_parse(obj, root, perr) || root.type != Json::Obj) {
        err = "model did not return a script";
        return false;
    }
    const Json* scenes = root.field("scenes");
    if (!scenes || scenes->type != Json::Arr) {
        err = "model did not return scenes";
        return false;
    }
    beats.clear();
    for (const auto& s : scenes->a) {
        if (s.type != Json::Obj) continue;
        ScriptBeat b;
        b.narration = s.str("narration");
        b.title = s.str("title");
        b.kicker = s.str("kicker");
        b.image = static_cast<int>(s.num("image", -1));
        if (!b.narration.empty()) beats.push_back(std::move(b));
    }
    if (beats.size() < 3) {
        err = "model returned too few scenes";
        return false;
    }
    err.clear();
    return true;
}

bool plan_with_api(const PlanInput& in, Storyboard& board, std::string& err) {
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
    std::string content;
    if (!chat(in, system, user, 0.4, content, err)) return false;
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
