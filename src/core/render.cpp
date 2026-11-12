#include "core/render.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "core/font.h"
#include "core/utf8.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace kare {

namespace {

constexpr float kPi = 3.14159265f;

struct Rgb {
    uint8_t r, g, b;
};

enum Deco { kBokeh, kStars, kWaves, kFireflies, kGrid, kPaper, kRings };

struct Pal {
    Rgb bg0, bg1, bg2, accent, accent2, ink, muted;
    bool light;
    Deco deco;
};

Pal palette_for(const std::string& style) {
    if (style == "dusk")
        return {{26, 12, 30}, {118, 48, 46}, {58, 22, 42}, {255, 192, 120}, {255, 98, 86}, {255, 246, 236}, {255, 212, 188}, false, kBokeh};
    if (style == "night")
        return {{5, 8, 20}, {16, 30, 66}, {9, 14, 36}, {124, 176, 255}, {152, 112, 255}, {236, 242, 255}, {164, 182, 216}, false, kStars};
    if (style == "sea")
        return {{3, 20, 28}, {10, 72, 86}, {5, 38, 50}, {108, 232, 210}, {58, 168, 224}, {236, 252, 248}, {160, 210, 204}, false, kWaves};
    if (style == "forest")
        return {{7, 18, 11}, {26, 58, 32}, {13, 32, 19}, {230, 198, 96}, {112, 184, 92}, {244, 242, 226}, {190, 204, 170}, false, kFireflies};
    if (style == "neon")
        return {{6, 4, 16}, {34, 8, 58}, {12, 6, 30}, {255, 46, 170}, {40, 230, 255}, {255, 246, 255}, {214, 182, 232}, false, kGrid};
    if (style == "paper")
        return {{241, 233, 218}, {222, 205, 180}, {233, 222, 202}, {178, 58, 40}, {64, 88, 112}, {34, 28, 24}, {98, 82, 68}, true, kPaper};
    if (style == "minimal")
        return {{247, 247, 247}, {226, 228, 232}, {238, 238, 241}, {255, 74, 48}, {40, 44, 52}, {16, 16, 18}, {96, 98, 104}, true, kRings};
    return {{11, 9, 8}, {40, 25, 16}, {20, 14, 11}, {242, 172, 82}, {255, 112, 62}, {250, 244, 236}, {204, 184, 162}, false, kBokeh};
}

float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
float ease_out(float p) {
    p = clamp01(p);
    float q = 1.f - p;
    return 1.f - q * q * q;
}
float ease_in_out(float p) {
    p = clamp01(p);
    return p < 0.5f ? 4.f * p * p * p : 1.f - std::pow(-2.f * p + 2.f, 3.f) * 0.5f;
}
float ease_expo(float p) {
    p = clamp01(p);
    return p >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * p);
}
float spring(float p) {
    p = clamp01(p);
    if (p >= 1.f) return 1.f;
    return 1.f - std::exp(-6.5f * p) * std::cos(11.f * p);
}
float smoothstep(float a, float b, float x) {
    float t = clamp01((x - a) / (b - a));
    return t * t * (3.f - 2.f * t);
}

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float rnd(uint32_t seed) { return static_cast<float>(hash32(seed) & 0xFFFFFFu) / 16777216.f; }

Rgb mix(Rgb a, Rgb b, float t) {
    t = clamp01(t);
    return {static_cast<uint8_t>(a.r + (b.r - a.r) * t), static_cast<uint8_t>(a.g + (b.g - a.g) * t),
            static_cast<uint8_t>(a.b + (b.b - a.b) * t)};
}
int luma(Rgb c) { return (c.r * 54 + c.g * 183 + c.b * 19) >> 8; }

const Rgb kWhite{255, 255, 255};
const Rgb kBlack{0, 0, 0};

inline void blend_px(uint8_t* p, Rgb c, int a) {
    if (a <= 0) return;
    if (a >= 255) {
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        return;
    }
    int ia = 255 - a;
    p[0] = static_cast<uint8_t>((c.r * a + p[0] * ia + 127) / 255);
    p[1] = static_cast<uint8_t>((c.g * a + p[1] * ia + 127) / 255);
    p[2] = static_cast<uint8_t>((c.b * a + p[2] * ia + 127) / 255);
}

inline void add_px(uint8_t* p, Rgb c, int a) {
    if (a <= 0) return;
    p[0] = static_cast<uint8_t>(std::min(255, p[0] + ((255 - p[0]) * c.r / 255) * a / 255));
    p[1] = static_cast<uint8_t>(std::min(255, p[1] + ((255 - p[1]) * c.g / 255) * a / 255));
    p[2] = static_cast<uint8_t>(std::min(255, p[2] + ((255 - p[2]) * c.b / 255) * a / 255));
}

inline uint8_t* px_at(Frame& f, int x, int y) { return &f.rgba[(static_cast<size_t>(y) * f.w + x) * 4]; }

