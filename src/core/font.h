#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kare {

struct Glyph {
    int w = 0;
    int h = 0;
    int xoff = 0;
    int yoff = 0;
    int advance = 0;
    std::vector<unsigned char> alpha;
};

struct TextLayout {
    std::vector<std::string> lines;
    int line_h = 0;
    int height = 0;
    int px = 0;
    bool bold = false;
};

bool fonts_load(std::string& err);
bool fonts_ready();
const Glyph& glyph_for(bool bold, uint32_t cp, int px);
int font_ascent(bool bold, int px);
int font_kern(bool bold, uint32_t prev, uint32_t cp, int px);
int text_width(bool bold, const std::string& text, int px);
TextLayout layout_text(bool bold, const std::string& text, int px, int max_width, int max_lines);

}  // namespace kare
