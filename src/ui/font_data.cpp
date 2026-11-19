// font_data.cpp - the baked glyphs, decoded once.
#include "font_data.h"

namespace r4::detail {

namespace {
#include "font_data.inc"
} // namespace

const BakedFont &bakedFont() {
    static const BakedFont font = [] {
        BakedFont f;
        f.width = kAtlasWidth;
        f.height = kAtlasHeight;
        f.count = kFontCount;
        f.pixels.resize((size_t)kAtlasWidth * kAtlasHeight);
        size_t o = 0;
        for (size_t i = 0; i + 1 < sizeof kAtlasRuns; i += 2) {
            for (int k = 0; k < kAtlasRuns[i] && o < f.pixels.size(); k++) {
                f.pixels[o++] = kAtlasRuns[i + 1];
            }
        }
        f.metrics = kFontMetrics;
        f.boxes = kGlyphBox;
        f.glyphs = kGlyphMetrics;
        return f;
    }();
    return font;
}

} // namespace r4::detail
