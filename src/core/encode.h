#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/story.h"

namespace kare {

using Progress = std::function<bool(float)>;

std::string encode_video(const Storyboard& board, const std::string& path, const Progress& progress = {});
bool write_png(const std::string& path, int w, int h, const uint8_t* rgba, std::string& err);
bool write_preview_png(const std::string& path, const Storyboard& board, double time_sec, std::string& err);
bool probe_video(const std::string& path, std::string& err, bool* has_audio = nullptr);

}  // namespace kare
