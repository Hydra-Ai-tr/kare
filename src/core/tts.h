#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/story.h"

namespace kare {

// Narration starts this long after its scene begins, so it never fights the cut.
constexpr double kVoiceLead = 0.18;

struct VoiceWord {
    double start = 0, end = 0;
    std::string text;
};

struct VoiceClip {
    std::vector<int16_t> pcm;  // mono, kAudioRate
    double seconds = 0;        // without trailing silence
    std::vector<VoiceWord> words;
};

struct VoiceInfo {
    std::string name;
    std::string lang;  // "tr", "en", ... or "" when unknown
};

std::vector<VoiceInfo> list_voices();
// "tr" / "en" pick the first installed voice for that language; anything else is a voice name.
std::string voice_language(const std::string& voice);
bool voice_available(const std::string& voice);
std::string speakable(const std::string& text);
// With synthesize_now = false only an already spoken clip is returned, so callers on the UI thread never block.
std::shared_ptr<const VoiceClip> narrate(const std::string& text, const std::string& voice, int rate,
                                         bool synthesize_now = true);
std::shared_ptr<const VoiceClip> scene_voice(const Storyboard& board, int index, bool synthesize_now = true);
void prepare_voices(const Storyboard& board);
// Lengthens or shortens narrated scenes so each line fits; index < 0 fits every scene.
void fit_to_voice(Storyboard& board, int index = -1);

}  // namespace kare
