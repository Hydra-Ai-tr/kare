#pragma once

#include <string>

namespace kare {

struct Config {
    std::string lang = "tr";
    bool use_api = false;
    std::string api_base = "http://127.0.0.1:11434/v1";
    std::string api_model = "qwen2.5:7b";
    std::string api_key;
    bool mark = true;
    std::string last_dir;
};

std::string config_path();
Config load_config();
void save_config(const Config& cfg);
std::string default_export_path();

}  // namespace kare
