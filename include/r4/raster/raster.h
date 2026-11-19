// r4/raster/raster.h - 2D drawing on the CPU, in the memory of a surface:
// the cockpits, the screens of the instruments, the maps of the old games,
// and any 2D layer of a program. Nothing here touches the GPU:
// r4::render::Overlay shows a surface over the scene or on a mesh.
//
// A surface has any size (320x200 as a VGA screen, 1920x1080 or more) and
// one of two formats:
//   - Indexed8: a byte per pixel, an index in a palette of 256 colors; the
//     palette is applied when the surface is shown, so the palette effects
//     (fades, tints, cycling) cost nothing;
//   - RGBA8: true colors, 4 bytes per pixel, with alpha blending if asked.
// The colors given to the drawing calls are indices on Indexed8 surfaces
// and rgba() values on RGBA8 ones. Rows start at the top, y goes down.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace r4::raster {

// a true color: R in the low byte, then G, B, A (the byte order of RGBA8)
inline constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24;
}
inline constexpr uint8_t red(uint32_t c) {
    return c & 0xff;
}
inline constexpr uint8_t green(uint32_t c) {
    return c >> 8 & 0xff;
}
inline constexpr uint8_t blue(uint32_t c) {
    return c >> 16 & 0xff;
}
inline constexpr uint8_t alpha(uint32_t c) {
    return c >> 24 & 0xff;
}

// 256 true colors
struct Palette {
    std::array<uint32_t, 256> colors{};

    // the 256 colors of the VGA (mode 13h): 16 EGA, 16 greys, 216 hues
    static Palette vga();
    // a ramp of greys, from black to white
    static Palette grey();
    // n colors spread over a ramp from a to b (the rest black): the
    // 4 greens of a handheld, the amber of a terminal...
    static Palette ramp(uint32_t a, uint32_t b, int n);

    // the index of the color nearest to r, g, b (among the first count)
    int nearest(uint8_t r, uint8_t g, uint8_t b, int count = 256) const;

    // new palettes, for the effects: t from 0 (this one) to 1
    Palette fadedTo(uint32_t color, float t) const;
    Palette blendedWith(const Palette &other, float t) const;
    Palette adjusted(float brightness, float contrast) const; // brightness added (-1..1), contrast multiplied
    Palette tinted(uint32_t color, float t) const;
    // the colors first..last turned by steps (palette cycling: water, fire)
    Palette rotated(int first, int last, int steps) const;
};

// a font of bitmaps: one mask per character of Latin-1
struct Font {
    struct Glyph {
        int width = 0;
        int height = 0;
        int xoff = 0; // from the pen, yoff from the top of the line
        int yoff = 0;
        int advance = 0;
        std::vector<uint8_t> mask; // width x height, 0 or 1, rows from the top
    };
    int height = 0; // of a line
    std::array<Glyph, 256> glyphs;

    // the fonts that come with R4nkine (Liberation Sans, thresholded): 11,
    // 13, 16, 22, 26 and 32 pixels; larger ones are a small one enlarged by
    // a whole factor, as the pixels of the old screens
    static const Font &builtin(int pixels = 13, bool bold = false);
    int width(const std::string &text) const;
};

enum class Format {
    Indexed8,
    RGBA8
};

class Surface {
public:
    Surface() = default;
    Surface(int width, int height, Format format = Format::Indexed8);

    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }
    Format format() const {
        return format_;
    }
    int bytesPerPixel() const {
        return format_ == Format::Indexed8 ? 1 : 4;
    }
    size_t pitch() const {
        return (size_t)width_ * bytesPerPixel();
    } // bytes per row
    uint8_t *pixels() {
        return data_.data();
    }
    const uint8_t *pixels() const {
        return data_.data();
    }
    // a counter that changes with every drawing: the overlay uploads only
    // the surfaces that changed
    uint64_t version() const {
        return version_;
    }
    void touch() {
        version_++;
    }

    // the drawing stays in this rectangle
    void setClip(int x, int y, int w, int h);
    void resetClip() {
        setClip(0, 0, width_, height_);
    }

    // the color of the indexed surfaces that is not drawn by blits (-1: none);
    // the overlay shows it transparent
    int transparentIndex = 255;
    // RGBA8: the colors with an alpha below 255 are blended over what is there
    bool blend = false;

    // --- drawing ------------------------------------------------------------
    void clear(uint32_t color);
    void pixel(int x, int y, uint32_t color);
    uint32_t get(int x, int y) const; // 0 outside
    void hline(int x0, int x1, int y, uint32_t color);
    void vline(int x, int y0, int y1, uint32_t color);
    void line(int x0, int y0, int x1, int y1, uint32_t color);
    void lineThick(int x0, int y0, int x1, int y1, uint32_t color, int thickness);
    // a line drawn on, off, on...: the dashes of the radar and the HUD
    void lineDashed(int x0, int y0, int x1, int y1, uint32_t color, int on, int off);
    void rect(int x, int y, int w, int h, uint32_t color);
    void fillRect(int x, int y, int w, int h, uint32_t color);
    void circle(int cx, int cy, int r, uint32_t color);
    void fillCircle(int cx, int cy, int r, uint32_t color);
    void ellipse(int cx, int cy, int rx, int ry, uint32_t color);
    void fillEllipse(int cx, int cy, int rx, int ry, uint32_t color);
    // any polygon, filled by the even-odd rule
    void fillPolygon(const int *xy, int count, uint32_t color);
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);

    // a part of another surface (all of it when sw or sh is 0) at dx, dy.
    // Indexed into indexed: the transparent index of src is skipped and
    // colorOffset added to the others. Indexed into RGBA8: through the
    // palette. RGBA8 into RGBA8: blended when blend is on.
    void blit(const Surface &src, int dx, int dy, int sx = 0, int sy = 0, int sw = 0, int sh = 0, int colorOffset = 0, const Palette *palette = nullptr);
    // the same, from memory laid out as this surface (pitch in bytes)
    void blitRaw(const uint8_t *src, int sw, int sh, size_t srcPitch, int dx, int dy, int key = -1);
    // a part of src scaled into a rectangle (nearest pixels)
    void blitScaled(const Surface &src, int dx, int dy, int dw, int dh, const Palette *palette = nullptr);
    // the content moved by dx, dy inside the clip; what is uncovered takes fill
    void scroll(int dx, int dy, uint32_t fill);

    // text: x is the left of the pen, y the top of the line; the width drawn
    int text(const Font &font, int x, int y, const std::string &text, uint32_t color);
    enum class Align {
        Left,
        Center,
        Right
    };
    int text(const Font &font, int x, int y, const std::string &text, uint32_t color, Align align);

    // RGBA8 pixels of the surface, through the palette for an indexed one;
    // rows from the top, or from the bottom (OpenGL order)
    void toRGBA(const Palette *palette, uint8_t *out, bool bottomUp = false) const;

private:
    void put(int x, int y, uint32_t c); // clipped
    void span(int x0, int x1, int y, uint32_t c);

    int width_ = 0;
    int height_ = 0;
    Format format_ = Format::Indexed8;
    std::vector<uint8_t> data_;
    int cx0_ = 0; // the clip, x1 and y1 excluded
    int cy0_ = 0;
    int cx1_ = 0;
    int cy1_ = 0;
    uint64_t version_ = 0;
};

} // namespace r4::raster
