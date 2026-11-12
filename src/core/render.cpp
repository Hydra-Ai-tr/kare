#include "core/render.h"

#include <algorithm>
#include <cmath>

#include "core/font.h"
#include "core/utf8.h"

namespace kare {

namespace {

struct Rgb {
    uint8_t r, g, b;
};

struct Pal {
    Rgb bg0, bg1, accent, accent2, ink, muted;
    bool light = false;
};

Pal palette_for(const std::string& style) {
    if (style == "dusk") return {{42, 18, 36}, {196, 84, 42}, {255, 198, 130}, {255, 120, 70}, {255, 244, 230}, {255, 214, 180}, false};
    if (style == "night") return {{8, 12, 28}, {18, 36, 72}, {150, 190, 255}, {90, 120, 200}, {236, 242, 255}, {170, 190, 220}, false};
    if (style == "sea") return {{6, 32, 40}, {14, 78, 88}, {130, 230, 210}, {70, 170, 190}, {236, 252, 248}, {170, 210, 200}, false};
    if (style == "forest") return {{10, 26, 16}, {24, 58, 30}, {214, 184, 90}, {80, 140, 70}, {244, 240, 220}, {190, 200, 160}, false};
    if (style == "neon") return {{8, 6, 18}, {28, 8, 48}, {255, 50, 170}, {40, 230, 255}, {255, 245, 255}, {220, 180, 230}, false};
    if (style == "paper") return {{236, 226, 208}, {214, 196, 170}, {150, 52, 36}, {90, 70, 50}, {32, 26, 22}, {90, 70, 55}, true};
    if (style == "minimal") return {{244, 244, 244}, {226, 226, 228}, {20, 20, 20}, {90, 90, 90}, {18, 18, 18}, {80, 80, 80}, true};
    return {{16, 13, 11}, {52, 30, 16}, {232, 168, 74}, {255, 110, 55}, {248, 240, 228}, {196, 170, 140}, false};
}

void put(Frame& f, int x, int y, Rgb c) {
    if (x < 0 || y < 0 || x >= f.w || y >= f.h) return;
    uint8_t* p = &f.rgba[(static_cast<size_t>(y) * f.w + x) * 4];
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = 255;
}

void blend(Frame& f, int x, int y, Rgb c, int a) {
    if (x < 0 || y < 0 || x >= f.w || y >= f.h || a <= 0) return;
    if (a > 255) a = 255;
    uint8_t* p = &f.rgba[(static_cast<size_t>(y) * f.w + x) * 4];
    int ia = 255 - a;
    p[0] = static_cast<uint8_t>((c.r * a + p[0] * ia) / 255);
    p[1] = static_cast<uint8_t>((c.g * a + p[1] * ia) / 255);
    p[2] = static_cast<uint8_t>((c.b * a + p[2] * ia) / 255);
    p[3] = 255;
}

Rgb lerp(Rgb a, Rgb b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return {static_cast<uint8_t>(a.r + (b.r - a.r) * t), static_cast<uint8_t>(a.g + (b.g - a.g) * t),
            static_cast<uint8_t>(a.b + (b.b - a.b) * t)};
}

void fill_rect(Frame& f, int x, int y, int w, int h, Rgb c, int a) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) blend(f, xx, yy, c, a);
}

void orb(Frame& f, float cx, float cy, float radius, Rgb c, float strength) {
    if (radius < 2) return;
    int x0 = std::max(0, static_cast<int>(cx - radius));
    int y0 = std::max(0, static_cast<int>(cy - radius));
    int x1 = std::min(f.w - 1, static_cast<int>(cx + radius));
    int y1 = std::min(f.h - 1, static_cast<int>(cy + radius));
    float r2 = radius * radius;
    for (int y = y0; y <= y1; y++) {
        float dy = static_cast<float>(y) - cy;
        for (int x = x0; x <= x1; x++) {
            float dx = static_cast<float>(x) - cx;
            float d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            float d = std::sqrt(d2) / radius;
            float a = (1.f - d);
            a = a * a * strength;
            blend(f, x, y, c, static_cast<int>(a * 255));
        }
    }
}

