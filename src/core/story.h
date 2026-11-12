#pragma once

#include <string>
#include <vector>

namespace kare {

constexpr int kMaxScenes = 24;
constexpr double kMaxSceneSeconds = 30.0;
constexpr double kMaxTotalSeconds = 180.0;

constexpr int kMaxLayers = 16;

// A free element the user places by hand on top of a scene. Positions are fractions of the frame so a layer
// keeps its place when the aspect or resolution changes.
struct Layer {
    std::string kind = "text";  // text, image, box
    std::string text;
    std::string image;
    double x = 0.5, y = 0.5;  // centre
    double w = 0.5;           // width (text wraps inside it)
    double h = 0.12;          // box height
    double size = 1.0;        // text size multiplier
    double rotation = 0.0;    // degrees, boxes and images only
    std::string color = "#ffffff";
    std::string fill;  // text backdrop or box colour, empty for none
    double opacity = 1.0;
    double radius = 0.0;
    double start = 0.0, end = 0.0;  // seconds inside the scene; end <= start runs to the scene end
    std::string in = "fade";        // none fade rise pop slide type
    std::string out = "fade";       // none fade
    bool bold = true;
    bool shadow = true;
    int align = 1;  // 0 left, 1 centre, 2 right
};

struct Look {
    std::string filter = "none";
    double brightness = 0, contrast = 0, saturation = 0, warmth = 0, vignette = 0;
    bool active() const {
        return filter != "none" || brightness != 0 || contrast != 0 || saturation != 0 || warmth != 0 || vignette != 0;
    }
};

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
    std::string narration;
    std::string camera = "auto";  // auto in out left right up down still
    std::string fit = "auto";     // auto cover contain
    double focus_x = 0.5, focus_y = 0.5;
    double zoom = 1.0;
    Look look;
    std::vector<Layer> layers;
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
    std::string voice = "none";
    int voice_rate = 0;
    std::string captions = "auto";
    std::string credits;
    std::string music_file;
    double music_volume = 1.0;
    double voice_volume = 1.0;
    double caption_y = 0.0;  // 0 = automatic placement, otherwise the caption centre as a fraction of height
    double caption_size = 1.0;
    std::string caption_color = "#ffd84d";
    bool caption_upper = true;
    std::vector<Scene> scenes;
};

const std::vector<std::string>& layer_kind_ids();
const std::vector<std::string>& layer_in_ids();
const std::vector<std::string>& camera_ids();
const std::vector<std::string>& fit_ids();
const std::vector<std::string>& filter_ids();
bool parse_hex_color(const std::string& hex, unsigned char rgb[3]);
std::string hex_color(const unsigned char rgb[3]);
void normalize_layer(Layer& layer);

bool has_narration(const Storyboard& board);
const std::vector<std::string>& caption_ids();

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
