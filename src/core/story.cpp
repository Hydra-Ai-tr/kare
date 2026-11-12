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

const std::vector<std::string>& layer_kind_ids() {
    static const std::vector<std::string> ids = {"text", "image", "box"};
    return ids;
}

const std::vector<std::string>& layer_in_ids() {
    static const std::vector<std::string> ids = {"none", "fade", "rise", "pop", "slide", "type"};
    return ids;
}

const std::vector<std::string>& camera_ids() {
    static const std::vector<std::string> ids = {"auto", "in", "out", "left", "right", "up", "down", "still"};
    return ids;
}

const std::vector<std::string>& fit_ids() {
    static const std::vector<std::string> ids = {"auto", "cover", "contain"};
    return ids;
}

const std::vector<std::string>& filter_ids() {
    static const std::vector<std::string> ids = {"none", "bw", "sepia", "vintage", "warm", "cool", "vivid", "noir", "fade"};
    return ids;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_hex_color(const std::string& hex, unsigned char rgb[3]) {
    std::string h = !hex.empty() && hex[0] == '#' ? hex.substr(1) : hex;
    if (h.size() == 3) h = std::string{h[0], h[0], h[1], h[1], h[2], h[2]};
    if (h.size() != 6) return false;
    for (int k = 0; k < 3; k++) {
        int a = hex_digit(h[static_cast<size_t>(k) * 2]), b = hex_digit(h[static_cast<size_t>(k) * 2 + 1]);
        if (a < 0 || b < 0) return false;
        rgb[k] = static_cast<unsigned char>(a * 16 + b);
    }
    return true;
}

std::string hex_color(const unsigned char rgb[3]) {
    static const char* d = "0123456789abcdef";
    std::string s = "#";
    for (int k = 0; k < 3; k++) {
        s.push_back(d[rgb[k] >> 4]);
        s.push_back(d[rgb[k] & 15]);
    }
    return s;
}

static std::string clean_color(const std::string& c, const std::string& fallback) {
    unsigned char rgb[3];
    if (c.empty()) return fallback;
    return parse_hex_color(c, rgb) ? hex_color(rgb) : fallback;
}

void normalize_layer(Layer& l) {
    if (!listed(layer_kind_ids(), l.kind)) l.kind = "text";
    if (!listed(layer_in_ids(), l.in)) l.in = "fade";
    if (l.out != "none") l.out = "fade";
    l.x = std::clamp(l.x, -0.5, 1.5);
    l.y = std::clamp(l.y, -0.5, 1.5);
    l.w = std::clamp(l.w, 0.02, 2.0);
    l.h = std::clamp(l.h, 0.005, 2.0);
    l.size = std::clamp(l.size, 0.2, 6.0);
    l.rotation = std::clamp(l.rotation, -180.0, 180.0);
    l.opacity = std::clamp(l.opacity, 0.0, 1.0);
    l.radius = std::clamp(l.radius, 0.0, 200.0);
    l.start = std::clamp(l.start, 0.0, kMaxSceneSeconds);
    l.end = std::clamp(l.end, 0.0, kMaxSceneSeconds);
    l.color = clean_color(l.color, "#ffffff");
    l.fill = clean_color(l.fill, "");
    if (l.kind == "box" && l.fill.empty()) l.fill = l.color;
    l.align = std::clamp(l.align, 0, 2);
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
    board.music_volume = std::clamp(board.music_volume, 0.0, 2.0);
    board.voice_volume = std::clamp(board.voice_volume, 0.0, 2.0);
    board.caption_y = board.caption_y <= 0 ? 0.0 : std::clamp(board.caption_y, 0.08, 0.95);
    board.caption_size = std::clamp(board.caption_size, 0.5, 2.0);
    board.caption_color = clean_color(board.caption_color, "#ffd84d");
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
        if (!listed(camera_ids(), s.camera)) s.camera = "auto";
        if (!listed(fit_ids(), s.fit)) s.fit = "auto";
        s.focus_x = std::clamp(s.focus_x, 0.0, 1.0);
        s.focus_y = std::clamp(s.focus_y, 0.0, 1.0);
        s.zoom = std::clamp(s.zoom, 1.0, 3.0);
        Look& lk = s.look;
        if (!listed(filter_ids(), lk.filter)) lk.filter = "none";
        lk.brightness = std::clamp(lk.brightness, -1.0, 1.0);
        lk.contrast = std::clamp(lk.contrast, -1.0, 1.0);
        lk.saturation = std::clamp(lk.saturation, -1.0, 1.0);
        lk.warmth = std::clamp(lk.warmth, -1.0, 1.0);
        lk.vignette = std::clamp(lk.vignette, 0.0, 1.0);
        if (s.layers.size() > static_cast<size_t>(kMaxLayers)) s.layers.resize(static_cast<size_t>(kMaxLayers));
        for (auto& l : s.layers) normalize_layer(l);
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

static Json layer_to_json(const Layer& l) {
    Json o = Json::obj();
    o.put("kind", Json::strv(l.kind));
    if (!l.text.empty()) o.put("text", Json::strv(l.text));
    if (!l.image.empty()) o.put("image", Json::strv(l.image));
    o.put("x", Json::numv(l.x));
    o.put("y", Json::numv(l.y));
    o.put("w", Json::numv(l.w));
    o.put("h", Json::numv(l.h));
    o.put("size", Json::numv(l.size));
    if (l.rotation != 0) o.put("rotation", Json::numv(l.rotation));
    o.put("color", Json::strv(l.color));
    if (!l.fill.empty()) o.put("fill", Json::strv(l.fill));
    o.put("opacity", Json::numv(l.opacity));
    if (l.radius > 0) o.put("radius", Json::numv(l.radius));
    o.put("start", Json::numv(l.start));
    o.put("end", Json::numv(l.end));
    o.put("in", Json::strv(l.in));
    o.put("out", Json::strv(l.out));
    o.put("bold", Json::boolv(l.bold));
    o.put("shadow", Json::boolv(l.shadow));
    o.put("align", Json::numv(l.align));
    return o;
}

static Layer layer_from_json(const Json& j) {
    Layer l;
    l.kind = j.str("kind", "text");
    l.text = j.str("text");
    l.image = j.str("image");
    l.x = j.num("x", 0.5);
    l.y = j.num("y", 0.5);
    l.w = j.num("w", 0.5);
    l.h = j.num("h", 0.12);
    l.size = j.num("size", 1.0);
    l.rotation = j.num("rotation", 0.0);
    l.color = j.str("color", "#ffffff");
    l.fill = j.str("fill");
    l.opacity = j.num("opacity", 1.0);
    l.radius = j.num("radius", 0.0);
    l.start = j.num("start", 0.0);
    l.end = j.num("end", 0.0);
    l.in = j.str("in", "fade");
    l.out = j.str("out", "fade");
    l.bold = j.boolean("bold", true);
    l.shadow = j.boolean("shadow", true);
    l.align = static_cast<int>(j.num("align", 1));
    return l;
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
    if (!board.music_file.empty()) j.put("music_file", Json::strv(board.music_file));
    if (board.music_volume != 1.0) j.put("music_volume", Json::numv(board.music_volume));
    if (board.voice_volume != 1.0) j.put("voice_volume", Json::numv(board.voice_volume));
    if (board.caption_y > 0) j.put("caption_y", Json::numv(board.caption_y));
    if (board.caption_size != 1.0) j.put("caption_size", Json::numv(board.caption_size));
    if (board.caption_color != "#ffd84d") j.put("caption_color", Json::strv(board.caption_color));
    if (!board.caption_upper) j.put("caption_upper", Json::boolv(false));
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
        if (s.camera != "auto") o.put("camera", Json::strv(s.camera));
        if (s.fit != "auto") o.put("fit", Json::strv(s.fit));
        if (s.focus_x != 0.5 || s.focus_y != 0.5) {
            o.put("focus_x", Json::numv(s.focus_x));
            o.put("focus_y", Json::numv(s.focus_y));
        }
        if (s.zoom != 1.0) o.put("zoom", Json::numv(s.zoom));
        if (s.look.active()) {
            Json lk = Json::obj();
            lk.put("filter", Json::strv(s.look.filter));
            lk.put("brightness", Json::numv(s.look.brightness));
            lk.put("contrast", Json::numv(s.look.contrast));
            lk.put("saturation", Json::numv(s.look.saturation));
            lk.put("warmth", Json::numv(s.look.warmth));
            lk.put("vignette", Json::numv(s.look.vignette));
            o.put("look", std::move(lk));
        }
        if (!s.layers.empty()) {
            Json arr = Json::arr();
            for (const auto& l : s.layers) arr.a.push_back(layer_to_json(l));
            o.put("layers", std::move(arr));
        }
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
    b.music_file = j.str("music_file");
    b.music_volume = j.num("music_volume", 1.0);
    b.voice_volume = j.num("voice_volume", 1.0);
    b.caption_y = j.num("caption_y", 0.0);
    b.caption_size = j.num("caption_size", 1.0);
    b.caption_color = j.str("caption_color", "#ffd84d");
    b.caption_upper = j.boolean("caption_upper", true);
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
            s.camera = item.str("camera", "auto");
            s.fit = item.str("fit", "auto");
            s.focus_x = item.num("focus_x", 0.5);
            s.focus_y = item.num("focus_y", 0.5);
            s.zoom = item.num("zoom", 1.0);
            if (const Json* lk = item.field("look"); lk && lk->type == Json::Obj) {
                s.look.filter = lk->str("filter", "none");
                s.look.brightness = lk->num("brightness", 0);
                s.look.contrast = lk->num("contrast", 0);
                s.look.saturation = lk->num("saturation", 0);
                s.look.warmth = lk->num("warmth", 0);
                s.look.vignette = lk->num("vignette", 0);
            }
            if (const Json* ls = item.field("layers"); ls && ls->type == Json::Arr)
                for (const auto& lj : ls->a)
                    if (lj.type == Json::Obj) s.layers.push_back(layer_from_json(lj));
            if (s.title.empty() && s.text.empty() && s.image.empty() && s.narration.empty() && s.layers.empty()) continue;
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
    auto resolve = [&](std::string& im) {
        bool absolute = (im.size() > 1 && im[1] == ':') || (!im.empty() && (im[0] == '\\' || im[0] == '/'));
        if (!im.empty() && !absolute && !dir.empty()) im = dir + im;
    };
    for (auto& s : board.scenes) {
        resolve(s.image);
        for (auto& l : s.layers) resolve(l.image);
    }
    resolve(board.music_file);
    return true;
}

}  // namespace kare
