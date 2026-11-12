#include "core/config.h"

#include "core/json.h"
#include "core/utf8.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace kare {

static std::string config_dir() {
#ifdef _WIN32
    if (const wchar_t* dir = _wgetenv(L"KARE_CONFIG_DIR"); dir && *dir) return narrow(dir);
    wchar_t buf[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) return ".";
    return narrow(buf) + "\\Kare";
#else
    return ".";
#endif
}

std::string config_path() {
#ifdef _WIN32
    return config_dir() + "\\config.json";
#else
    return "kare-config.json";
#endif
}

Config load_config() {
    Config cfg;
    std::string text;
    if (!read_text(config_path(), text)) return cfg;
    Json j;
    std::string err;
    if (!json_parse(text, j, err) || j.type != Json::Obj) return cfg;
    cfg.lang = j.str("lang", "tr");
    if (cfg.lang != "en") cfg.lang = "tr";
    cfg.use_api = j.boolean("use_api", false);
    cfg.api_base = j.str("api_base", cfg.api_base);
    cfg.api_model = j.str("api_model", cfg.api_model);
    cfg.api_key = j.str("api_key");
    cfg.github_token = j.str("github_token");
    cfg.mark = j.boolean("mark", true);
    cfg.last_dir = j.str("last_dir");
    auto list = [&](const char* key, std::vector<std::string>& out) {
        const Json* arr = j.field(key);
        if (!arr || arr->type != Json::Arr) return;
        for (const auto& item : arr->a)
            if (item.type == Json::Str && !item.s.empty()) out.push_back(item.s);
    };
    list("recent_projects", cfg.recent_projects);
    list("recent_exports", cfg.recent_exports);
    return cfg;
}

void push_recent(std::vector<std::string>& list, const std::string& path, size_t limit) {
    if (path.empty()) return;
    std::string key = fold_tr(path);
    for (size_t i = 0; i < list.size(); i++) {
        if (fold_tr(list[i]) == key) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    list.insert(list.begin(), path);
    if (list.size() > limit) list.resize(limit);
}

void save_config(const Config& cfg) {
    ensure_dir(config_dir());
    Json j = Json::obj();
    j.put("lang", Json::strv(cfg.lang));
    j.put("use_api", Json::boolv(cfg.use_api));
    j.put("api_base", Json::strv(cfg.api_base));
    j.put("api_model", Json::strv(cfg.api_model));
    j.put("api_key", Json::strv(cfg.api_key));
    j.put("github_token", Json::strv(cfg.github_token));
    j.put("mark", Json::boolv(cfg.mark));
    j.put("last_dir", Json::strv(cfg.last_dir));
    auto list = [](const std::vector<std::string>& items) {
        Json arr = Json::arr();
        for (const auto& s : items) arr.a.push_back(Json::strv(s));
        return arr;
    };
    j.put("recent_projects", list(cfg.recent_projects));
    j.put("recent_exports", list(cfg.recent_exports));
    write_text(config_path(), j.dump());
}

std::string default_export_path() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYVIDEO, nullptr, 0, buf))) return narrow(std::wstring(buf)) + "\\kare.mp4";
#endif
    return "kare.mp4";
}

}  // namespace kare
