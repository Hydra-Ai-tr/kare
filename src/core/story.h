#pragma once

#include <string>
#include <vector>

namespace kare {

struct Scene {
    std::string title;
    std::string text;
    double seconds = 2.5;
    std::string motion = "drift";
    std::string style;
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

}  // namespace kare