float rr_sdf(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = std::fabs(px - cx) - (hw - r);
    float qy = std::fabs(py - cy) - (hh - r);
    float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

struct Box {
    int x0, y0, x1, y1;
};

Box clip_box(const Frame& f, float x, float y, float w, float h, float pad = 1.f) {
    Box b;
    b.x0 = std::max(0, static_cast<int>(std::floor(x - pad)));
    b.y0 = std::max(0, static_cast<int>(std::floor(y - pad)));
    b.x1 = std::min(f.w, static_cast<int>(std::ceil(x + w + pad)));
    b.y1 = std::min(f.h, static_cast<int>(std::ceil(y + h + pad)));
    return b;
}

void fill_rrect(Frame& f, float x, float y, float w, float h, float r, Rgb c, float alpha, const Rgb* c2 = nullptr) {
    if (w <= 0.f || h <= 0.f || alpha <= 0.002f) return;
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    r = std::min(r, std::min(hw, hh));
    Box b = clip_box(f, x, y, w, h);
    for (int yy = b.y0; yy < b.y1; yy++) {
        uint8_t* row = px_at(f, 0, yy);
        for (int xx = b.x0; xx < b.x1; xx++) {
            float d = rr_sdf(xx + 0.5f, yy + 0.5f, cx, cy, hw, hh, r);
            float cov = clamp01(0.5f - d);
            if (cov <= 0.f) continue;
            Rgb col = c2 ? mix(c, *c2, (xx + 0.5f - x) / w) : c;
            blend_px(row + xx * 4, col, static_cast<int>(cov * alpha * 255.f));
        }
    }
}

void stroke_rrect(Frame& f, float x, float y, float w, float h, float r, float th, Rgb c, float alpha) {
    if (w <= 0.f || h <= 0.f || alpha <= 0.002f) return;
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    r = std::min(r, std::min(hw, hh));
    Box b = clip_box(f, x, y, w, h, th + 1.f);
    for (int yy = b.y0; yy < b.y1; yy++) {
        uint8_t* row = px_at(f, 0, yy);
        for (int xx = b.x0; xx < b.x1; xx++) {
            float d = std::fabs(rr_sdf(xx + 0.5f, yy + 0.5f, cx, cy, hw, hh, r)) - th * 0.5f;
            float cov = clamp01(0.5f - d);
            if (cov > 0.f) blend_px(row + xx * 4, c, static_cast<int>(cov * alpha * 255.f));
        }
    }
}

void shadow_rrect(Frame& f, float x, float y, float w, float h, float r, float blur, float alpha) {
    if (alpha <= 0.002f) return;
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    r = std::min(r, std::min(hw, hh));
    Box b = clip_box(f, x, y, w, h, blur);
    for (int yy = b.y0; yy < b.y1; yy++) {
        uint8_t* row = px_at(f, 0, yy);
        for (int xx = b.x0; xx < b.x1; xx++) {
            float d = rr_sdf(xx + 0.5f, yy + 0.5f, cx, cy, hw, hh, r);
            float a = 1.f - smoothstep(-blur * 0.25f, blur, d);
            if (a > 0.f) blend_px(row + xx * 4, kBlack, static_cast<int>(a * a * alpha * 255.f));
        }
    }
}

void fill_circle(Frame& f, float cx, float cy, float rad, Rgb c, float alpha) {
    fill_rrect(f, cx - rad, cy - rad, rad * 2.f, rad * 2.f, rad, c, alpha);
}

void glow(Frame& f, float cx, float cy, float rad, Rgb c, float alpha, bool additive = true) {
    if (rad < 1.f || alpha <= 0.002f) return;
    Box b = clip_box(f, cx - rad, cy - rad, rad * 2.f, rad * 2.f, 0.f);
    float inv = 1.f / (rad * rad);
    for (int yy = b.y0; yy < b.y1; yy++) {
        float dy = yy + 0.5f - cy;
        uint8_t* row = px_at(f, 0, yy);
        for (int xx = b.x0; xx < b.x1; xx++) {
            float dx = xx + 0.5f - cx;
            float q = 1.f - (dx * dx + dy * dy) * inv;
            if (q <= 0.f) continue;
            int a = static_cast<int>(q * q * alpha * 255.f);
            if (additive) add_px(row + xx * 4, c, a);
            else blend_px(row + xx * 4, c, a);
        }
    }
}

void ring_arc(Frame& f, float cx, float cy, float R, float th, float sweep, Rgb c, float alpha) {
    if (sweep <= 0.f || alpha <= 0.002f) return;
    Box b = clip_box(f, cx - R - th, cy - R - th, (R + th) * 2.f, (R + th) * 2.f);
    float limit = clamp01(sweep) * 2.f * kPi;
    for (int yy = b.y0; yy < b.y1; yy++) {
        uint8_t* row = px_at(f, 0, yy);
        for (int xx = b.x0; xx < b.x1; xx++) {
            float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
            float d = std::fabs(std::sqrt(dx * dx + dy * dy) - R) - th * 0.5f;
            float cov = clamp01(0.5f - d);
            if (cov <= 0.f) continue;
            float ang = std::atan2(dx, -dy);
            if (ang < 0.f) ang += 2.f * kPi;
            if (ang > limit) continue;
            blend_px(row + xx * 4, c, static_cast<int>(cov * alpha * 255.f));
        }
    }
}

// ---------------------------------------------------------------- images

struct Level {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
};

struct Image {
    std::vector<Level> levels;
    int w() const { return levels.empty() ? 0 : levels[0].w; }
    int h() const { return levels.empty() ? 0 : levels[0].h; }
};

Level half_level(const Level& s) {
    Level d;
    d.w = std::max(1, s.w / 2);
    d.h = std::max(1, s.h / 2);
    d.px.resize(static_cast<size_t>(d.w) * d.h * 4);
    for (int y = 0; y < d.h; y++) {
        int y0 = std::min(s.h - 1, y * 2), y1 = std::min(s.h - 1, y * 2 + 1);
        for (int x = 0; x < d.w; x++) {
            int x0 = std::min(s.w - 1, x * 2), x1 = std::min(s.w - 1, x * 2 + 1);
            for (int k = 0; k < 4; k++) {
                int v = s.px[(static_cast<size_t>(y0) * s.w + x0) * 4 + k] + s.px[(static_cast<size_t>(y0) * s.w + x1) * 4 + k] +
                        s.px[(static_cast<size_t>(y1) * s.w + x0) * 4 + k] + s.px[(static_cast<size_t>(y1) * s.w + x1) * 4 + k];
                d.px[(static_cast<size_t>(y) * d.w + x) * 4 + k] = static_cast<uint8_t>((v + 2) / 4);
            }
        }
    }
    return d;
}

std::shared_ptr<const Image> get_image(const std::string& path) {
    static std::mutex mu;
    static std::unordered_map<std::string, std::shared_ptr<const Image>> cache;
    if (path.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    std::shared_ptr<Image> img;
    std::vector<unsigned char> bytes;
    if (read_file(path, bytes) && !bytes.empty()) {
        int w = 0, h = 0, n = 0;
        unsigned char* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
        if (data && w > 0 && h > 0) {
            img = std::make_shared<Image>();
            Level base;
            base.w = w;
            base.h = h;
            base.px.assign(data, data + static_cast<size_t>(w) * h * 4);
            while (base.w > 2560 || base.h > 2560) base = half_level(base);
            img->levels.push_back(std::move(base));
            while (img->levels.back().w > 48 && img->levels.back().h > 48) img->levels.push_back(half_level(img->levels.back()));
        }
        if (data) stbi_image_free(data);
    }
    cache[path] = img;
    return img;
}

void draw_image(Frame& f, const Image& im, float x, float y, float w, float h, float radius, float alpha) {
    if (w < 1.f || h < 1.f || alpha <= 0.002f || im.levels.empty()) return;
    size_t li = 0;
    while (li + 1 < im.levels.size() && im.levels[li + 1].w >= w && im.levels[li + 1].h >= h) li++;
    const Level& L = im.levels[li];
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    float r = std::min(radius, std::min(hw, hh));
    float sx = L.w / w, sy = L.h / h;
    Box b = clip_box(f, x, y, w, h);
    for (int yy = b.y0; yy < b.y1; yy++) {
        float v = (yy + 0.5f - y) * sy - 0.5f;
        int iy = static_cast<int>(std::floor(v));
        float fy = v - iy;
        int iy0 = std::clamp(iy, 0, L.h - 1), iy1 = std::clamp(iy + 1, 0, L.h - 1);
        const uint8_t* r0 = &L.px[static_cast<size_t>(iy0) * L.w * 4];
        const uint8_t* r1 = &L.px[static_cast<size_t>(iy1) * L.w * 4];
        uint8_t* row = px_at(f, 0, yy);
        for (int xx = b.x0; xx < b.x1; xx++) {
            float cov;
            if (r > 0.5f) cov = clamp01(0.5f - rr_sdf(xx + 0.5f, yy + 0.5f, cx, cy, hw, hh, r));
            else cov = clamp01(std::min(xx + 1.f - x, x + w - xx)) * clamp01(std::min(yy + 1.f - y, y + h - yy));
            if (cov <= 0.f) continue;
            float u = (xx + 0.5f - x) * sx - 0.5f;
            int ix = static_cast<int>(std::floor(u));
            float fx = u - ix;
            int ix0 = std::clamp(ix, 0, L.w - 1) * 4, ix1 = std::clamp(ix + 1, 0, L.w - 1) * 4;
            float s[4];
            for (int k = 0; k < 4; k++) {
                float top = r0[ix0 + k] + (r0[ix1 + k] - r0[ix0 + k]) * fx;
                float bot = r1[ix0 + k] + (r1[ix1 + k] - r1[ix0 + k]) * fx;
                s[k] = top + (bot - top) * fy;
            }
            int a = static_cast<int>(cov * alpha * s[3]);
            blend_px(row + xx * 4, Rgb{static_cast<uint8_t>(s[0]), static_cast<uint8_t>(s[1]), static_cast<uint8_t>(s[2])}, a);
        }
    }
}

// ---------------------------------------------------------------- text

std::string upper_tr(const std::string& s) {
    std::vector<uint32_t> out;
    for (uint32_t cp : utf8_cps(s)) {
        if (cp == 'i') cp = 0x130;
        else if (cp == 0x131) cp = 'I';
        else if (cp >= 'a' && cp <= 'z') cp -= 32;
        else if (cp == 0xE7) cp = 0xC7;
        else if (cp == 0x11F) cp = 0x11E;
        else if (cp == 0xF6) cp = 0xD6;
        else if (cp == 0x15F) cp = 0x15E;
        else if (cp == 0xFC) cp = 0xDC;
        out.push_back(cp);
    }
    return cps_utf8(out);
}

int draw_word(Frame& f, bool bold, const std::string& s, int px, int x, int top, Rgb c, float alpha, int clip0 = INT_MIN,
              int clip1 = INT_MAX, int max_chars = -1) {
    if (alpha <= 0.003f) return x + text_width(bold, s, px);
    int baseline = top + font_ascent(bold, px);
    int mul = static_cast<int>(clamp01(alpha) * 256.f);
    uint32_t prev = 0;
    size_t i = 0;
    uint32_t cp = 0;
    int n = 0;
    while (utf8_next(s, i, cp)) {
        if (max_chars >= 0 && n >= max_chars) break;
        if (prev) x += font_kern(bold, prev, cp, px);
        const Glyph& g = glyph_for(bold, cp, px);
        for (int gy = 0; gy < g.h; gy++) {
            int yy = baseline + g.yoff + gy;
            if (yy < 0 || yy >= f.h || yy < clip0 || yy >= clip1) continue;
            uint8_t* row = px_at(f, 0, yy);
            const unsigned char* src = &g.alpha[static_cast<size_t>(gy) * g.w];
            for (int gx = 0; gx < g.w; gx++) {
                int a = src[gx];
                if (!a) continue;
                int xx = x + g.xoff + gx;
                if (xx < 0 || xx >= f.w) continue;
                blend_px(row + xx * 4, c, (a * mul) >> 8);
            }
        }
        x += g.advance;
        prev = cp;
        n++;
    }
    return x;
}

int spaced_width(bool bold, const std::string& s, int px, int spacing) {
    int w = 0, n = 0;
    for (uint32_t cp : utf8_cps(s)) {
        w += glyph_for(bold, cp, px).advance;
        n++;
    }
    return w + std::max(0, n - 1) * spacing;
}

void draw_spaced(Frame& f, bool bold, const std::string& s, int px, int x, int top, int spacing, Rgb c, float alpha) {
    for (uint32_t cp : utf8_cps(s)) {
        std::string one = cps_utf8(std::vector<uint32_t>{cp});
        int nx = draw_word(f, bold, one, px, x, top, c, alpha);
        x = nx + spacing;
    }
}

struct WordT {
    std::string s;
    int w = 0;
    int chars = 0;
    bool hi = false;
    bool br = false;
};

struct LineT {
    int first = 0, last = 0, width = 0;
};

struct Block {
    bool bold = false;
    int px = 0, line_h = 0, space = 0, width = 0, height = 0;
    std::vector<WordT> words;
    std::vector<LineT> lines;
    int chars() const {
        int n = 0;
        for (const auto& w : words) n += w.chars + 1;
        return n;
    }
};

std::vector<WordT> tokenize(const std::string& text) {
    std::vector<WordT> out;
    std::vector<uint32_t> cur;
    bool hi = false, cur_hi = false;
    auto flush = [&] {
        if (cur.empty()) return;
        WordT w;
        w.s = cps_utf8(cur);
        w.chars = static_cast<int>(cur.size());
        w.hi = cur_hi;
        out.push_back(std::move(w));
        cur.clear();
    };
    for (uint32_t cp : utf8_cps(text)) {
        if (cp == '\n') {
            flush();
            WordT b;
            b.br = true;
            out.push_back(b);
            continue;
        }
        if (cp == ' ' || cp == '\t' || cp == '\r') {
            flush();
            continue;
        }
        if (cp == '*') {
            if (cur.empty()) {
                hi = true;
                cur_hi = true;
            } else {
                hi = false;
            }
            continue;
        }
        if (cur.empty()) cur_hi = hi;
        cur.push_back(cp);
    }
    flush();
    return out;
}

void wrap_block(Block& b, float max_w) {
    b.lines.clear();
    LineT cur;
    bool has = false;
    for (int i = 0; i < static_cast<int>(b.words.size()); i++) {
        const WordT& w = b.words[static_cast<size_t>(i)];
        if (w.br) {
            if (has) b.lines.push_back(cur);
            has = false;
            continue;
        }
        if (!has) {
            cur = {i, i + 1, w.w};
            has = true;
            continue;
        }
        int add = b.space + w.w;
        if (cur.width + add > max_w) {
            b.lines.push_back(cur);
            cur = {i, i + 1, w.w};
        } else {
            cur.last = i + 1;
            cur.width += add;
        }
    }
    if (has) b.lines.push_back(cur);
}

Block make_block(const std::string& text, bool bold, float px_f, float max_w, int max_lines, float min_px_f, float lead = 0.f) {
    Block b;
    b.bold = bold;
    b.words = tokenize(text);
    if (b.words.empty()) return b;
    int px = std::max(8, static_cast<int>(std::lround(px_f)));
    int min_px = std::max(8, static_cast<int>(std::lround(min_px_f)));
    for (int attempt = 0; attempt < 14; attempt++) {
        b.px = px;
        b.space = std::max(1, glyph_for(bold, ' ', px).advance);
        for (auto& w : b.words)
            if (!w.br) w.w = text_width(bold, w.s, px);
        wrap_block(b, max_w);
        bool fits = static_cast<int>(b.lines.size()) <= max_lines;
        for (const auto& l : b.lines)
            if (l.width > max_w) fits = false;
        if (fits || px <= min_px) break;
        px = std::max(min_px, static_cast<int>(px * 0.9f));
    }
    if (static_cast<int>(b.lines.size()) > max_lines) {
        b.lines.resize(static_cast<size_t>(max_lines));
        LineT& last = b.lines.back();
        WordT& w = b.words[static_cast<size_t>(last.last - 1)];
        w.s += "\xE2\x80\xA6";
        w.chars += 1;
        int nw = text_width(bold, w.s, b.px);
        last.width += nw - w.w;
        w.w = nw;
    }
    float k = lead > 0.f ? lead : (bold ? 1.14f : 1.36f);
    b.line_h = static_cast<int>(std::lround(b.px * k));
    b.height = static_cast<int>(b.lines.size()) * b.line_h;
    b.width = 0;
    for (const auto& l : b.lines) b.width = std::max(b.width, l.width);
    return b;
}

enum AnimKind { kRise, kFade, kType, kReveal, kSpring, kNone };

struct Motion {
    AnimKind kind = kRise;
    float delay = 0.f;
    float stagger = 0.06f;
    float cps = 30.f;
};

AnimKind anim_kind(const std::string& id) {
    if (id == "fade") return kFade;
    if (id == "type") return kType;
    if (id == "reveal") return kReveal;
    if (id == "spring") return kSpring;
    return kRise;
}

struct DrawOut {
    float end_x = 0.f, end_y = 0.f;
    bool typing = false;
};

DrawOut draw_block(Frame& f, const Block& b, float t, float x, float y, int align, Rgb ink, Rgb hi, float alpha,
                   const Motion& m, float shadow, float S) {
    DrawOut out;
    if (b.lines.empty()) return out;
    int k = 0;
    int typed_before = 0;
    float typed = (t - m.delay) * m.cps;
    for (size_t li = 0; li < b.lines.size(); li++) {
        const LineT& L = b.lines[li];
        float lx = align == 0 ? x : align == 1 ? x - L.width * 0.5f : x - L.width;
        int line_top = static_cast<int>(std::lround(y + static_cast<float>(li) * b.line_h));
        int cx = static_cast<int>(std::lround(lx));
        for (int wi = L.first; wi < L.last; wi++) {
            const WordT& w = b.words[static_cast<size_t>(wi)];
            if (w.br) continue;
            float a = alpha, dy = 0.f;
            int clip0 = INT_MIN, clip1 = INT_MAX, maxc = -1;
            switch (m.kind) {
                case kRise: {
                    float p = (t - (m.delay + k * m.stagger)) / 0.55f;
                    float e = ease_out(p);
                    dy = (1.f - e) * 0.55f * b.line_h;
                    a *= clamp01(p * 1.8f);
                    break;
                }
                case kSpring: {
                    float p = (t - (m.delay + k * m.stagger)) / 0.8f;
                    dy = (1.f - spring(p)) * 0.75f * b.line_h;
                    a *= clamp01(p * 3.f);
                    break;
                }
                case kFade: {
                    float p = (t - (m.delay + k * m.stagger)) / 0.7f;
                    a *= ease_out(p);
                    break;
                }
                case kReveal: {
                    float p = (t - (m.delay + static_cast<float>(li) * std::max(0.09f, m.stagger * 2.f))) / 0.75f;
                    dy = (1.f - ease_expo(p)) * b.line_h * 1.05f;
                    clip0 = line_top - b.px / 4;
                    clip1 = line_top + b.line_h + b.px / 8;
                    if (p <= 0.f) a = 0.f;
                    break;
                }
                case kType: {
                    int vis = static_cast<int>(typed) - typed_before;
                    maxc = std::clamp(vis, 0, w.chars);
                    if (maxc == 0) a = 0.f;
                    if (maxc > 0 && maxc < w.chars) out.typing = true;
                    break;
                }
                case kNone:
                    break;
            }
            int top = line_top + static_cast<int>(std::lround(dy));
            if (a > 0.003f) {
                if (shadow > 0.f)
                    draw_word(f, b.bold, w.s, b.px, cx, top + static_cast<int>(std::lround(2.f * S)), kBlack, a * shadow,
                              clip0, clip1, maxc);
                int ex = draw_word(f, b.bold, w.s, b.px, cx, top, w.hi ? hi : ink, a, clip0, clip1, maxc);
                if (m.kind == kType && maxc > 0) {
                    out.end_x = static_cast<float>(ex);
                    out.end_y = static_cast<float>(line_top);
                }
            }
            cx += w.w + b.space;
            typed_before += w.chars + 1;
            k++;
        }
    }
    if (m.kind == kType && typed < static_cast<float>(typed_before)) out.typing = true;
    return out;
}

float anim_end(const Block& b, const Motion& m) {
    int words = 0;
    for (const auto& w : b.words)
        if (!w.br) words++;
    switch (m.kind) {
        case kType:
            return m.delay + b.chars() / std::max(1.f, m.cps);
        case kReveal:
            return m.delay + static_cast<float>(b.lines.size()) * std::max(0.09f, m.stagger * 2.f) + 0.4f;
        default:
            return m.delay + words * m.stagger + 0.35f;
    }
}

// ---------------------------------------------------------------- scene context

struct Cam {
    float k = 1.f, ox = 0.f, oy = 0.f;
};

Cam camera_for(const std::string& motion, float u, float W, float H) {
    Cam c;
    float e = ease_in_out(u);
    if (motion == "zoom") c.k = 1.f + 0.09f * e;
    else if (motion == "rise") {
        c.oy = (1.f - ease_out(u)) * H * 0.045f;
        c.k = 1.03f;
    } else if (motion == "pan") {
        c.ox = (0.5f - e) * W * 0.07f;
        c.k = 1.06f;
    } else if (motion == "drift") {
        c.ox = std::sin(u * kPi) * W * 0.022f;
        c.oy = std::cos(u * 2.1f) * H * 0.012f;
        c.k = 1.02f + 0.02f * e;
    }
    return c;
}

struct Ctx {
    Frame& f;
    const Storyboard& b;
    const Scene& s;
    Pal pal;
    int index = 0, count = 1;
    float t = 0.f, ta = 0.f, dur = 1.f, T = 0.f;
    float S = 1.f, W = 0.f, H = 0.f;
    bool portrait = false, square = false;
    float sx = 0.f, sy = 0.f, sw = 0.f, sh = 0.f;
    float ox = 0.f, oy = 0.f;
    Cam cam;
    AnimKind anim = kRise;
    Rgb ink{}, body{}, hi{};
    float shadow = 0.f;
};

Motion motion_at(const Ctx& c, float delay, float stagger = 0.065f) {
    Motion m;
    m.kind = c.anim;
    m.delay = delay;
    m.stagger = stagger;
    m.cps = 34.f;
    return m;
}

float kicker_h(const Ctx& c) { return c.s.kicker.empty() ? 0.f : std::round(17.f * c.S) * 2.3f; }

void draw_kicker(Ctx& c, float x, float y, int align, float delay = 0.f) {
    if (c.s.kicker.empty()) return;
    int px = static_cast<int>(std::lround(17.f * c.S));
    int sp = static_cast<int>(std::lround(3.2f * c.S));
    std::string up = upper_tr(c.s.kicker);
    int tw = spaced_width(true, up, px, sp);
    float p = ease_out((c.ta - delay) / 0.6f);
    if (p <= 0.f) return;
    float dash = 28.f * c.S, gap = 12.f * c.S;
    float total = (align == 1 ? 0.f : dash + gap) + tw;
    float x0 = align == 0 ? x : align == 1 ? x - total * 0.5f : x - total;
    float slide = (1.f - p) * -16.f * c.S;
    if (align != 1) {
        fill_rrect(c.f, x0 + slide, y + px * 0.5f - 1.5f * c.S, dash * p, 3.f * c.S, 1.5f * c.S, c.pal.accent, p);
        x0 += dash + gap;
    }
    draw_spaced(c.f, true, up, px, static_cast<int>(x0 + slide), static_cast<int>(y), sp, c.pal.accent, p);
}

std::vector<std::string> text_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : text) {
        if (ch == '\n') {
            std::string t = tidy(cur);
            if (!t.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!tidy(cur).empty()) out.push_back(cur);
    for (auto& l : out) {
        size_t a = l.find_first_not_of(' ');
        size_t b = l.find_last_not_of(' ');
        l = a == std::string::npos ? std::string() : l.substr(a, b - a + 1);
    }
    return out;
}

std::vector<std::string> split_items(const std::string& text) {
    auto lines = text_lines(text);
    if (lines.size() >= 2) return lines;
    std::string t = lines.empty() ? text : lines[0];
    for (const char* sep : {" \xC2\xB7 ", "; ", " | ", " \xE2\x80\xA2 "}) {
        std::vector<std::string> parts;
        size_t start = 0;
        std::string s(sep);
        while (true) {
            size_t at = t.find(s, start);
            std::string part = tidy(t.substr(start, at == std::string::npos ? std::string::npos : at - start));
            if (!part.empty()) parts.push_back(part);
            if (at == std::string::npos) break;
            start = at + s.size();
        }
        if (parts.size() >= 2) return parts;
    }
    return lines;
}

std::string strip_bullet(const std::string& s) {
    auto cps = utf8_cps(s);
    size_t i = 0;
    while (i < cps.size() && cps[i] == ' ') i++;
    if (i + 1 < cps.size() && (cps[i] == '-' || cps[i] == '*' || cps[i] == 0x2022) && cps[i + 1] == ' ') {
        i++;
        while (i < cps.size() && cps[i] == ' ') i++;
    } else {
        size_t j = i;
        while (j < cps.size() && cps[j] >= '0' && cps[j] <= '9') j++;
        if (j > i && j < cps.size() && (cps[j] == '.' || cps[j] == ')')) {
            i = j + 1;
            while (i < cps.size() && cps[i] == ' ') i++;
        }
    }
    return cps_utf8(std::vector<uint32_t>(cps.begin() + static_cast<std::ptrdiff_t>(i), cps.end()));
}

bool starts_with(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

bool looks_command(const std::string& line) {
    static const char* heads[] = {"$ ",    "> ",     "git ",   "npm ",    "npx ",   "pip ",     "pip3 ",  "cargo ",
                                  "cmake", "go ",    "docker", "make",    "./",     "yarn ",    "pnpm ",  "brew ",
                                  "winget", "curl ", "python", "node ",   "dotnet", "gradle",   "mvn ",   "kare",
                                  "cd ",    "ls",    "sudo ",  "apt ",    "uv ",    "bun ",     "deno ",  "rustup "};
    for (const char* h : heads)
        if (starts_with(line, h)) return true;
    return false;
}

bool parse_bar(const std::string& line, std::string& label, double& value, bool& percent, bool* prefix = nullptr) {
    auto cps = utf8_cps(line);
    int end = static_cast<int>(cps.size()) - 1;
    while (end >= 0 && cps[static_cast<size_t>(end)] == ' ') end--;
    percent = false;
    if (prefix) *prefix = false;
    if (end >= 0 && cps[static_cast<size_t>(end)] == '%') {
        percent = true;
        end--;
    }
    int j = end;
    while (j >= 0 && ((cps[static_cast<size_t>(j)] >= '0' && cps[static_cast<size_t>(j)] <= '9') ||
                      cps[static_cast<size_t>(j)] == '.' || cps[static_cast<size_t>(j)] == ',')) j--;
    if (j != end && !percent && j >= 0 && cps[static_cast<size_t>(j)] == '%') {
        percent = true;
        if (prefix) *prefix = true;
        std::string num = cps_utf8(std::vector<uint32_t>(cps.begin() + j + 1, cps.begin() + end + 1));
        std::replace(num.begin(), num.end(), ',', '.');
        value = std::atof(num.c_str());
        label = tidy(cps_utf8(std::vector<uint32_t>(cps.begin(), cps.begin() + j)));
        while (!label.empty() && (label.back() == ':' || label.back() == '-' || label.back() == ' ')) label.pop_back();
        return !label.empty();
    }
    if (j == end) {
        size_t k = 0;
        while (k < cps.size() && cps[k] == ' ') k++;
        if (k < cps.size() && cps[k] == '%') {
            size_t d = k + 1, e = d;
            while (e < cps.size() && cps[e] >= '0' && cps[e] <= '9') e++;
            if (e == d) return false;
            std::string num = cps_utf8(std::vector<uint32_t>(cps.begin() + static_cast<std::ptrdiff_t>(d), cps.begin() + static_cast<std::ptrdiff_t>(e)));
            value = std::atof(num.c_str());
            label = tidy(cps_utf8(std::vector<uint32_t>(cps.begin() + static_cast<std::ptrdiff_t>(e), cps.end())));
            percent = true;
            if (prefix) *prefix = true;
            return !label.empty();
        }
        return false;
    }
    std::string num = cps_utf8(std::vector<uint32_t>(cps.begin() + j + 1, cps.begin() + end + 1));
    std::replace(num.begin(), num.end(), ',', '.');
    value = std::atof(num.c_str());
    label = tidy(cps_utf8(std::vector<uint32_t>(cps.begin(), cps.begin() + j + 1)));
    while (!label.empty() && (label.back() == ':' || label.back() == '-' || label.back() == ' ')) label.pop_back();
    return !label.empty();
}

struct Count {
    std::string prefix, suffix;
    double value = 0;
    int decimals = 0;
    char group = 0;
    bool ok = false;
};

Count parse_count(const std::string& s) {
    Count c;
    size_t i = 0;
    while (i < s.size() && !(s[i] >= '0' && s[i] <= '9')) i++;
    if (i >= s.size()) return c;
    size_t j = i;
    while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.' || s[j] == ',')) j++;
    while (j > i && (s[j - 1] == '.' || s[j - 1] == ',')) j--;
    std::string run = s.substr(i, j - i);
    c.prefix = s.substr(0, i);
    c.suffix = s.substr(j);
    size_t sep = run.find_last_of(".,");
    if (sep != std::string::npos && run.size() - sep - 1 == 3) {
        c.group = run[sep];
        std::string digits;
        for (char ch : run)
            if (ch >= '0' && ch <= '9') digits.push_back(ch);
        c.value = std::atof(digits.c_str());
    } else if (sep != std::string::npos) {
        if (run.find_first_of(".,") != sep) return c;
        c.decimals = static_cast<int>(run.size() - sep - 1);
        std::string v = run;
        v[sep] = '.';
        c.value = std::atof(v.c_str());
        c.group = run[sep] == ',' ? ',' : '.';
    } else {
        c.value = std::atof(run.c_str());
    }
    c.ok = c.value > 0 && run.size() <= 12;
    return c;
}

std::string format_count(const Count& c, double v) {
    char buf[64];
    std::string num;
    if (c.decimals > 0) {
        std::snprintf(buf, sizeof buf, "%.*f", c.decimals, v);
        num = buf;
        if (c.group == ',') std::replace(num.begin(), num.end(), '.', ',');
    } else {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
        num = buf;
        if (c.group) {
            std::string g;
            int n = static_cast<int>(num.size());
            for (int k = 0; k < n; k++) {
                g.push_back(num[static_cast<size_t>(k)]);
                int left = n - k - 1;
                if (left > 0 && left % 3 == 0) g.push_back(c.group);
            }
            num = g;
        }
    }
    return c.prefix + num + c.suffix;
}

// ---------------------------------------------------------------- background

void paint_background(Frame& f, const Pal& pal, float T, const Cam& cam) {
    const int w = f.w, h = f.h;
    Rgb lut[256];
    for (int i = 0; i < 256; i++) {
        float t = i / 255.f;
        lut[i] = t < 0.62f ? mix(pal.bg0, pal.bg1, t / 0.62f) : mix(pal.bg1, pal.bg2, (t - 0.62f) / 0.38f);
    }
    std::vector<float> colw(static_cast<size_t>(w)), vx(static_cast<size_t>(w)), vy(static_cast<size_t>(h));
    float vig = pal.light ? 0.16f : 0.5f;
    for (int x = 0; x < w; x++) {
        float fx = static_cast<float>(x) / static_cast<float>(w);
        colw[static_cast<size_t>(x)] = fx * 0.24f + 0.05f * std::sin(T * 0.37f + fx * 3.1f);
        float d = fx * 2.f - 1.f;
        vx[static_cast<size_t>(x)] = 1.f - vig * d * d * 0.75f;
    }
    for (int y = 0; y < h; y++) {
        float d = static_cast<float>(y) / static_cast<float>(h) * 2.f - 1.f;
        vy[static_cast<size_t>(y)] = 1.f - vig * d * d;
    }
    float md = static_cast<float>(std::max(w, h));
    float cx = w * 0.5f, cy = h * 0.5f;
    struct Orb {
        float x, y, inv, str;
        float r, g, b;
    } orbs[3];
    float base = pal.light ? 0.16f : 0.36f;
    auto make = [&](float px, float py, float rad, Rgb c, float str) {
        Orb o;
        o.x = cx + (px * w - cx) * cam.k + cam.ox * 0.6f;
        o.y = cy + (py * h - cy) * cam.k + cam.oy * 0.6f;
        float r = rad * md * cam.k;
        o.inv = 1.f / (r * r);
        o.str = str;
        o.r = c.r;
        o.g = c.g;
        o.b = c.b;
        return o;
    };
    orbs[0] = make(0.24f + 0.08f * std::sin(T * 0.23f), 0.28f + 0.08f * std::cos(T * 0.19f), 0.46f, pal.accent, base);
    orbs[1] = make(0.80f + 0.07f * std::cos(T * 0.17f), 0.76f + 0.07f * std::sin(T * 0.21f), 0.40f, pal.accent2, base * 0.85f);
    orbs[2] = make(0.55f + 0.18f * std::sin(T * 0.11f), 0.05f + 0.06f * std::cos(T * 0.13f), 0.30f, mix(pal.accent, pal.ink, 0.4f),
                   base * 0.45f);
    for (int y = 0; y < h; y++) {
        uint8_t* row = px_at(f, 0, y);
        float ty = static_cast<float>(y) / static_cast<float>(h) * 0.76f;
        float vyy = vy[static_cast<size_t>(y)];
        float dys[3];
        for (int k = 0; k < 3; k++) dys[k] = (y - orbs[k].y) * (y - orbs[k].y);
        for (int x = 0; x < w; x++) {
            float t = ty + colw[static_cast<size_t>(x)];
            int idx = static_cast<int>(t * 255.f);
            idx = idx < 0 ? 0 : (idx > 255 ? 255 : idx);
            float r = lut[idx].r, g = lut[idx].g, b = lut[idx].b;
            for (int k = 0; k < 3; k++) {
                float dx = x - orbs[k].x;
                float q = 1.f - (dx * dx + dys[k]) * orbs[k].inv;
                if (q <= 0.f) continue;
                q = q * q * orbs[k].str;
                r += (orbs[k].r - r) * q;
                g += (orbs[k].g - g) * q;
                b += (orbs[k].b - b) * q;
            }
            float v = vx[static_cast<size_t>(x)] * vyy;
            uint8_t* p = row + x * 4;
            p[0] = static_cast<uint8_t>(r * v);
            p[1] = static_cast<uint8_t>(g * v);
            p[2] = static_cast<uint8_t>(b * v);
            p[3] = 255;
        }
    }
}

void paint_deco(Frame& f, const Pal& pal, float T, const Cam& cam, float S) {
    const float W = static_cast<float>(f.w), H = static_cast<float>(f.h);
    const float cx = W * 0.5f, cy = H * 0.5f;
    auto tx = [&](float x) { return cx + (x - cx) * cam.k + cam.ox; };
    auto ty = [&](float y) { return cy + (y - cy) * cam.k + cam.oy; };
    const float md = std::min(W, H);
    switch (pal.deco) {
        case kBokeh: {
            for (int i = 0; i < 16; i++) {
                float rad = md * (0.018f + 0.05f * rnd(i * 7 + 1));
                float sp = 0.012f + 0.02f * rnd(i * 7 + 2);
                float x = std::fmod(rnd(i * 7 + 3) * 1.3f + T * sp * 0.4f, 1.3f) - 0.15f;
                float y = 1.15f - std::fmod(rnd(i * 7 + 4) * 1.3f + T * sp, 1.3f);
                Rgb c = i % 3 == 0 ? pal.accent2 : (i % 3 == 1 ? pal.accent : pal.ink);
                float a = 0.05f + 0.07f * rnd(i * 7 + 5);
                glow(f, tx(x * W), ty(y * H), rad * 1.6f, c, a * 0.8f);
                fill_circle(f, tx(x * W), ty(y * H), rad, c, a);
            }
            break;
        }
        case kStars: {
            for (int i = 0; i < 140; i++) {
                float x = rnd(i * 5 + 1), y = rnd(i * 5 + 2);
                x = std::fmod(x + T * 0.004f * (0.5f + rnd(i * 5 + 3)), 1.f);
                float tw = 0.5f + 0.5f * std::sin(T * (1.2f + 2.f * rnd(i * 5 + 4)) + i);
                float size = (0.7f + 1.3f * rnd(i * 5 + 5)) * S;
                float px = tx(x * W), py = ty(y * H);
                if (size > 1.4f * S) glow(f, px, py, size * 4.f, pal.accent, 0.25f * tw);
                fill_circle(f, px, py, size, pal.ink, 0.25f + 0.6f * tw);
            }
            break;
        }
        case kWaves: {
            for (int k = 0; k < 6; k++) {
                float base = H * (0.62f + k * 0.065f);
                float amp = H * (0.012f + 0.006f * k);
                float freq = (2.2f + k * 0.4f) / W * kPi;
                float ph = T * (0.5f + 0.12f * k) + k;
                float a = 0.10f + 0.03f * k;
                Rgb c = k % 2 ? pal.accent : pal.accent2;
                for (int x = 0; x < f.w; x++) {
                    float yv = ty(base + std::sin(x * freq + ph) * amp + std::sin(x * freq * 2.3f - ph * 1.3f) * amp * 0.35f);
                    float xx = tx(static_cast<float>(x));
                    int ix = static_cast<int>(xx);
                    if (ix < 0 || ix >= f.w) continue;
                    int y0 = static_cast<int>(std::floor(yv - 1.5f * S)), y1 = static_cast<int>(std::ceil(yv + 1.5f * S));
                    for (int y = std::max(0, y0); y <= std::min(f.h - 1, y1); y++) {
                        float d = std::fabs(y + 0.5f - yv) / (1.2f * S);
                        float cov = clamp01(1.f - d);
                        if (cov > 0.f) blend_px(px_at(f, ix, y), c, static_cast<int>(cov * a * 255.f));
                    }
                }
            }
            break;
        }
        case kFireflies: {
            for (int i = 0; i < 34; i++) {
                float x = rnd(i * 9 + 1) + 0.04f * std::sin(T * (0.4f + rnd(i * 9 + 2)) + i);
                float y = rnd(i * 9 + 3) + 0.05f * std::cos(T * (0.3f + rnd(i * 9 + 4)) + i * 2);
                float pulse = 0.5f + 0.5f * std::sin(T * (1.5f + 2.f * rnd(i * 9 + 5)) + i * 3);
                float px = tx(x * W), py = ty(y * H);
                glow(f, px, py, (10.f + 10.f * rnd(i * 9 + 6)) * S, pal.accent, 0.5f * pulse);
                fill_circle(f, px, py, 1.6f * S, mix(pal.accent, kWhite, 0.5f), 0.4f + 0.5f * pulse);
            }
            break;
        }
        case kGrid: {
            float horizon = H * 0.62f;
            float vpx = tx(cx);
            Rgb c = pal.accent2;
            for (int y = static_cast<int>(horizon); y < f.h; y++) {
                float depth = (y - horizon) / (H - horizon);
                float z = 1.f / std::max(0.02f, depth);
                float fz = std::fmod(z * 0.9f - T * 1.6f, 1.f);
                if (fz < 0.f) fz += 1.f;
                float hline = std::fabs(fz - 0.5f) < 0.035f * z * 0.25f + 0.01f ? 1.f : 0.f;
                float fade = clamp01(depth * 1.6f);
                uint8_t* row = px_at(f, 0, y);
                for (int x = 0; x < f.w; x++) {
                    float gx = (x - vpx) / std::max(1.f, (y - horizon)) * 0.9f;
                    float fg = std::fabs(gx * 3.f - std::round(gx * 3.f));
                    float v = fg < 0.03f ? 1.f : 0.f;
                    float a = std::max(v, hline) * 0.32f * fade;
                    if (a > 0.f) add_px(row + x * 4, c, static_cast<int>(a * 255.f));
                }
            }
            glow(f, vpx, ty(horizon), W * 0.32f, pal.accent, 0.35f);
            fill_rrect(f, 0.f, horizon - 1.f * S, W, 2.f * S, 0.f, pal.accent, 0.55f);
            break;
        }
        case kPaper: {
            float step = md * 0.045f;
            Rgb c = pal.accent2;
            for (float x = std::fmod(tx(0.f), step); x < W; x += step)
                fill_rrect(f, x, 0.f, std::max(1.f, 0.8f * S), H, 0.f, c, 0.06f);
            for (float y = std::fmod(ty(0.f), step); y < H; y += step)
                fill_rrect(f, 0.f, y, W, std::max(1.f, 0.8f * S), 0.f, c, 0.06f);
            break;
        }
        case kRings: {
            float rx = tx(W * 0.82f), ry = ty(H * 0.3f);
            for (int k = 0; k < 4; k++) {
                float R = md * (0.16f + k * 0.11f) * (1.f + 0.02f * std::sin(T * 0.6f + k));
                ring_arc(f, rx, ry, R, 1.4f * S, 1.f, pal.accent2, 0.10f);
            }
            ring_arc(f, rx, ry, md * 0.27f, 3.f * S, 0.25f + 0.1f * std::sin(T * 0.4f), pal.accent, 0.6f);
            break;
        }
    }
}

// ---------------------------------------------------------------- layouts

void draw_statement(Ctx& c);

void draw_hero(Ctx& c) {
    float maxw = c.sw * (c.portrait ? 1.f : 0.86f);
    float tpx = (c.portrait ? 78.f : c.square ? 76.f : 92.f) * c.S;
    Block title = make_block(c.s.title, true, tpx, maxw, 3, 40.f * c.S);
    Block body = make_block(c.s.text, false, (c.portrait ? 32.f : 30.f) * c.S, maxw * 0.86f, 3, 20.f * c.S);
    float kh = kicker_h(c);
    float bar = title.height ? 30.f * c.S : 0.f;
    float gap = body.height ? 20.f * c.S : 0.f;
    float total = kh + title.height + bar + gap + body.height;
    float x = c.W * 0.5f + c.ox;
    float y = c.sy + (c.sh - total) * 0.5f + c.oy;
    draw_kicker(c, x, y, 1);
    y += kh;
    Motion mt = motion_at(c, 0.12f, 0.075f);
    draw_block(c.f, title, c.ta, x, y, 1, c.ink, c.hi, 1.f, mt, c.shadow, c.S);
    y += title.height;
    float tend = std::min(1.4f, anim_end(title, mt));
    if (title.height) {
        float p = ease_out((c.ta - tend + 0.25f) / 0.7f);
        float bw = 120.f * c.S * p;
        fill_rrect(c.f, x - bw * 0.5f, y + 12.f * c.S, bw, 5.f * c.S, 2.5f * c.S, c.pal.accent, p, &c.pal.accent2);
        y += bar;
    }
    y += gap;
    Motion mb = motion_at(c, tend, 0.03f);
    if (c.anim == kSpring) mb.kind = kRise;
    draw_block(c.f, body, c.ta, x, y, 1, c.body, c.hi, 1.f, mb, c.shadow, c.S);
}

void draw_statement(Ctx& c) {
    float bar_x = c.sx + c.ox;
    float col = bar_x + 34.f * c.S;
    float maxw = c.portrait ? c.sw - 34.f * c.S : c.sw * 0.74f;
    Block title = make_block(c.s.title, true, (c.portrait ? 60.f : 64.f) * c.S, maxw, 3, 32.f * c.S);
    Block body = make_block(c.s.text, false, (c.portrait ? 32.f : 31.f) * c.S, maxw, c.portrait ? 7 : 5, 20.f * c.S);
    float kh = kicker_h(c);
    float gap = title.height && body.height ? 22.f * c.S : 0.f;
    float total = kh + title.height + gap + body.height;
    float y = c.sy + (c.sh - total) * 0.5f + c.oy;
    float p = ease_out(c.ta / 0.8f);
    fill_rrect(c.f, bar_x, y, 5.f * c.S, total * p, 2.5f * c.S, c.pal.accent, 1.f, nullptr);
    draw_kicker(c, col, y, 0);
    y += kh;
    Motion mt = motion_at(c, 0.15f);
    draw_block(c.f, title, c.ta, col, y, 0, c.ink, c.hi, 1.f, mt, c.shadow, c.S);
    y += title.height + gap;
    float tend = title.height ? std::min(1.2f, anim_end(title, mt) - 0.1f) : 0.2f;
    Motion mb = motion_at(c, tend, 0.025f);
    if (c.anim == kSpring) mb.kind = kRise;
    draw_block(c.f, body, c.ta, col, y, 0, c.body, c.hi, 1.f, mb, c.shadow, c.S);
}

void draw_list(Ctx& c) {
    std::vector<std::string> items;
    for (const auto& it : split_items(c.s.text)) {
        std::string s = strip_bullet(it);
        if (!s.empty()) items.push_back(s);
        if (items.size() >= 6) break;
    }
    if (items.empty()) {
        draw_statement(c);
        return;
    }
    float x = c.sx + c.ox;
    float maxw = c.sw * (c.portrait ? 1.f : 0.82f);
    Block title = make_block(c.s.title, true, (c.portrait ? 52.f : 50.f) * c.S, maxw, 2, 30.f * c.S);
    float ipx = (c.portrait ? 34.f : 32.f) * c.S;
    float badge = 0.f;
    std::vector<Block> blocks;
    for (int attempt = 0; attempt < 6; attempt++) {
        badge = ipx * 1.35f;
        blocks.clear();
        bool ok = true;
        for (const auto& it : items) {
            blocks.push_back(make_block(it, false, ipx, maxw - badge - 22.f * c.S, 2, ipx));
            if (blocks.back().lines.size() > 2) ok = false;
        }
        float rows = 0.f;
        for (const auto& b : blocks) rows += std::max(badge, static_cast<float>(b.height)) + 18.f * c.S;
        if (ok && rows + title.height + kicker_h(c) + 40.f * c.S <= c.sh) break;
        ipx *= 0.9f;
    }
    float kh = kicker_h(c);
    float gap = title.height ? 34.f * c.S : 0.f;
    float rows_h = 0.f;
    std::vector<float> row_h;
    for (const auto& b : blocks) {
        row_h.push_back(std::max(badge, static_cast<float>(b.height)));
        rows_h += row_h.back();
    }
    float rgap = 18.f * c.S;
    rows_h += rgap * static_cast<float>(blocks.size() - 1);
    float total = kh + title.height + gap + rows_h;
    float y = c.sy + std::max(0.f, (c.sh - total) * 0.5f) + c.oy;
    draw_kicker(c, x, y, 0);
    y += kh;
    Motion mt = motion_at(c, 0.1f);
    draw_block(c.f, title, c.ta, x, y, 0, c.ink, c.hi, 1.f, mt, c.shadow, c.S);
    y += title.height + gap;
    float window = std::max(0.9f, c.dur * 0.55f);
    float step = std::min(0.55f, window / static_cast<float>(blocks.size()));
    bool dark_num = luma(c.pal.accent) > 140;
    for (size_t i = 0; i < blocks.size(); i++) {
        float start = 0.4f + static_cast<float>(i) * step;
        float p = ease_out((c.ta - start) / 0.5f);
        if (p > 0.f) {
            float dx = (1.f - p) * -34.f * c.S;
            float cyr = y + row_h[i] * 0.5f;
            float br = badge * 0.5f;
            if (c.pal.light) fill_circle(c.f, x + br + dx, cyr, br, c.pal.accent, p);
            else {
                glow(c.f, x + br + dx, cyr, br * 2.2f, c.pal.accent, 0.25f * p);
                fill_circle(c.f, x + br + dx, cyr, br, c.pal.accent, p);
            }
            std::string num = std::to_string(i + 1);
            int npx = static_cast<int>(badge * 0.5f);
            int nw = text_width(true, num, npx);
            draw_word(c.f, true, num, npx, static_cast<int>(x + br + dx - nw * 0.5f), static_cast<int>(cyr - npx * 0.58f),
                      dark_num ? c.pal.bg0 : kWhite, p);
            Motion mi;
            mi.kind = kNone;
            draw_block(c.f, blocks[i], c.ta, x + badge + 22.f * c.S + dx, cyr - blocks[i].height * 0.5f, 0, c.ink, c.hi, p, mi,
                       c.shadow, c.S);
        }
        y += row_h[i] + rgap;
    }
}

void draw_stat(Ctx& c) {
    Count cnt = parse_count(c.s.title);
    float p = ease_expo((c.ta - 0.15f) / 1.4f);
    std::string shown = cnt.ok ? format_count(cnt, cnt.value * p) : c.s.title;
    float maxw = c.sw;
    Block fin = make_block(c.s.title, true, (c.portrait ? 150.f : 172.f) * c.S, maxw, 1, 54.f * c.S, 1.05f);
    Block body = make_block(c.s.text, false, (c.portrait ? 36.f : 34.f) * c.S, c.sw * 0.82f, 2, 22.f * c.S);
    float kh = kicker_h(c);
    float gap = body.height ? 20.f * c.S : 0.f;
    bool pct = c.s.title.find('%') != std::string::npos && cnt.ok && cnt.value <= 100.0;
    float R = std::max(fin.width * 0.5f, fin.px * 0.62f) + 34.f * c.S;
    float num_h = static_cast<float>(fin.height);
    if (pct && R * 2.f + 24.f * c.S + kh + gap + body.height < c.sh) num_h = R * 2.f + 24.f * c.S;
    else pct = false;
    float total = kh + num_h + gap + body.height;
    float x = c.W * 0.5f + c.ox;
    float y = c.sy + (c.sh - total) * 0.5f + c.oy;
    draw_kicker(c, x, y, 1);
    y += kh;
    float ncy = y + num_h * 0.5f;
    float pop = spring((c.ta - 0.05f) / 0.8f);
    float a = clamp01((c.ta - 0.05f) * 4.f);
    if (!c.pal.light) glow(c.f, x, ncy, std::max(fin.width * 0.75f, fin.px * 1.2f), c.pal.accent, 0.22f * a);
    if (pct) {
        ring_arc(c.f, x, ncy, R, 7.f * c.S, 1.f, c.ink, 0.10f * a);
        ring_arc(c.f, x, ncy, R, 7.f * c.S, static_cast<float>(cnt.value / 100.0) * p, c.pal.accent, a);
    }
    int fw = fin.width;
    int top = static_cast<int>(ncy - fin.height * 0.5f + (1.f - pop) * 40.f * c.S);
    if (c.shadow > 0.f)
        draw_word(c.f, true, shown, fin.px, static_cast<int>(x - fw * 0.5f), top + static_cast<int>(3.f * c.S), kBlack, a * c.shadow);
    draw_word(c.f, true, shown, fin.px, static_cast<int>(x - fw * 0.5f), top, c.pal.accent, a);
    y += num_h + gap;
    Motion mb = motion_at(c, 0.55f, 0.04f);
    if (mb.kind == kSpring || mb.kind == kType) mb.kind = kRise;
    draw_block(c.f, body, c.ta, x, y, 1, c.ink, c.hi, 1.f, mb, c.shadow, c.S);
}

std::string strip_quotes(const std::string& s) {
    auto cps = utf8_cps(s);
    auto is_q = [](uint32_t cp) { return cp == '"' || cp == 0x201C || cp == 0x201D || cp == 0xAB || cp == 0xBB || cp == '\''; };
    while (!cps.empty() && (is_q(cps.front()) || cps.front() == ' ')) cps.erase(cps.begin());
    while (!cps.empty() && (is_q(cps.back()) || cps.back() == ' ')) cps.pop_back();
    return cps_utf8(cps);
}

void draw_quote(Ctx& c) {
    std::string q = strip_quotes(!c.s.text.empty() ? c.s.text : c.s.title);
    std::string who = !c.s.text.empty() ? c.s.title : "";
    float maxw = c.sw * (c.portrait ? 1.f : 0.8f);
    Block body = make_block(q, false, (c.portrait ? 46.f : 46.f) * c.S, maxw, 6, 24.f * c.S, 1.3f);
    int wpx = static_cast<int>(std::lround(22.f * c.S));
    float markh = 120.f * c.S;
    float total = markh + body.height + (who.empty() ? 0.f : 46.f * c.S);
    float x = c.W * 0.5f + c.ox;
    float y = c.sy + (c.sh - total) * 0.5f + c.oy;
    float pm = ease_out(c.ta / 0.6f);
    int mpx = static_cast<int>(240.f * c.S);
    const char* mark = "\xE2\x80\x9C";
    int mw = text_width(true, mark, mpx);
    draw_word(c.f, true, mark, mpx, static_cast<int>(x - mw * 0.5f), static_cast<int>(y - mpx * 0.28f + (1.f - pm) * 20.f * c.S),
              c.pal.accent, 0.85f * pm);
    y += markh;
    Motion mq = motion_at(c, 0.25f, 0.05f);
    draw_block(c.f, body, c.ta, x, y, 1, c.ink, c.hi, 1.f, mq, c.shadow, c.S);
    y += body.height + 24.f * c.S;
    if (!who.empty()) {
        float pa = ease_out((c.ta - std::min(1.6f, anim_end(body, mq))) / 0.6f);
        std::string line = "\xE2\x80\x94  " + who;
        int ww = text_width(true, line, wpx);
        draw_word(c.f, true, line, wpx, static_cast<int>(x - ww * 0.5f), static_cast<int>(y), c.pal.accent, pa);
    }
}

void draw_split(Ctx& c) {
    if (c.portrait || c.square) {
        float maxw = c.sw;
        Block title = make_block(c.s.title, true, 58.f * c.S, maxw, 3, 30.f * c.S);
        Block body = make_block(c.s.text, false, 31.f * c.S, maxw, 8, 20.f * c.S);
        float kh = kicker_h(c);
        float total = kh + title.height + 54.f * c.S + body.height;
        float x = c.sx + c.ox;
        float y = c.sy + (c.sh - total) * 0.5f + c.oy;
        draw_kicker(c, x, y, 0);
        y += kh;
        Motion mt = motion_at(c, 0.1f);
        draw_block(c.f, title, c.ta, x, y, 0, c.ink, c.hi, 1.f, mt, c.shadow, c.S);
        y += title.height + 24.f * c.S;
        float p = ease_out((c.ta - 0.3f) / 0.7f);
        fill_rrect(c.f, x, y, maxw * 0.4f * p, 3.f * c.S, 1.5f * c.S, c.pal.accent, 1.f, &c.pal.accent2);
        y += 30.f * c.S;
        draw_block(c.f, body, c.ta, x, y, 0, c.body, c.hi, 1.f, motion_at(c, 0.45f, 0.02f), c.shadow, c.S);
        return;
    }
    float lw = c.sw * 0.42f, rx = c.sx + c.sw * 0.54f + c.ox, rw = c.sw * 0.46f;
    float lx = c.sx + c.ox;
    Block title = make_block(c.s.title, true, 62.f * c.S, lw, 4, 32.f * c.S);
    Block body = make_block(c.s.text, false, 30.f * c.S, rw, 8, 19.f * c.S);
    float kh = kicker_h(c);
    float lt = kh + title.height;
    float cy = c.sy + c.sh * 0.5f + c.oy;
    float y = cy - lt * 0.5f;
    draw_kicker(c, lx, y, 0);
    Motion mt = motion_at(c, 0.1f);
    draw_block(c.f, title, c.ta, lx, y + kh, 0, c.ink, c.hi, 1.f, mt, c.shadow, c.S);
    float p = ease_out((c.ta - 0.2f) / 0.8f);
    float dh = std::max(lt, static_cast<float>(body.height)) * 0.6f + 40.f * c.S;
    fill_rrect(c.f, c.sx + c.sw * 0.48f + c.ox, cy - dh * p, 3.f * c.S, dh * 2.f * p, 1.5f * c.S, c.pal.accent, 0.9f);
    Motion mb = motion_at(c, 0.45f, 0.02f);
    if (mb.kind == kSpring) mb.kind = kRise;
    draw_block(c.f, body, c.ta, rx, cy - body.height * 0.5f, 0, c.body, c.hi, 1.f, mb, c.shadow, c.S);
}

void draw_terminal(Ctx& c) {
    auto lines = text_lines(c.s.text);
    if (lines.empty()) lines.push_back(c.s.text);
    if (lines.size() > 9) lines.resize(9);
    float cw = c.portrait ? c.sw : std::min(c.sw, 960.f * c.S);
    float pad = 30.f * c.S;
    float px = 25.f * c.S;
    int longest = 0;
    for (int attempt = 0; attempt < 6; attempt++) {
        longest = 0;
        for (const auto& l : lines) longest = std::max(longest, text_width(false, l, static_cast<int>(px)));
        if (longest <= cw - pad * 2.f || px <= 15.f * c.S) break;
        px *= 0.9f;
    }
    int ipx = static_cast<int>(std::lround(px));
    float lh = px * 1.62f;
    float header = 44.f * c.S;
    float ch = header + pad * 0.8f + lh * static_cast<float>(lines.size()) + pad * 0.7f;
    Block title = make_block(c.s.title, true, (c.portrait ? 50.f : 46.f) * c.S, c.sw, 2, 28.f * c.S);
    float kh = kicker_h(c);
    float gap = title.height ? 30.f * c.S : 0.f;
    float total = kh + title.height + gap + ch;
    float x = c.W * 0.5f + c.ox;
    float y = c.sy + std::max(0.f, (c.sh - total) * 0.5f) + c.oy;
    draw_kicker(c, x, y, 1);
    y += kh;
    Motion mt = motion_at(c, 0.05f);
    if (mt.kind == kType) mt.kind = kRise;
    draw_block(c.f, title, c.ta, x, y, 1, c.ink, c.hi, 1.f, mt, c.shadow, c.S);
    y += title.height + gap;
    float pe = ease_out((c.ta - 0.15f) / 0.6f);
    float cy0 = y + (1.f - pe) * 30.f * c.S;
    float cx0 = x - cw * 0.5f;
    float r = 14.f * c.S;
    shadow_rrect(c.f, cx0, cy0 + 14.f * c.S, cw, ch, r, 50.f * c.S, 0.55f * pe);
    fill_rrect(c.f, cx0, cy0, cw, ch, r, Rgb{11, 14, 20}, 0.96f * pe);
    fill_rrect(c.f, cx0, cy0, cw, header, r, Rgb{22, 27, 36}, pe);
    fill_rrect(c.f, cx0, cy0 + header - r, cw, r, 0.f, Rgb{22, 27, 36}, pe);
    stroke_rrect(c.f, cx0, cy0, cw, ch, r, 1.2f * c.S, kWhite, 0.10f * pe);
    const Rgb dots[3] = {{255, 95, 86}, {255, 189, 46}, {39, 201, 63}};
    for (int i = 0; i < 3; i++) fill_circle(c.f, cx0 + 24.f * c.S + i * 20.f * c.S, cy0 + header * 0.5f, 6.f * c.S, dots[i], pe);
    std::string head = c.s.kicker.empty() ? "terminal" : c.s.kicker;
    int hpx = static_cast<int>(15.f * c.S);
    int hw = text_width(false, head, hpx);
    draw_word(c.f, false, head, hpx, static_cast<int>(x - hw * 0.5f), static_cast<int>(cy0 + (header - hpx * 1.1f) * 0.5f),
              Rgb{140, 150, 166}, pe);
    Rgb ink{232, 238, 246}, dim{132, 144, 160};
    Rgb acc = c.pal.light ? mix(c.pal.accent, kWhite, 0.35f) : c.pal.accent;
    int total_chars = 0;
    for (const auto& l : lines) total_chars += static_cast<int>(utf8_cps(l).size()) + 4;
    float cps = std::max(24.f, total_chars / std::max(0.7f, c.dur * 0.6f));
    float typed = (c.ta - 0.55f) * cps;
    int before = 0;
    float cur_x = cx0 + pad, cur_y = cy0 + header + pad * 0.8f;
    bool cursor_set = false;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& l = lines[i];
        int n = static_cast<int>(utf8_cps(l).size());
        int vis = std::clamp(static_cast<int>(typed) - before, 0, n);
        int ly = static_cast<int>(cy0 + header + pad * 0.8f + lh * static_cast<float>(i) + (lh - px * 1.2f) * 0.5f);
        int lx = static_cast<int>(cx0 + pad);
        if (vis > 0) {
            bool cmd = looks_command(l);
            bool comment = !l.empty() && l[0] == '#';
            std::string body = l;
            if (cmd) {
                std::string shown = starts_with(l, "$ ") || starts_with(l, "> ") ? l.substr(2) : l;
                int ex = draw_word(c.f, true, "$", ipx, lx, ly, acc, pe);
                body = shown;
                lx = ex + static_cast<int>(px * 0.55f);
                int sub = vis - (n - static_cast<int>(utf8_cps(body).size()));
                int end = draw_word(c.f, false, body, ipx, lx, ly, ink, pe, INT_MIN, INT_MAX, std::max(0, sub));
                cur_x = static_cast<float>(end);
            } else {
                int end = draw_word(c.f, false, body, ipx, lx, ly, comment ? dim : Rgb{170, 220, 180}, pe, INT_MIN, INT_MAX, vis);
                cur_x = static_cast<float>(end);
            }
            cur_y = static_cast<float>(ly);
            cursor_set = true;
        }
        before += n + 4;
    }
    if (pe > 0.5f) {
        bool blink = std::fmod(c.ta * 2.f, 1.f) < 0.55f || typed < before;
        if (!cursor_set) {
            cur_x = cx0 + pad;
            cur_y = cy0 + header + pad * 0.8f + (lh - px * 1.2f) * 0.5f;
        }
        if (blink) fill_rrect(c.f, cur_x + 3.f * c.S, cur_y + px * 0.1f, px * 0.55f, px * 1.1f, 2.f * c.S, acc, 0.9f * pe);
    }
}

void draw_bars(Ctx& c) {
    struct Row {
        std::string label;
        double value;
        bool pct;
        bool pre;
    };
    std::vector<Row> rows;
    for (const auto& it : split_items(c.s.text)) {
        Row r;
        if (parse_bar(strip_bullet(it), r.label, r.value, r.pct, &r.pre)) rows.push_back(r);
        if (rows.size() >= 6) break;
    }
    if (rows.empty()) {
        draw_statement(c);
        return;
    }
    double maxv = 0;
    bool all_pct = true;
    for (const auto& r : rows) {
        maxv = std::max(maxv, r.value);
        if (r.value > 100.0) all_pct = false;
    }
    double scale = all_pct ? 100.0 : std::max(1e-6, maxv);
    float bw = c.sw * (c.portrait ? 1.f : 0.78f);
    float x = c.sx + (c.portrait ? 0.f : 0.f) + c.ox;
    Block title = make_block(c.s.title, true, (c.portrait ? 52.f : 48.f) * c.S, c.sw, 2, 28.f * c.S);
    int lpx = static_cast<int>((c.portrait ? 30.f : 28.f) * c.S);
    float bh = 16.f * c.S;
    float rh = lpx * 1.3f + 10.f * c.S + bh;
    float rgap = 26.f * c.S;
    float kh = kicker_h(c);
    float gap = title.height ? 40.f * c.S : 0.f;
    float total = kh + title.height + gap + rh * rows.size() + rgap * (rows.size() - 1);
    float y = c.sy + std::max(0.f, (c.sh - total) * 0.5f) + c.oy;
    draw_kicker(c, x, y, 0);
    y += kh;
    draw_block(c.f, title, c.ta, x, y, 0, c.ink, c.hi, 1.f, motion_at(c, 0.1f), c.shadow, c.S);
    y += title.height + gap;
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        float start = 0.35f + 0.16f * static_cast<float>(i);
        float pa = ease_out((c.ta - start) / 0.4f);
        float pb = ease_expo((c.ta - start - 0.1f) / 1.1f);
        float frac = static_cast<float>(std::clamp(r.value / scale, 0.0, 1.0));
        draw_word(c.f, false, r.label, lpx, static_cast<int>(x), static_cast<int>(y), c.ink, pa);
        char vb[32];
        if (r.value >= 100 || std::fabs(r.value - std::round(r.value)) < 1e-6)
            std::snprintf(vb, sizeof vb, "%s%lld%s", r.pre ? "%" : "", static_cast<long long>(std::llround(r.value * pb)),
                          r.pct && !r.pre ? "%" : "");
        else std::snprintf(vb, sizeof vb, "%s%.1f%s", r.pre ? "%" : "", r.value * pb, r.pct && !r.pre ? "%" : "");
        int vw = text_width(true, vb, lpx);
        draw_word(c.f, true, vb, lpx, static_cast<int>(x + bw - vw), static_cast<int>(y), c.pal.accent, pa);
        float by = y + lpx * 1.3f + 10.f * c.S;
        fill_rrect(c.f, x, by, bw, bh, bh * 0.5f, c.ink, 0.10f * pa);
        float fw = std::max(bh, bw * frac * pb);
        if (pb > 0.01f) {
            if (!c.pal.light) glow(c.f, x + fw, by + bh * 0.5f, bh * 2.4f, c.pal.accent2, 0.35f * pa);
            fill_rrect(c.f, x, by, fw, bh, bh * 0.5f, c.pal.accent, pa, &c.pal.accent2);
        }
        y += rh + rgap;
    }
}

void darken_for_text(Frame& f, float strength_bottom, float strength_left) {
    int w = f.w, h = f.h;
    for (int y = 0; y < h; y++) {
        float fy = static_cast<float>(y) / static_cast<float>(h);
        float ab = smoothstep(0.35f, 1.f, fy) * strength_bottom;
        uint8_t* row = px_at(f, 0, y);
        for (int x = 0; x < w; x++) {
            float fx = static_cast<float>(x) / static_cast<float>(w);
            float al = (1.f - smoothstep(0.f, 0.65f, fx)) * strength_left;
            float a = 1.f - (1.f - ab) * (1.f - al);
            if (a <= 0.004f) continue;
            uint8_t* p = row + x * 4;
            float k = 1.f - a;
            p[0] = static_cast<uint8_t>(p[0] * k);
            p[1] = static_cast<uint8_t>(p[1] * k);
            p[2] = static_cast<uint8_t>(p[2] * k);
        }
    }
}

void draw_media(Ctx& c, const Image& im) {
    float ia = static_cast<float>(im.w()) / static_cast<float>(std::max(1, im.h()));
    float fa = c.W / c.H;
    float dw = ia > fa ? c.H * ia : c.W;
    float dh = ia > fa ? c.H : c.W / ia;
    float k = c.cam.k * 1.05f;
    dw *= k;
    dh *= k;
    float x = (c.W - dw) * 0.5f + c.cam.ox * 1.4f;
    float y = (c.H - dh) * 0.5f + c.cam.oy * 1.4f;
    draw_image(c.f, im, x, y, dw, dh, 0.f, 1.f);
    darken_for_text(c.f, 0.82f, 0.38f);
    float maxw = c.sw * (c.portrait ? 1.f : 0.7f);
    Block title = make_block(c.s.title, true, (c.portrait ? 62.f : 66.f) * c.S, maxw, 3, 32.f * c.S);
    Block body = make_block(c.s.text, false, 30.f * c.S, maxw, 3, 20.f * c.S);
    float kh = kicker_h(c);
    float gap = title.height && body.height ? 16.f * c.S : 0.f;
    float total = kh + title.height + gap + body.height;
    float tx = c.sx;
    float ty = c.sy + c.sh - total;
    draw_kicker(c, tx, ty, 0);
    ty += kh;
    Motion mt = motion_at(c, 0.2f);
    draw_block(c.f, title, c.ta, tx, ty, 0, kWhite, c.pal.accent, 1.f, mt, 0.4f, c.S);
    ty += title.height + gap;
    Motion mb = motion_at(c, std::min(1.2f, anim_end(title, mt) - 0.1f), 0.025f);
    if (mb.kind == kSpring) mb.kind = kRise;
    draw_block(c.f, body, c.ta, tx, ty, 0, Rgb{226, 230, 236}, c.pal.accent, 1.f, mb, 0.4f, c.S);
}

void draw_showcase(Ctx& c, const Image& im) {
    float ia = static_cast<float>(im.w()) / static_cast<float>(std::max(1, im.h()));
    float ax, ay, aw, ah;
    float tx, tw;
    bool side = !(c.portrait || c.square);
    if (side) {
        tw = c.sw * 0.34f;
        tx = c.sx;
        ax = c.sx + c.sw * 0.39f;
        aw = c.sw * 0.61f;
        ay = c.sy;
        ah = c.sh;
    } else {
        tw = c.sw;
        tx = c.sx;
        ax = c.sx;
        aw = c.sw;
        ay = c.sy + c.sh * 0.42f;
        ah = c.sh * 0.58f;
    }
    float dw = aw, dh = aw / ia;
    if (dh > ah) {
        dh = ah;
        dw = ah * ia;
    }
    float pe = ease_out((c.ta - 0.1f) / 0.9f);
    float zk = 1.f + (c.cam.k - 1.f) * 0.6f;
    float iw = dw * zk, ih = dh * zk;
    float ix = ax + (aw - iw) * 0.5f + c.cam.ox * 0.5f;
    float iy = ay + (ah - ih) * 0.5f + (1.f - pe) * 50.f * c.S + std::sin(c.T * 1.1f) * 4.f * c.S + c.cam.oy * 0.5f;
    float r = 14.f * c.S;
    if (!c.pal.light) glow(c.f, ix + iw * 0.5f, iy + ih * 0.5f, std::max(iw, ih) * 0.75f, c.pal.accent, 0.20f * pe);
    shadow_rrect(c.f, ix, iy + 22.f * c.S, iw, ih, r, 60.f * c.S, 0.6f * pe);
    draw_image(c.f, im, ix, iy, iw, ih, r, pe);
    stroke_rrect(c.f, ix, iy, iw, ih, r, 1.4f * c.S, kWhite, 0.14f * pe);

    Block title = make_block(c.s.title, true, (side ? 52.f : 52.f) * c.S, tw, side ? 4 : 2, 28.f * c.S);
    Block body = make_block(c.s.text, false, (side ? 27.f : 29.f) * c.S, tw, side ? 7 : 3, 18.f * c.S);
    float kh = kicker_h(c);
    float gap = title.height && body.height ? 18.f * c.S : 0.f;
    float total = kh + title.height + gap + body.height;
    float y = side ? c.sy + (c.sh - total) * 0.5f + c.oy : c.sy + (c.sh * 0.4f - total) * 0.5f + c.oy;
    float x = tx + c.ox;
    draw_kicker(c, x, y, 0);
    y += kh;
    Motion mt = motion_at(c, 0.15f);
    draw_block(c.f, title, c.ta, x, y, 0, c.ink, c.hi, 1.f, mt, c.shadow, c.S);
    y += title.height + gap;
    Motion mb = motion_at(c, std::min(1.2f, anim_end(title, mt) - 0.1f), 0.025f);
    if (mb.kind == kSpring) mb.kind = kRise;
    draw_block(c.f, body, c.ta, x, y, 0, c.body, c.hi, 1.f, mb, c.shadow, c.S);
}

void draw_end(Ctx& c) {
    float maxw = c.sw * 0.84f;
    Block title = make_block(c.s.title, true, (c.portrait ? 50.f : 54.f) * c.S, maxw - 70.f * c.S, 2, 26.f * c.S);
    Block body = make_block(c.s.text, false, 28.f * c.S, maxw, 3, 18.f * c.S);
    float padx = 36.f * c.S, pady = 20.f * c.S;
    float pw = title.width + padx * 2.f, ph = title.height + pady * 2.f;
    float kh = kicker_h(c);
    float gap = body.height ? 30.f * c.S : 0.f;
    float total = kh + (title.height ? ph : 0.f) + gap + body.height;
    float x = c.W * 0.5f + c.ox;
    float y = c.sy + (c.sh - total) * 0.5f + c.oy;
    draw_kicker(c, x, y, 1);
    y += kh;
    if (title.height) {
        float p = spring((c.ta - 0.1f) / 0.9f);
        float a = clamp01((c.ta - 0.1f) * 3.f);
        float w = pw * (0.8f + 0.2f * p);
        if (!c.pal.light) glow(c.f, x, y + ph * 0.5f, pw * 0.7f, c.pal.accent, 0.22f * a);
        fill_rrect(c.f, x - w * 0.5f, y, w, ph, ph * 0.5f, c.pal.accent, 0.12f * a);
        stroke_rrect(c.f, x - w * 0.5f, y, w, ph, ph * 0.5f, 2.5f * c.S, c.pal.accent, a);
        Motion mt = motion_at(c, 0.2f, 0.05f);
        if (mt.kind == kType) mt.kind = kFade;
        draw_block(c.f, title, c.ta, x, y + pady, 1, c.ink, c.hi, 1.f, mt, c.shadow, c.S);
        y += ph + gap;
    }
    Motion mb = motion_at(c, 0.6f, 0.03f);
    mb.kind = kFade;
    draw_block(c.f, body, c.ta, x, y, 1, c.body, c.hi, 1.f, mb, c.shadow, c.S);
}

// ---------------------------------------------------------------- composition

std::string style_of(const Storyboard& b, int i) {
    const Scene& s = b.scenes[static_cast<size_t>(i)];
    return s.style.empty() ? b.style : s.style;
}

bool letterboxed(const Storyboard& b) { return b.style == "ember" && b.width > b.height; }

void render_scene(const Storyboard& b, int index, double local, double T, Frame& out) {
    const int W = b.width, H = b.height;
    out.w = W;
    out.h = H;
    out.rgba.resize(static_cast<size_t>(W) * H * 4);
    const Scene& s = b.scenes[static_cast<size_t>(index)];
    Pal pal = palette_for(style_of(b, index));
    float dur = static_cast<float>(std::max(0.001, s.seconds));
    float u = static_cast<float>(local) / dur;
    Cam cam = camera_for(s.motion, std::clamp(u, 0.f, 1.2f), static_cast<float>(W), static_cast<float>(H));
    paint_background(out, pal, static_cast<float>(T), cam);
    float S = static_cast<float>(std::min(W, H)) / 720.f;
    paint_deco(out, pal, static_cast<float>(T), cam, S);

    Ctx c{out, b, s, pal};
    c.index = index;
    c.count = static_cast<int>(b.scenes.size());
    c.t = static_cast<float>(local);
    float tscale = std::clamp(dur / 3.2f, 0.55f, 1.f);
    c.ta = c.t / tscale;
    c.dur = dur / tscale;
    c.T = static_cast<float>(T);
    c.S = S;
    c.W = static_cast<float>(W);
    c.H = static_cast<float>(H);
    c.portrait = H > W * 1.2f;
    c.square = !c.portrait && W < H * 1.2f;
    float mx = c.W * (c.portrait ? 0.085f : 0.08f);
    float my = c.H * (c.portrait ? 0.08f : 0.11f);
    if (letterboxed(b)) my += c.H * 0.07f;
    c.sx = mx;
    c.sy = my;
    c.sw = c.W - mx * 2.f;
    c.sh = c.H - my * 2.f;
    c.cam = cam;
    c.ox = cam.ox * 0.25f;
    c.oy = cam.oy * 0.25f;
    c.anim = anim_kind(resolve_anim(b, index));
    c.ink = pal.ink;
    c.body = pal.light ? pal.muted : mix(pal.ink, pal.muted, 0.35f);
    c.hi = pal.accent;
    c.shadow = pal.light ? 0.f : 0.35f;

    std::string layout = resolve_layout(b, index);
    if (layout == "media" || layout == "showcase") {
        auto im = get_image(s.image);
        if (im && !im->levels.empty()) {
            if (layout == "media") draw_media(c, *im);
            else draw_showcase(c, *im);
            return;
        }
        layout = "statement";
    }
    if (s.title.empty() && s.text.empty()) return;
    if (layout == "hero") draw_hero(c);
    else if (layout == "list") draw_list(c);
    else if (layout == "stat") draw_stat(c);
    else if (layout == "quote") draw_quote(c);
    else if (layout == "split") draw_split(c);
    else if (layout == "terminal") draw_terminal(c);
    else if (layout == "bars") draw_bars(c);
    else if (layout == "end") draw_end(c);
    else draw_statement(c);
}

void sample_scaled(const Frame& src, float k, float sx, float sy, float& r, float& g, float& b) {
    float cx = src.w * 0.5f, cy = src.h * 0.5f;
    float x = cx + (sx - cx) / k - 0.5f, y = cy + (sy - cy) / k - 0.5f;
    int ix = std::clamp(static_cast<int>(std::floor(x)), 0, src.w - 2);
    int iy = std::clamp(static_cast<int>(std::floor(y)), 0, src.h - 2);
    float fx = clamp01(x - ix), fy = clamp01(y - iy);
    const uint8_t* p00 = &src.rgba[(static_cast<size_t>(iy) * src.w + ix) * 4];
    const uint8_t* p01 = p00 + 4;
    const uint8_t* p10 = p00 + static_cast<size_t>(src.w) * 4;
    const uint8_t* p11 = p10 + 4;
    auto lerp2 = [&](int k2) {
        float t = p00[k2] + (p01[k2] - p00[k2]) * fx;
        float bt = p10[k2] + (p11[k2] - p10[k2]) * fx;
        return t + (bt - t) * fy;
    };
    r = lerp2(0);
    g = lerp2(1);
    b = lerp2(2);
}

void composite(const std::string& type, float p, const Frame& A, const Frame& B, Frame& out, const Pal& pal, float S) {
    const int w = A.w, h = A.h;
    out.w = w;
    out.h = h;
    out.rgba.resize(A.rgba.size());
    if (type == "slide") {
        float e = ease_in_out(p);
        int off = static_cast<int>(std::lround(e * w));
        for (int y = 0; y < h; y++) {
            uint8_t* dst = &out.rgba[static_cast<size_t>(y) * w * 4];
            const uint8_t* ra = &A.rgba[static_cast<size_t>(y) * w * 4];
            const uint8_t* rb = &B.rgba[static_cast<size_t>(y) * w * 4];
            if (w - off > 0) std::memcpy(dst, ra + static_cast<size_t>(off) * 4, static_cast<size_t>(w - off) * 4);
            if (off > 0) std::memcpy(dst + static_cast<size_t>(w - off) * 4, rb, static_cast<size_t>(off) * 4);
        }
        int seam = w - off;
        for (int y = 0; y < h; y++)
            for (int x = std::max(0, seam - static_cast<int>(40 * S)); x < std::min(w, seam); x++) {
                float d = static_cast<float>(seam - x) / (40.f * S);
                blend_px(&out.rgba[(static_cast<size_t>(y) * w + x) * 4], kBlack, static_cast<int>((1.f - d) * 70.f));
            }
        return;
    }
    if (type == "wipe") {
        float e = ease_in_out(p);
        float edge = -0.12f + e * 1.24f;
        for (int y = 0; y < h; y++) {
            float fy = static_cast<float>(y) / h;
            for (int x = 0; x < w; x++) {
                float pos = static_cast<float>(x) / w * 0.86f + fy * 0.14f;
                float m = 1.f - smoothstep(edge - 0.012f, edge + 0.012f, pos);
                size_t i = (static_cast<size_t>(y) * w + x) * 4;
                for (int k = 0; k < 3; k++) out.rgba[i + k] = static_cast<uint8_t>(A.rgba[i + k] + (B.rgba[i + k] - A.rgba[i + k]) * m);
                out.rgba[i + 3] = 255;
                float g = std::exp(-((pos - edge) * (pos - edge)) / 0.00012f);
                if (g > 0.01f) add_px(&out.rgba[i], pal.accent, static_cast<int>(g * 230.f));
            }
        }
        return;
    }
    if (type == "zoom") {
        float e = ease_in_out(p);
        float ka = 1.f + 0.45f * e;
        float kb = 1.22f - 0.22f * e;
        float m = smoothstep(0.3f, 0.7f, p);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float r = 0, g = 0, b = 0;
                float r2 = 0, g2 = 0, b2 = 0;
                if (m < 1.f) sample_scaled(A, ka, x + 0.5f, y + 0.5f, r, g, b);
                if (m > 0.f) sample_scaled(B, kb, x + 0.5f, y + 0.5f, r2, g2, b2);
                size_t i = (static_cast<size_t>(y) * w + x) * 4;
                out.rgba[i] = static_cast<uint8_t>(r + (r2 - r) * m);
                out.rgba[i + 1] = static_cast<uint8_t>(g + (g2 - g) * m);
                out.rgba[i + 2] = static_cast<uint8_t>(b + (b2 - b) * m);
                out.rgba[i + 3] = 255;
            }
        }
        float fl = 1.f - std::fabs(p - 0.5f) * 2.f;
        if (fl > 0.f) {
            int a = static_cast<int>(fl * fl * 70.f);
            for (size_t i = 0; i < out.rgba.size(); i += 4) add_px(&out.rgba[i], kWhite, a);
        }
        return;
    }
    if (type == "flash") {
        const Frame& src = p < 0.5f ? A : B;
        float q = p < 0.5f ? p / 0.5f : (1.f - p) / 0.5f;
        int a = static_cast<int>(q * q * 245.f);
        Rgb fc = mix(kWhite, pal.accent, 0.12f);
        out.rgba = src.rgba;
        for (size_t i = 0; i < out.rgba.size(); i += 4) blend_px(&out.rgba[i], fc, a);
        return;
    }
    if (type == "glitch") {
        float amp = 1.f - std::fabs(p * 2.f - 1.f);
        uint32_t step = static_cast<uint32_t>(p * 9.f);
        int rs = static_cast<int>(amp * 16.f * S);
        int y = 0;
        uint32_t band = 0;
        while (y < h) {
            int bh = static_cast<int>((6.f + 40.f * rnd(step * 131u + band * 7u)) * S);
            float r = rnd(step * 977u + band * 13u);
            const Frame& src = (r < p) ? B : A;
            int shift = rnd(step * 31u + band * 5u) < 0.5f ? static_cast<int>((rnd(step * 17u + band) - 0.5f) * amp * w * 0.14f) : 0;
            for (int yy = y; yy < std::min(h, y + bh); yy++) {
                for (int x = 0; x < w; x++) {
                    int xs = std::clamp(x + shift, 0, w - 1);
                    int xr = std::clamp(xs + rs, 0, w - 1), xb = std::clamp(xs - rs, 0, w - 1);
                    size_t row = static_cast<size_t>(yy) * w;
                    size_t i = (row + x) * 4;
                    out.rgba[i] = src.rgba[(row + xr) * 4];
                    out.rgba[i + 1] = src.rgba[(row + xs) * 4 + 1];
                    out.rgba[i + 2] = src.rgba[(row + xb) * 4 + 2];
                    out.rgba[i + 3] = 255;
                }
            }
            y += std::max(1, bh);
            band++;
        }
        return;
    }
    float e = type == "fade" ? smoothstep(0.f, 1.f, p) : (p < 1.f ? 0.f : 1.f);
    int m = static_cast<int>(e * 256.f);
    for (size_t i = 0; i < out.rgba.size(); i++) out.rgba[i] = static_cast<uint8_t>(A.rgba[i] + (((B.rgba[i] - A.rgba[i]) * m) >> 8));
}