void draw_glyphs(Frame& f, bool bold, const std::string& text, int px, int x, int top, Rgb color, int alpha) {
    int baseline = top + font_ascent(bold, px);
    uint32_t prev = 0;
    size_t i = 0;
    uint32_t cp = 0;
    while (utf8_next(text, i, cp)) {
        if (prev) x += font_kern(bold, prev, cp, px);
        const Glyph& g = glyph_for(bold, cp, px);
        for (int gy = 0; gy < g.h; gy++) {
            for (int gx = 0; gx < g.w; gx++) {
                int a = g.alpha[static_cast<size_t>(gy) * g.w + gx];
                if (!a) continue;
                blend(f, x + g.xoff + gx, baseline + g.yoff + gy, color, a * alpha / 255);
            }
        }
        x += g.advance;
        prev = cp;
    }
}

void draw_layout(Frame& f, const TextLayout& layout, int center_x, int top, Rgb color, int alpha, bool shadow) {
    int y = top;
    for (const auto& line : layout.lines) {
        int w = text_width(layout.bold, line, layout.px);
        int x = center_x - w / 2;
        if (shadow) {
            Rgb sh{0, 0, 0};
            draw_glyphs(f, layout.bold, line, layout.px, x + 2, y + 3, sh, alpha / 3);
        }
        draw_glyphs(f, layout.bold, line, layout.px, x, y, color, alpha);
        y += layout.line_h;
    }
}

float smooth(float u) {
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    return u * u * (3.f - 2.f * u);
}

}  // namespace

