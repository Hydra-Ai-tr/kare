#pragma once

#include <string>

#include "core/director.h"

namespace kare {

bool plan_with_api(const PlanInput& in, Storyboard& board, std::string& err);

}  // namespace kare
