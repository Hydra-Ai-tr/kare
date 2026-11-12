#pragma once

#include <cstdint>
#include <string>
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

std::string resolve_layout(const Storyboard& board, int index);
std::string resolve_transition(const Storyboard& board, int index);
std::string resolve_anim(const Storyboard& board, int index);
double transition_seconds(const Storyboard& board, int index);
bool image_readable(const std::string& path, int* w = nullptr, int* h = nullptr);

// Final (un-animated) placement of a layer in board pixels; text layers report their wrap width and text height.
bool layer_box(const Storyboard& board, int scene, int layer, float& cx, float& cy, float& w, float& h);
bool layer_visible(const Storyboard& board, int scene, int layer, double local);

}  // namespace kare