void finish(const Storyboard& b, Frame& f, double t, double total, const Pal& pal) {
    const int w = f.w, h = f.h;
    float S = static_cast<float>(std::min(w, h)) / 720.f;
    if (letterboxed(b)) {
        int lb = static_cast<int>(h * 0.07f);
        for (int y = 0; y < lb; y++) {
            std::memset(&f.rgba[static_cast<size_t>(y) * w * 4], 0, static_cast<size_t>(w) * 4);
            std::memset(&f.rgba[static_cast<size_t>(h - 1 - y) * w * 4], 0, static_cast<size_t>(w) * 4);
        }
        for (size_t i = 3; i < f.rgba.size(); i += 4) f.rgba[i] = 255;
    }
    if (b.mark) {
        int px = static_cast<int>(std::lround(14.f * S));
        int sp = static_cast<int>(std::lround(3.f * S));
        int tw = spaced_width(true, "KARE", px, sp);
        int x = w - static_cast<int>(w * 0.04f) - tw;
        int y = letterboxed(b) ? h - static_cast<int>(h * 0.035f) - px / 2 - px / 2 : h - static_cast<int>(h * 0.045f) - px;
        Rgb c = letterboxed(b) ? Rgb{150, 150, 150} : pal.muted;
        draw_spaced(f, true, "KARE", px, x, y, sp, c, 0.55f);
    }
    if (b.grain) {
        uint32_t seed = hash32(static_cast<uint32_t>(t * 60.0) + 17u);
        int amp = pal.light ? 5 : 7;
        for (int y = 0; y < h; y++) {
            uint32_t st = seed ^ (static_cast<uint32_t>(y) * 0x9E3779B9u);
            uint8_t* row = px_at(f, 0, y);
            for (int x = 0; x < w; x++) {
                st = st * 1664525u + 1013904223u;
                int n = static_cast<int>((st >> 24) & 0xFF) - 128;
                int d = n * amp / 128;
                uint8_t* p = row + x * 4;
                p[0] = static_cast<uint8_t>(std::clamp(p[0] + d, 0, 255));
                p[1] = static_cast<uint8_t>(std::clamp(p[1] + d, 0, 255));
                p[2] = static_cast<uint8_t>(std::clamp(p[2] + d, 0, 255));
            }
        }
    }
    float fade = 1.f;
    if (t < 0.45) fade = static_cast<float>(t / 0.45);
    if (total - t < 0.7) fade = std::min(fade, static_cast<float>((total - t) / 0.7));
    fade = smoothstep(0.f, 1.f, fade);
    if (fade < 0.999f) {
        Rgb to = pal.light ? pal.bg0 : kBlack;
        int a = static_cast<int>((1.f - fade) * 255.f);
        for (size_t i = 0; i < f.rgba.size(); i += 4) blend_px(&f.rgba[i], to, a);
    }
}

}  // namespace

