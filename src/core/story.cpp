#include "core/story.h"

#include <algorithm>

#include "core/json.h"
#include "core/utf8.h"

namespace kare {

double total_seconds(const Storyboard& board) {
    double t = 0;
    for (const auto& s : board.scenes) t += s.seconds;
    return t;
}

bool known_style(const std::string& style) {
    return style == "ember" || style == "dusk" || style == "night" || style == "sea" || style == "forest" ||
           style == "neon" || style == "paper" || style == "minimal";
}

bool known_motion(const std::string& motion) {
    return motion == "drift" || motion == "zoom" || motion == "rise";
}

void apply_aspect_size(Storyboard& board) {
    if (board.aspect == "9:16") {
        board.width = 720;
        board.height = 1280;
    } else if (board.aspect == "1:1") {
        board.width = 720;
        board.height = 720;
    } else {
        board.aspect = "16:9";
        board.width = 1280;
        board.height = 720;
    }
}

static bool valid_size(int w, int h) {
    return w >= 16 && h >= 16 && w <= 1920 && h <= 1920 && (w % 2) == 0 && (h % 2) == 0;
}

void scale_duration(Storyboard& board, double seconds) {
    if (seconds < 0.4 || board.scenes.empty()) return;
    double t = total_seconds(board);
    if (t < 0.01) {
        for (auto& s : board.scenes) s.seconds = seconds / static_cast<double>(board.scenes.size());
        return;
    }
    double k = seconds / t;
    for (auto& s : board.scenes) s.seconds = std::max(0.4, s.seconds * k);
}

void normalize(Storyboard& board) {
    if (board.fps != 12 && board.fps != 24 && board.fps != 30) board.fps = 24;
    if (board.aspect != "16:9" && board.aspect != "9:16" && board.aspect != "1:1") board.aspect = "16:9";
    if (!valid_size(board.width, board.height)) apply_aspect_size(board);
    if (!known_style(board.style)) board.style = "ember";
    if (board.scenes.size() > 12) board.scenes.resize(12);
    if (board.scenes.empty()) {
        Scene s;
        s.title = "Kare";
        s.seconds = 4;
        s.motion = "zoom";
        board.scenes.push_back(s);
    }
    for (auto& s : board.scenes) {
        if (!known_motion(s.motion)) s.motion = "drift";
        if (!s.style.empty() && !known_style(s.style)) s.style.clear();
        if (s.seconds < 0.4) s.seconds = 0.4;
        if (s.seconds > 20) s.seconds = 20;
        s.title = tidy(s.title);
        s.text = tidy(s.text);
    }
    double t = total_seconds(board);
    if (t > 120.0) scale_duration(board, 120.0);
    if (board.title.empty()) {
        for (const auto& s : board.scenes) {
            if (!s.title.empty()) {
                board.title = s.title;
                break;
            }
        }
    }
    if (board.title.empty()) board.title = "Kare";
}

bool move_scene(Storyboard& board, int from, int to) {
    int n = static_cast<int>(board.scenes.size());
    if (from < 0 || to < 0 || from >= n || to >= n || from == to) return false;
    Scene s = board.scenes[static_cast<size_t>(from)];
    board.scenes.erase(board.scenes.begin() + from);
    board.scenes.insert(board.scenes.begin() + to, std::move(s));
    return true;
}

void add_scene(Storyboard& board, int index) {
    Scene s;
    s.title = "Yeni sahne";
    s.seconds = 2.5;
    const char* motions[] = {"drift", "zoom", "rise"};
    s.motion = motions[board.scenes.size() % 3];
    if (index < 0 || index > static_cast<int>(board.scenes.size())) index = static_cast<int>(board.scenes.size());
    board.scenes.insert(board.scenes.begin() + index, std::move(s));
}

void duplicate_scene(Storyboard& board, int index) {
    if (index < 0 || index >= static_cast<int>(board.scenes.size())) return;
    Scene s = board.scenes[static_cast<size_t>(index)];
    board.scenes.insert(board.scenes.begin() + index + 1, std::move(s));
}

void remove_scene(Storyboard& board, int index) {
    if (board.scenes.size() <= 1) return;
    if (index < 0 || index >= static_cast<int>(board.scenes.size())) return;
    board.scenes.erase(board.scenes.begin() + index);
}

std::string story_to_json(const Storyboard& board) {
    Json j = Json::obj();
    j.put("prompt", Json::strv(board.prompt));
    j.put("title", Json::strv(board.title));
    j.put("style", Json::strv(board.style));
    j.put("aspect", Json::strv(board.aspect));
    j.put("fps", Json::numv(board.fps));
    j.put("width", Json::numv(board.width));
    j.put("height", Json::numv(board.height));
    j.put("mark", Json::boolv(board.mark));
    Json scenes = Json::arr();
    for (const auto& s : board.scenes) {
        Json o = Json::obj();
        o.put("title", Json::strv(s.title));
        o.put("text", Json::strv(s.text));
        o.put("seconds", Json::numv(s.seconds));
        o.put("motion", Json::strv(s.motion));
        o.put("style", Json::strv(s.style));
        scenes.a.push_back(std::move(o));
    }
    j.put("scenes", std::move(scenes));
    return j.dump();
}

bool story_from_json(const std::string& text, Storyboard& board, std::string& err) {
    Json j;
    if (!json_parse(text, j, err)) return false;
    if (j.type != Json::Obj) {
        err = "storyboard must be an object";
        return false;
    }
    Storyboard b;
    b.prompt = j.str("prompt");
    b.title = j.str("title");
    b.style = j.str("style", "ember");
    b.aspect = j.str("aspect", "16:9");
    b.fps = static_cast<int>(j.num("fps", 24));
    b.width = static_cast<int>(j.num("width", 0));
    b.height = static_cast<int>(j.num("height", 0));
    b.mark = j.boolean("mark", true);
    const Json* scenes = j.field("scenes");
    if (scenes && scenes->type == Json::Arr) {
        for (const auto& item : scenes->a) {
            if (item.type != Json::Obj) continue;
            Scene s;
            s.title = item.str("title");
            s.text = item.str("text");
            s.seconds = item.num("seconds", 2.5);
            s.motion = item.str("motion", "drift");
            s.style = item.str("style");
            if (s.title.empty() && s.text.empty()) continue;
            b.scenes.push_back(std::move(s));
        }
    }
    normalize(b);
    board = std::move(b);
    err.clear();
    return true;
}

bool save_story(const std::string& path, const Storyboard& board, std::string& err) {
    if (!write_text(path, story_to_json(board))) {
        err = "could not write project";
        return false;
    }
    err.clear();
    return true;
}

bool load_story(const std::string& path, Storyboard& board, std::string& err) {
    std::string text;
    if (!read_text(path, text)) {
        err = "could not read project";
        return false;
    }
    return story_from_json(text, board, err);
}

}  // namespace kare
