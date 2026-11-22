// bake_font.cpp - bakes the Latin-1 glyphs of r4::ui into one 8-bit atlas, src/ui/font_data.inc (committed; needs stb_truetype).
// Usage: bake_font LiberationSans-Regular.ttf LiberationSans-Bold.ttf > src/ui/font_data.inc
//
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>
#include <stb_truetype.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// a float literal for C++: %g, with the f suffix when it has a decimal point
// (an integer such as 3 stays 3, since 3f is not a literal)
static std::string lit(float v, const char *fmt = "%g") {
    char b[32];
    std::snprintf(b, sizeof b, fmt, v);
    std::string s = b;
    if (s.find('.') != std::string::npos) {
        s += 'f';
    }
    return s;
}

struct Face {
    int file;
    float px;
    const char *name;
};

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: bake_font regular.ttf bold.ttf\n");
        return 1;
    }
    std::vector<std::vector<unsigned char>> ttf(2);
    for (int i = 0; i < 2; i++) {
        FILE *f = std::fopen(argv[1 + i], "rb");
        if (!f) {
            return 1;
        }
        std::fseek(f, 0, SEEK_END);
        ttf[i].resize(std::ftell(f));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(ttf[i].data(), 1, ttf[i].size(), f) != ttf[i].size()) {
            return 1;
        }
        std::fclose(f);
    }
    const Face faces[] = {{0, 13, "regular"}, {1, 13, "bold"}, {0, 11, "small"}, {1, 16, "title"}, {0, 26, "regular 2x"}, {1, 26, "bold 2x"}, {0, 22, "small 2x"}, {1, 32, "title 2x"}};
    const int nf = 8;
    const int W = 1024;
    const int H = 256;
    std::vector<unsigned char> atlas(W * H);
    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, atlas.data(), W, H - 4, W, 1, nullptr);
    stbtt_PackSetOversampling(&pc, 1, 1);
    std::vector<stbtt_packedchar> chars[nf][2];
    for (int f = 0; f < nf; f++) {
        chars[f][0].resize(95);
        chars[f][1].resize(96);
        stbtt_pack_range r[2] = {};
        r[0].font_size = faces[f].px;
        r[0].first_unicode_codepoint_in_range = 32;
        r[0].num_chars = 95;
        r[0].chardata_for_range = chars[f][0].data();
        r[1].font_size = faces[f].px;
        r[1].first_unicode_codepoint_in_range = 160;
        r[1].num_chars = 96;
        r[1].chardata_for_range = chars[f][1].data();
        if (!stbtt_PackFontRanges(&pc, ttf[faces[f].file].data(), 0, r, 2)) {
            std::fprintf(stderr, "atlas too small\n");
            return 1;
        }
    }
    stbtt_PackEnd(&pc);
    atlas[(H - 1) * W + W - 1] = 255;
    atlas[(H - 1) * W + W - 2] = 255;
    atlas[(H - 2) * W + W - 1] = 255;
    atlas[(H - 2) * W + W - 2] = 255;

    std::printf("// font_data.inc - made by tools/bake_font.cpp from Liberation Sans 2.1\n"
                "// (Regular and Bold). The glyphs are under the SIL Open Font License 1.1:\n"
                "// see src/ui/OFL.txt. Do not edit.\n\n");
    std::printf("constexpr int kAtlasWidth = %d, kAtlasHeight = %d;\n", W, H);
    std::printf("constexpr int kFontCount = %d;\n", nf);
    std::printf("// size in pixels, ascent, descent, line gap\nconstexpr float kFontMetrics[kFontCount][4] = {\n");
    for (int f = 0; f < nf; f++) {
        stbtt_fontinfo info;
        stbtt_InitFont(&info, ttf[faces[f].file].data(), 0);
        int a;
        int d;
        int g;
        stbtt_GetFontVMetrics(&info, &a, &d, &g);
        float s = stbtt_ScaleForPixelHeight(&info, faces[f].px);
        std::printf("    {%s, %s, %s, %s}, // %s\n", lit(faces[f].px).c_str(), lit(a * s, "%.3f").c_str(), lit(d * s, "%.3f").c_str(), lit(g * s, "%.3f").c_str(), faces[f].name);
    }
    std::printf("};\n");
    std::printf("// per font, 191 glyphs (32-126 then 160-255): x0 y0 x1 y1 (atlas, rows from the top), xoff yoff xadvance\n");
    std::printf("constexpr short kGlyphBox[kFontCount][191][4] = {\n");
    for (int f = 0; f < nf; f++) {
        std::printf("{");
        for (int r = 0; r < 2; r++) {
            for (auto &c : chars[f][r]) {
                std::printf("{%d,%d,%d,%d},", c.x0, c.y0, c.x1, c.y1);
            }
        }
        std::printf("},\n");
    }
    std::printf("};\nconstexpr float kGlyphMetrics[kFontCount][191][3] = {\n");
    for (int f = 0; f < nf; f++) {
        std::printf("{");
        for (int r = 0; r < 2; r++) {
            for (auto &c : chars[f][r]) {
                std::printf("{%s,%s,%s},", lit(c.xoff).c_str(), lit(c.yoff).c_str(), lit(c.xadvance).c_str());
            }
        }
        std::printf("},\n");
    }
    std::printf("};\n");
    std::vector<int> rle;
    for (size_t i = 0; i < atlas.size();) {
        size_t j = i;
        while (j < atlas.size() && atlas[j] == atlas[i] && j - i < 255) {
            j++;
        }
        rle.push_back((int)(j - i));
        rle.push_back(atlas[i]);
        i = j;
    }
    std::printf("// the atlas, rows from the top, in runs: count, value\n");
    std::printf("constexpr unsigned char kAtlasRuns[%zu] = {", rle.size());
    for (size_t i = 0; i < rle.size(); i++) {
        std::printf("%s%d,", i % 32 ? "" : "\n", rle[i]);
    }
    std::printf("\n};\n");
    return 0;
}
