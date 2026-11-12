#pragma once

#include <string>
#include <vector>

#include "core/director.h"

namespace kare {

struct ScriptBeat {
    std::string narration, title, kicker;
    int image = -1;
};

bool plan_with_api(const PlanInput& in, Storyboard& board, std::string& err);
// Writes a narrated documentary script from research text; images are one-line descriptions indexed by position.
bool script_with_api(const PlanInput& in, const std::string& brief, const std::vector<std::string>& images,
                     const std::string& lang, double seconds, bool shorts, std::vector<ScriptBeat>& beats,
                     std::string& err);
bool test_api(const std::string& base, const std::string& key, std::string& message);

}  // namespace kare
