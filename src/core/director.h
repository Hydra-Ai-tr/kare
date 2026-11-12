#pragma once

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
};

struct PlanResult {
    Storyboard board;
    std::string warning;
    bool from_api = false;
    std::string repo;
    std::string repo_note;
};

PlanResult plan(const PlanInput& in);
Storyboard plan_local(const PlanInput& in);

}  // namespace kare