void render_frame(const Storyboard& board, double time_sec, Frame& out) {
    Storyboard b = board;
    normalize(b);
    out.w = b.width;
    out.h = b.height;
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, 0);

    double total = std::max(0.001, total_seconds(b));
    double t = time_sec;
    if (t < 0) t = 0;
    if (t > total - 1e-4) t = total - 1e-4;
    int index = 0;
    double acc = 0;
    for (int i = 0; i < static_cast<int>(b.scenes.size()); i++) {
        double dur = b.scenes[static_cast<size_t>(i)].seconds;
        if (t < acc + dur || i + 1 == static_cast<int>(b.scenes.size())) {
            index = i;
            break;
        }
        acc += dur;
    }
    const Scene& scene = b.scenes[static_cast<size_t>(index)];
    double local = t - acc;
    double dur = std::max(0.001, scene.seconds);
    float u = static_cast<float>(local / dur);
    std::string style = scene.style.empty() ? b.style : scene.style;
    Pal pal = palette_for(style);
    std::string motion = scene.motion.empty() ? "drift" : scene.motion;

    float ox = 0, oy = 0, zoom = 1.f;
    if (motion == "drift") {
        ox = std::sin(u * 3.14159f) * out.w * 0.03f;
        oy = std::cos(u * 2.1f) * out.h * 0.018f;
    } else if (motion == "rise") {
        oy = (1.f - smooth(std::min(1.f, u * 1.4f))) * out.h * 0.04f;
    } else if (motion == "zoom") {
        zoom = 1.f + 0.06f * u;
    }

    for (int y = 0; y < out.h; y++) {
        float ty = out.h <= 1 ? 0 : static_cast<float>(y) / static_cast<float>(out.h - 1);
        float wave = 0.5f + 0.5f * std::sin(ty * 3.0f + u * 1.2f);
        Rgb col = lerp(pal.bg0, pal.bg1, ty * 0.82f + wave * 0.18f);
        float ny = (static_cast<float>(y) - out.h * 0.5f) / (out.h * 0.72f);
        for (int x = 0; x < out.w; x++) {
            float nx = (static_cast<float>(x) - out.w * 0.5f) / (out.w * 0.72f);
            float d = nx * nx + ny * ny;
            float vig = d <= 1.f ? 0.f : std::min(0.68f, (d - 1.f) / 1.15f);
            Rgb c = col;
            c.r = static_cast<uint8_t>(c.r * (1.f - vig));
            c.g = static_cast<uint8_t>(c.g * (1.f - vig));
            c.b = static_cast<uint8_t>(c.b * (1.f - vig));
            put(out, x, y, c);
        }
    }

    float strength = pal.light ? 0.28f : 0.62f;
    orb(out, out.w * 0.22f + ox, out.h * 0.28f + oy * 0.4f, out.w * 0.30f * zoom, pal.accent, strength);
    orb(out, out.w * 0.80f - ox * 0.8f, out.h * 0.74f - oy, out.w * 0.24f * zoom, pal.accent2, strength * 0.75f);

    int title_px = std::max(28, out.h / 11);
    int body_px = std::max(18, out.h / 24);
    if (out.h > out.w) {
        title_px = std::max(28, out.h / 16);
        body_px = std::max(18, out.h / 32);
    }
    if (scene.text.empty()) title_px = std::max(title_px, out.h / 9);
    if (scene.title.empty()) body_px = std::max(body_px, out.h / 16);
    title_px = static_cast<int>(title_px * zoom);
    int max_w = static_cast<int>(out.w * 0.78f);
    TextLayout title = layout_text(true, scene.title, title_px, max_w, 3);
    TextLayout body = layout_text(false, scene.text, body_px, max_w, 4);
    int gap = (title.height && body.height) ? std::max(12, title_px / 4) : 0;
    int block_h = title.height + gap + body.height;
    int top = (out.h - block_h) / 2 + static_cast<int>(oy);
    int min_top = static_cast<int>(out.h * 0.14f);
    int max_top = std::max(min_top, out.h - block_h - static_cast<int>(out.h * 0.12f));
    if (top < min_top) top = min_top;
    if (top > max_top) top = max_top;
    int cx = out.w / 2 + static_cast<int>(ox);

    if (block_h > 0) {
        Rgb band = pal.light ? Rgb{255, 255, 255} : Rgb{0, 0, 0};
        int ba = pal.light ? 70 : 55;
        fill_rect(out, 0, top - static_cast<int>(out.h * 0.03f), out.w, block_h + static_cast<int>(out.h * 0.06f), band, ba);
    }
    bool shadow = !pal.light;
    draw_layout(out, title, cx, top, pal.ink, 255, shadow);
    draw_layout(out, body, cx, top + title.height + gap, pal.light ? pal.muted : pal.ink, pal.light ? 255 : 230, shadow);

    int line_w = static_cast<int>(out.w * 0.16f * (0.35f + 0.65f * smooth(u)));
    int line_y = top + block_h + std::max(16, out.h / 48);
    if (line_y < out.h - 8 && line_w > 2)
        fill_rect(out, cx - line_w / 2, line_y, line_w, std::max(3, out.h / 240), pal.accent, 230);

    if (b.mark) {
        int px = std::max(14, out.h / 46);
        draw_glyphs(out, true, "KARE", px, static_cast<int>(out.w * 0.06f), static_cast<int>(out.h * 0.055f), pal.muted, 200);
    }

    int n = static_cast<int>(b.scenes.size());
    int gapx = std::max(14, out.w / 70);
    int dots_w = n * gapx;
    int dx = out.w / 2 - dots_w / 2 + gapx / 2;
    int dy = out.h - std::max(28, out.h / 16);
    for (int i = 0; i < n; i++) {
        int rad = i == index ? 6 : 3;
        Rgb c = i == index ? pal.accent : pal.muted;
        for (int y = -rad; y <= rad; y++)
            for (int x = -rad; x <= rad; x++)
                if (x * x + y * y <= rad * rad) blend(out, dx + i * gapx + x, dy + y, c, 230);
    }

    float fade = 1.f;
    double edge = std::min(0.32, dur * 0.24);
    if (edge < 0.04) edge = 0.04;
    if (local < edge) fade = static_cast<float>(local / edge);
    if (dur - local < edge) fade = std::min(fade, static_cast<float>((dur - local) / edge));
    fade = smooth(fade);
    if (fade < 0.999f) {
        for (int i = 0; i < out.w * out.h; i++) {
            uint8_t* p = &out.rgba[static_cast<size_t>(i) * 4];
            p[0] = static_cast<uint8_t>(pal.bg0.r + (p[0] - pal.bg0.r) * fade);
            p[1] = static_cast<uint8_t>(pal.bg0.g + (p[1] - pal.bg0.g) * fade);
            p[2] = static_cast<uint8_t>(pal.bg0.b + (p[2] - pal.bg0.b) * fade);
        }
    }
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
