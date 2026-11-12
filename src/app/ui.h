#pragma once

#include <string>
#include <vector>

namespace kare {

struct ShotInput {
    float x = 0, y = 0;
    int button = -1;
    bool drag = false;
    float x2 = 0, y2 = 0;
};

struct Launch {
    std::string open_path;
    std::string shot;
    std::string page;
    std::string lang;
    int width = 1600, height = 900;
    float scale = 1.f;
    double play_at = -1;
    std::vector<ShotInput> inputs;
};

void run_app(const Launch& launch = {});
}
