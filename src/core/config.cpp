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
    cfg.mark = j.boolean("mark", true);
    cfg.last_dir = j.str("last_dir");
    return cfg;
}

void save_config(const Config& cfg) {
    ensure_dir(config_dir());
    Json j = Json::obj();
    j.put("lang", Json::strv(cfg.lang));
    j.put("use_api", Json::boolv(cfg.use_api));
    j.put("api_base", Json::strv(cfg.api_base));
    j.put("api_model", Json::strv(cfg.api_model));
    j.put("api_key", Json::strv(cfg.api_key));
    j.put("mark", Json::boolv(cfg.mark));
    j.put("last_dir", Json::strv(cfg.last_dir));
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
