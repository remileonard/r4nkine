// font_data.h - the glyphs baked from Liberation Sans (font_data.inc), for r4::ui and r4::raster.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace r4::detail {

struct BakedFont {
    int width = 0; // the atlas, the fonts in it
    int height = 0;
    int count = 0;
    std::vector<uint8_t> pixels; // the atlas, 8 bits, rows from the top
    // per font: size in pixels, ascent, descent, line gap
    const float (*metrics)[4] = nullptr;
    // per font, per glyph (32-126 then 160-255): x0 y0 x1 y1 in the atlas
    const short (*boxes)[191][4] = nullptr;
    // per font, per glyph: xoff yoff xadvance
    const float (*glyphs)[191][3] = nullptr;
};

// the fonts: 0 regular 13, 1 bold 13, 2 small 11, 3 title 16; then the same at 2x
const BakedFont &bakedFont();

// the index of a Latin-1 code among the glyphs ('?' for the missing ones)
inline int glyphIndex(int code) {
    if (code >= 32 && code <= 126) {
        return code - 32;
    }
    if (code >= 160 && code <= 255) {
        return 95 + code - 160;
    }
    return '?' - 32;
}

} // namespace r4::detail
