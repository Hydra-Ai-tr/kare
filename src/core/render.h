#pragma once

#include <cstdint>
#include <vector>

#include "core/story.h"

namespace kare {

struct Frame {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
};

void render_frame(const Storyboard& board, double time_sec, Frame& out);
uint64_t frame_hash(const Frame& frame);

}  // namespace kare