std::string resolve_layout(const Storyboard& b, int index) {
    if (index < 0 || index >= static_cast<int>(b.scenes.size())) return "statement";
    const Scene& s = b.scenes[static_cast<size_t>(index)];
    if (!s.layout.empty() && s.layout != "auto") return s.layout;
    int n = static_cast<int>(b.scenes.size());
    if (!s.image.empty()) return utf8_cps(s.title).size() + utf8_cps(s.text).size() < 70 ? "media" : "showcase";
    auto lines = text_lines(s.text);
    if (lines.size() >= 2) {
        int cmd = 0, bars = 0;
        for (const auto& l : lines) {
            if (looks_command(l)) cmd++;
            std::string lab;
            double v;
            bool pct;
            if (parse_bar(strip_bullet(l), lab, v, pct)) bars++;
        }
        if (cmd * 2 >= static_cast<int>(lines.size())) return "terminal";
        if (bars == static_cast<int>(lines.size())) return "bars";
        return "list";
    }
    std::string title = tidy(s.title);
    auto tc = utf8_cps(title);
    if (!tc.empty() && tc.size() <= 9 && ((tc[0] >= '0' && tc[0] <= '9') || tc[0] == '%' || tc[0] == '+' || tc[0] == '$') &&
        parse_count(title).ok)
        return "stat";
    auto first = utf8_cps(s.text.empty() ? s.title : s.text);
    if (!first.empty() && (first[0] == '"' || first[0] == 0x201C || first[0] == 0xAB)) return "quote";
    std::string ft = fold_tr(s.title + " " + s.text);
    if (index == n - 1 && n >= 3 &&
        (ft.find("github.com") != std::string::npos || ft.find("www.") != std::string::npos ||
         ft.find("http") != std::string::npos || ft.find('@') != std::string::npos))
        return "end";
    if (index == 0) return "hero";
    if (s.text.empty() && tc.size() <= 42) return "hero";
    if (b.width > b.height && utf8_cps(s.text).size() > 110 && !s.title.empty()) return "split";
    return "statement";
}

