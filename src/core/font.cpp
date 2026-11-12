#include "core/font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "core/utf8.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace kare {

namespace {

struct Face {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool ok = false;
    std::mutex mu;
    std::deque<Glyph> store;
    std::unordered_map<uint64_t, size_t> index;
};

Face g_regular;
Face g_bold;
const Glyph g_empty;
bool g_ready = false;
std::string g_error;

Face& face(bool bold) { return (bold && g_bold.ok) ? g_bold : g_regular; }

std::string join_path(const std::string& dir, const std::string& name) {
#ifdef _WIN32
    if (dir.empty()) return name;
    char last = dir.back();
    if (last == '\\' || last == '/') return dir + name;
    return dir + "\\" + name;
#else
    if (dir.empty()) return name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
#endif
}

bool load_face(Face& f, const std::string& path) {
    if (!read_file(path, f.bytes) || f.bytes.size() < 12) return false;
    int offset = stbtt_GetFontOffsetForIndex(f.bytes.data(), 0);
    if (offset < 0) return false;
    if (!stbtt_InitFont(&f.info, f.bytes.data(), offset)) return false;
    f.ok = true;
    return true;
}

bool load_named(Face& f, const std::string& file) {
    std::string dir = exe_dir();
    std::vector<std::string> paths = {join_path(join_path(dir, "fonts"), file), join_path(dir, file)};
    for (const auto& p : paths)
        if (load_face(f, p)) return true;
    return false;
}

}  // namespace

bool fonts_load(std::string& err) {
    static std::once_flag once;
    std::call_once(once, [] {
        bool regular = load_named(g_regular, "Roboto-Regular.ttf");
        load_named(g_bold, "Roboto-Bold.ttf");
        g_ready = regular;
        if (!regular) g_error = "Roboto-Regular.ttf not found next to the program (fonts folder)";
    });
    err = g_error;
    return g_ready;
}

bool fonts_ready() { return g_ready; }

const Glyph& glyph_for(bool bold, uint32_t cp, int px) {
    if (!g_ready) return g_empty;
    if (px < 8) px = 8;
    if (px > 320) px = 320;
    Face& f = face(bold);
    uint64_t key = (static_cast<uint64_t>(cp) << 16) | static_cast<unsigned>(px);
    std::lock_guard<std::mutex> lock(f.mu);
    auto it = f.index.find(key);
    if (it != f.index.end()) return f.store[it->second];
    Glyph g;
    float scale = stbtt_ScaleForPixelHeight(&f.info, static_cast<float>(px));
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f.info, static_cast<int>(cp), &adv, &lsb);
    g.advance = static_cast<int>(std::lround(adv * scale));
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBox(&f.info, static_cast<int>(cp), scale, scale, &x0, &y0, &x1, &y1);
    g.w = std::max(0, x1 - x0);
    g.h = std::max(0, y1 - y0);
    g.xoff = x0;
    g.yoff = y0;
    if (g.w > 0 && g.h > 0) {
        g.alpha.assign(static_cast<size_t>(g.w * g.h), 0);
        stbtt_MakeCodepointBitmap(&f.info, g.alpha.data(), g.w, g.h, g.w, scale, scale, static_cast<int>(cp));
    }
    f.store.push_back(std::move(g));
    size_t idx = f.store.size() - 1;
    f.index.emplace(key, idx);
    return f.store[idx];
}

int font_ascent(bool bold, int px) {
    if (!g_ready) return px;
    Face& f = face(bold);
    float scale = stbtt_ScaleForPixelHeight(&f.info, static_cast<float>(px));
    int a = 0, d = 0, g = 0;
    stbtt_GetFontVMetrics(&f.info, &a, &d, &g);
    return std::max(1, static_cast<int>(std::lround(a * scale)));
}

int font_kern(bool bold, uint32_t prev, uint32_t cp, int px) {
    if (!g_ready || !prev) return 0;
    Face& f = face(bold);
    float scale = stbtt_ScaleForPixelHeight(&f.info, static_cast<float>(px));
    int k = stbtt_GetCodepointKernAdvance(&f.info, static_cast<int>(prev), static_cast<int>(cp));
    return static_cast<int>(std::lround(k * scale));
}

static int line_height(bool bold, int px) {
    if (!g_ready) return px + 4;
    Face& f = face(bold);
    float scale = stbtt_ScaleForPixelHeight(&f.info, static_cast<float>(px));
    int a = 0, d = 0, g = 0;
    stbtt_GetFontVMetrics(&f.info, &a, &d, &g);
    int h = static_cast<int>(std::lround((a - d + g) * scale));
    return std::max(px, h);
}

int text_width(bool bold, const std::string& text, int px) {
    int w = 0;
    uint32_t prev = 0;
    size_t i = 0;
    uint32_t cp = 0;
    while (utf8_next(text, i, cp)) {
        if (prev) w += font_kern(bold, prev, cp, px);
        w += glyph_for(bold, cp, px).advance;
        prev = cp;
    }
    return w;
}

static std::vector<std::string> wrap_text(bool bold, const std::string& text, int px, int max_width) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    if (max_width < 8) max_width = 8;
    std::vector<std::string> words;
    std::string cur;
    for (uint32_t cp : utf8_cps(text)) {
        if (cp == ' ' || cp == '\n') {
            if (!cur.empty()) {
                words.push_back(cur);
                cur.clear();
            }
            if (cp == '\n') words.emplace_back("\n");
        } else {
            cur += cps_utf8(std::vector<uint32_t>{cp});
        }
    }
    if (!cur.empty()) words.push_back(cur);
    std::string line;
    for (const auto& w : words) {
        if (w == "\n") {
            if (!line.empty()) lines.push_back(line);
            line.clear();
            continue;
        }
        std::string trial = line.empty() ? w : line + " " + w;
        if (line.empty() || text_width(bold, trial, px) <= max_width) line = trial;
        else {
            lines.push_back(line);
            line = w;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

TextLayout layout_text(bool bold, const std::string& text, int px, int max_width, int max_lines) {
    TextLayout layout;
    layout.bold = bold;
    if (text.empty() || max_lines < 1) return layout;
    int size = std::max(12, px);
    for (int attempt = 0; attempt < 6; attempt++) {
        auto lines = wrap_text(bold, text, size, max_width);
        if (static_cast<int>(lines.size()) <= max_lines || size <= 16) {
            if (static_cast<int>(lines.size()) > max_lines) lines.resize(static_cast<size_t>(max_lines));
            layout.lines = std::move(lines);
            layout.px = size;
            layout.line_h = line_height(bold, size);
            layout.height = static_cast<int>(layout.lines.size()) * layout.line_h;
            return layout;
        }
        size = std::max(16, size * 4 / 5);
    }
    return layout;
}

}  // namespace kare
