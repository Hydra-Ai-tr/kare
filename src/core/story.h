#pragma once

#include <string>
#include <vector>

namespace kare {

constexpr int kMaxScenes = 16;

struct Scene {
    std::string title;
    std::string text;
    double seconds = 2.5;
    std::string motion = "drift";
    std::string style;
    std::string layout = "auto";
    std::string transition = "auto";
    std::string anim = "auto";
    std::string kicker;
    std::string image;
};

struct Storyboard {
    std::string prompt;
    std::string title;
    std::string style = "ember";
    std::string aspect = "16:9";
    int fps = 24;
    int width = 1280;
    int height = 720;
    bool mark = true;
    std::string music = "auto";
    bool grain = true;
    std::vector<Scene> scenes;
};

double total_seconds(const Storyboard& board);
void apply_aspect_size(Storyboard& board);
void normalize(Storyboard& board);
void scale_duration(Storyboard& board, double seconds);
bool move_scene(Storyboard& board, int from, int to);
void add_scene(Storyboard& board, int index);
void duplicate_scene(Storyboard& board, int index);
void remove_scene(Storyboard& board, int index);

std::string story_to_json(const Storyboard& board);
bool story_from_json(const std::string& text, Storyboard& board, std::string& err);
bool save_story(const std::string& path, const Storyboard& board, std::string& err);
bool load_story(const std::string& path, Storyboard& board, std::string& err);

bool known_style(const std::string& style);
bool known_motion(const std::string& motion);
bool known_layout(const std::string& layout);
bool known_transition(const std::string& transition);
bool known_anim(const std::string& anim);
bool known_music(const std::string& music);

const std::vector<std::string>& layout_ids();
const std::vector<std::string>& transition_ids();
const std::vector<std::string>& anim_ids();
const std::vector<std::string>& music_ids();
const std::vector<std::string>& motion_ids();

}  // namespace kare
