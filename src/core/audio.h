#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/story.h"

namespace kare {

constexpr int kAudioRate = 44100;

std::string resolve_music(const Storyboard& board);
double music_bpm(const std::string& mood);
double beat_seconds(const Storyboard& board);
void snap_to_beat(Storyboard& board, double target_seconds = 0);

// Interleaved stereo 16-bit samples covering the whole storyboard; empty when the music is "none".
std::vector<int16_t> render_soundtrack(const Storyboard& board);
std::vector<uint8_t> wav_bytes(const std::vector<int16_t>& stereo, size_t start_frame = 0);
std::vector<float> audio_peaks(const std::vector<int16_t>& stereo, int buckets);

}  // namespace kare
