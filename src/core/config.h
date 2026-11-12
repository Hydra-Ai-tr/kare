#pragma once

#include <string>
#include <vector>

namespace kare {

struct Config {
    std::string lang = "tr";
    bool use_api = false;
    std::string api_base = "http://127.0.0.1:11434/v1";
    std::string api_model = "qwen2.5:7b";
    std::string api_key;
    std::string github_token;
    bool mark = true;
    std::string last_dir;
    std::vector<std::string> recent_projects;
    std::vector<std::string> recent_exports;
};

std::string config_path();
std::string data_dir();
Config load_config();
void save_config(const Config& cfg);
std::string default_export_path();
void push_recent(std::vector<std::string>& list, const std::string& path, size_t limit = 8);

}  // namespace kare
