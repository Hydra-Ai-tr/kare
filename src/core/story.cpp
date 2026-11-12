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

namespace {

bool listed(const std::vector<std::string>& ids, const std::string& v) {
    return std::find(ids.begin(), ids.end(), v) != ids.end();
}

}  // namespace

const std::vector<std::string>& layout_ids() {
    static const std::vector<std::string> ids = {"auto",     "hero", "statement", "list",     "stat", "quote", "split",
                                                 "terminal", "bars", "media",     "showcase", "story", "end"};
    return ids;
}

const std::vector<std::string>& caption_ids() {
    static const std::vector<std::string> ids = {"auto", "none", "words", "line"};
    return ids;
}

// Spoken text keeps its punctuation: a trailing comma or period changes the intonation.
static std::string squash_spaces(const std::string& s) {
    std::string out;
    bool space = false;
    for (char ch : s) {
        if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
            space = !out.empty();
            continue;
        }
        if (space) out.push_back(' ');
        space = false;
        out.push_back(ch);
    }
    return out;
}

bool has_narration(const Storyboard& board) {
    if (board.voice == "none" || board.voice.empty()) return false;
    for (const auto& s : board.scenes)
        if (!s.narration.empty()) return true;
    return false;
}

const std::vector<std::string>& transition_ids() {
    static const std::vector<std::string> ids = {"auto", "fade", "slide", "wipe", "zoom", "flash", "glitch", "cut"};
    return ids;
}

const std::vector<std::string>& anim_ids() {
    static const std::vector<std::string> ids = {"auto", "rise", "fade", "type", "reveal", "spring"};
    return ids;
}

const std::vector<std::string>& music_ids() {
    static const std::vector<std::string> ids = {"auto", "none", "ambient", "pulse", "drive", "lofi"};
    return ids;
}

const std::vector<std::string>& motion_ids() {
    static const std::vector<std::string> ids = {"drift", "zoom", "rise", "pan", "still"};
    return ids;
}

bool known_style(const std::string& style) {
    return style == "ember" || style == "dusk" || style == "night" || style == "sea" || style == "forest" ||
           style == "neon" || style == "paper" || style == "minimal";
}

bool known_motion(const std::string& motion) { return listed(motion_ids(), motion); }
bool known_layout(const std::string& layout) { return listed(layout_ids(), layout); }
bool known_transition(const std::string& transition) { return listed(transition_ids(), transition); }
bool known_anim(const std::string& anim) { return listed(anim_ids(), anim); }
bool known_music(const std::string& music) { return listed(music_ids(), music); }

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
    if (!known_music(board.music)) board.music = "auto";
    if (board.voice.empty()) board.voice = "none";
    board.voice_rate = std::clamp(board.voice_rate, -5, 6);
    if (!listed(caption_ids(), board.captions)) board.captions = "auto";
    if (board.scenes.size() > static_cast<size_t>(kMaxScenes)) board.scenes.resize(static_cast<size_t>(kMaxScenes));
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
        if (!known_layout(s.layout)) s.layout = "auto";
        if (!known_transition(s.transition)) s.transition = "auto";
        if (!known_anim(s.anim)) s.anim = "auto";
        s.kicker = tidy(s.kicker);
        if (s.seconds < 0.4) s.seconds = 0.4;
        if (s.seconds > kMaxSceneSeconds) s.seconds = kMaxSceneSeconds;
        s.title = tidy(s.title);
        s.text = tidy_lines(s.text);
        s.narration = squash_spaces(s.narration);
    }
    double t = total_seconds(board);
    if (t > kMaxTotalSeconds) scale_duration(board, kMaxTotalSeconds);
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
    if (static_cast<int>(board.scenes.size()) >= kMaxScenes) return;
    Scene s;
    s.title = "Yeni sahne";
    s.seconds = 2.5;
    const char* motions[] = {"drift", "zoom", "pan", "rise"};
    s.motion = motions[board.scenes.size() % 4];
    if (index < 0 || index > static_cast<int>(board.scenes.size())) index = static_cast<int>(board.scenes.size());
    board.scenes.insert(board.scenes.begin() + index, std::move(s));
}

void duplicate_scene(Storyboard& board, int index) {
    if (index < 0 || index >= static_cast<int>(board.scenes.size()) || static_cast<int>(board.scenes.size()) >= kMaxScenes) return;
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
    j.put("music", Json::strv(board.music));
    j.put("grain", Json::boolv(board.grain));
    j.put("voice", Json::strv(board.voice));
    j.put("voice_rate", Json::numv(board.voice_rate));
    j.put("captions", Json::strv(board.captions));
    if (!board.credits.empty()) j.put("credits", Json::strv(board.credits));
    Json scenes = Json::arr();
    for (const auto& s : board.scenes) {
        Json o = Json::obj();
        o.put("title", Json::strv(s.title));
        o.put("text", Json::strv(s.text));
        o.put("seconds", Json::numv(s.seconds));
        o.put("motion", Json::strv(s.motion));
        o.put("style", Json::strv(s.style));
        o.put("layout", Json::strv(s.layout));
        o.put("transition", Json::strv(s.transition));
        o.put("anim", Json::strv(s.anim));
        if (!s.kicker.empty()) o.put("kicker", Json::strv(s.kicker));
        if (!s.image.empty()) o.put("image", Json::strv(s.image));
        if (!s.narration.empty()) o.put("narration", Json::strv(s.narration));
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
    b.music = j.str("music", "auto");
    b.grain = j.boolean("grain", true);
    b.voice = j.str("voice", "none");
    b.voice_rate = static_cast<int>(j.num("voice_rate", 0));
    b.captions = j.str("captions", "auto");
    b.credits = j.str("credits");
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
            s.layout = item.str("layout", "auto");
            s.transition = item.str("transition", "auto");
            s.anim = item.str("anim", "auto");
            s.kicker = item.str("kicker");
            s.image = item.str("image");
            s.narration = item.str("narration");
            if (s.title.empty() && s.text.empty() && s.image.empty() && s.narration.empty()) continue;
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
    if (!story_from_json(text, board, err)) return false;
    size_t slash = path.find_last_of("\\/");
    std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    for (auto& s : board.scenes) {
        const std::string& im = s.image;
        bool absolute = (im.size() > 1 && im[1] == ':') || (!im.empty() && (im[0] == '\\' || im[0] == '/'));
        if (!im.empty() && !absolute && !dir.empty()) s.image = dir + im;
    }
    return true;
}

}  // namespace kare