std::string resolve_transition(const Storyboard& b, int index) {
    if (index <= 0 || index >= static_cast<int>(b.scenes.size())) return "cut";
    const Scene& s = b.scenes[static_cast<size_t>(index)];
    if (!s.transition.empty() && s.transition != "auto") return s.transition;
    if (resolve_layout(b, index) == "end") return "flash";
    std::string st = style_of(b, index);
    static const char* neon[] = {"glitch", "zoom", "slide", "glitch", "wipe"};
    static const char* calm[] = {"slide", "wipe", "fade"};
    static const char* cool[] = {"fade", "zoom", "slide", "wipe"};
    static const char* warm[] = {"zoom", "slide", "wipe", "fade", "slide"};
    int k = index - 1;
    if (st == "neon") return neon[k % 5];
    if (st == "paper" || st == "minimal") return calm[k % 3];
    if (st == "night" || st == "sea") return cool[k % 4];
    return warm[k % 5];
}

std::string resolve_anim(const Storyboard& b, int index) {
    if (index < 0 || index >= static_cast<int>(b.scenes.size())) return "rise";
    const Scene& s = b.scenes[static_cast<size_t>(index)];
    if (!s.anim.empty() && s.anim != "auto") return s.anim;
    std::string l = resolve_layout(b, index);
    if (l == "hero" || l == "end") return "spring";
    if (l == "quote" || l == "split" || l == "media") return "reveal";
    if (l == "terminal") return "type";
    if (l == "stat") return "fade";
    return "rise";
}

