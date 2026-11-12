#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kare {

// Decodes an audio file (mp3, wav, m4a, wma, ...) to interleaved 44.1 kHz stereo. Results are cached per path.
std::shared_ptr<const std::vector<int16_t>> load_audio(const std::string& path, std::string* err = nullptr);

}  // namespace kare
