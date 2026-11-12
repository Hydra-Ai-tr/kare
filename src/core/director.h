#pragma once

#include <functional>
#include <string>

#include "core/story.h"

namespace kare {

struct PlanInput {
    std::string prompt;
    std::string style;
    std::string aspect;
    double seconds = 0;
    bool mark = true;
    std::string lang = "tr";
    bool use_api = false;
    std::string api_base;
    std::string api_key;
    std::string api_model;
    std::string github_token;
    int web = 0;                  // 1 always research the topic online, -1 never, 0 when the prompt asks for it
    std::string voice = "auto";   // auto, none, tr, en or an installed voice name
    bool shorts = false;          // vertical 1080x1920 short
    std::function<void(const std::string&)> note;  // progress messages while planning
};

struct PlanResult {
    Storyboard board;
    std::string warning;
    bool from_api = false;
    std::string repo;
    std::string repo_note;
    std::string source;  // researched article URL
};

PlanResult plan(const PlanInput& in);
PlanResult plan_plain(const PlanInput& in);
Storyboard plan_local(const PlanInput& in);

}  // namespace kare