double transition_seconds(const Storyboard& b, int index) {
    if (index <= 0 || index >= static_cast<int>(b.scenes.size())) return 0.0;
    std::string type = resolve_transition(b, index);
    if (type == "cut") return 0.0;
    double d = type == "flash" ? 0.36 : type == "glitch" ? 0.32 : type == "fade" ? 0.5 : 0.55;
    d = std::min(d, 0.4 * b.scenes[static_cast<size_t>(index)].seconds);
    d = std::min(d, 0.4 * b.scenes[static_cast<size_t>(index) - 1].seconds);
    return std::max(0.0, d);
}

bool image_readable(const std::string& path, int* w, int* h) {
    auto im = get_image(path);
    if (!im || im->levels.empty()) return false;
    if (w) *w = im->w();
    if (h) *h = im->h();
    return true;
}

void render_frame(const Storyboard& board, double time_sec, Frame& out) {
    Storyboard b = board;
    normalize(b);
    double total = std::max(0.001, total_seconds(b));
    double t = std::clamp(time_sec, 0.0, total - 1e-4);
    int index = 0;
    double start = 0;
    for (int i = 0; i < static_cast<int>(b.scenes.size()); i++) {
        double dur = b.scenes[static_cast<size_t>(i)].seconds;
        if (t < start + dur || i + 1 == static_cast<int>(b.scenes.size())) {
            index = i;
            break;
        }
        start += dur;
    }
    double local = t - start;
    double td = transition_seconds(b, index);
    Pal pal = palette_for(style_of(b, index));
    if (index > 0 && td > 0.0 && local < td) {
        Frame A, B;
        render_scene(b, index - 1, b.scenes[static_cast<size_t>(index) - 1].seconds + local, t, A);
        render_scene(b, index, local, t, B);
        float S = static_cast<float>(std::min(b.width, b.height)) / 720.f;
        composite(resolve_transition(b, index), static_cast<float>(local / td), A, B, out, pal, S);
    } else {
        render_scene(b, index, local, t, out);
    }
    finish(b, out, t, total, pal);
}

uint64_t frame_hash(const Frame& frame) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < frame.rgba.size(); i += 64) {
        h ^= frame.rgba[i];
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace kare
