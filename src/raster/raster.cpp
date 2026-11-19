// raster.cpp - the surfaces of r4::raster: drawing on the CPU, indexed or in
// true colors.
#include "r4/raster/raster.h"

#include "../ui/font_data.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

namespace r4::raster {

// --- palettes ----------------------------------------------------------------

namespace {

uint8_t clampByte(float v) {
    return (uint8_t)(v <= 0 ? 0 : v >= 255 ? 255 : v + 0.5f);
}

uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto m = [&](int s) {
        return clampByte(((a >> s) & 0xff) * (1 - t) + ((b >> s) & 0xff) * t);
    };
    return rgba(m(0), m(8), m(16), m(24));
}

} // namespace

Palette Palette::vga() {
    // the default palette of the VGA, mode 13h
    Palette p;
    static const uint8_t ega[16][3] = {{0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170}, {170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170}, {85, 85, 85}, {85, 85, 255}, {85, 255, 85}, {85, 255, 255}, {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}};
    for (int i = 0; i < 16; i++) {
        p.colors[i] = rgba(ega[i][0], ega[i][1], ega[i][2]);
    }
    static const uint8_t greys[16] = {0, 20, 32, 44, 56, 68, 80, 97, 113, 129, 145, 161, 182, 202, 226, 255};
    for (int i = 0; i < 16; i++) {
        p.colors[16 + i] = rgba(greys[i], greys[i], greys[i]);
    }
    // 9 groups of 24 hues: 3 intensities x 3 saturations
    static const float levels[3] = {1.0f, 0.44f, 0.25f};
    static const float sats[3] = {0.0f, 0.5f, 0.72f};
    int i = 32;
    for (float level : levels) {
        for (float sat : sats) {
            for (int h = 0; h < 24; h++) {
                float hue = h / 24.0f * 6;
                int k = (int)hue;
                float f = hue - k;
                float hi = 1;
                float lo = sat;
                float up = lo + (hi - lo) * f;
                float down = hi - (hi - lo) * f;
                // blue, magenta, red, yellow, green, cyan, as the VGA orders them
                float r;
                float g;
                float b;
                switch (k % 6) {
                case 0:
                    r = up;
                    g = lo;
                    b = hi;
                    break;
                case 1:
                    r = hi;
                    g = lo;
                    b = down;
                    break;
                case 2:
                    r = hi;
                    g = up;
                    b = lo;
                    break;
                case 3:
                    r = down;
                    g = hi;
                    b = lo;
                    break;
                case 4:
                    r = lo;
                    g = hi;
                    b = up;
                    break;
                default:
                    r = lo;
                    g = down;
                    b = hi;
                    break;
                }
                p.colors[i++] = rgba(clampByte(r * level * 255), clampByte(g * level * 255), clampByte(b * level * 255));
            }
        }
    }
    for (; i < 256; i++) {
        p.colors[i] = rgba(0, 0, 0);
    }
    return p;
}

Palette Palette::grey() {
    Palette p;
    for (int i = 0; i < 256; i++) {
        p.colors[i] = rgba(i, i, i);
    }
    return p;
}

Palette Palette::ramp(uint32_t a, uint32_t b, int n) {
    Palette p;
    p.colors.fill(rgba(0, 0, 0));
    n = std::max(1, std::min(n, 256));
    for (int i = 0; i < n; i++) {
        p.colors[i] = mix(a, b, n > 1 ? (float)i / (n - 1) : 0);
    }
    return p;
}

int Palette::nearest(uint8_t r, uint8_t g, uint8_t b, int count) const {
    int best = 0;
    long bestD = 1L << 30;
    for (int i = 0; i < std::min(count, 256); i++) {
        long dr = red(colors[i]) - r;
        long dg = green(colors[i]) - g;
        long db = blue(colors[i]) - b;
        // weighted as the eye: green counts most
        long d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

Palette Palette::fadedTo(uint32_t color, float t) const {
    Palette p;
    for (int i = 0; i < 256; i++) {
        p.colors[i] = mix(colors[i], (color & 0x00ffffff) | (colors[i] & 0xff000000), t);
    }
    return p;
}

Palette Palette::blendedWith(const Palette &other, float t) const {
    Palette p;
    for (int i = 0; i < 256; i++) {
        p.colors[i] = mix(colors[i], other.colors[i], t);
    }
    return p;
}

Palette Palette::adjusted(float brightness, float contrast) const {
    Palette p;
    for (int i = 0; i < 256; i++) {
        uint32_t c = colors[i];
        auto f = [&](uint8_t v) {
            return clampByte((v - 128.0f) * contrast + 128.0f + brightness * 255);
        };
        p.colors[i] = rgba(f(red(c)), f(green(c)), f(blue(c)), alpha(c));
    }
    return p;
}

Palette Palette::tinted(uint32_t color, float t) const {
    Palette p;
    for (int i = 0; i < 256; i++) {
        uint32_t c = colors[i];
        // the light of the color, multiplied by the tint
        float l = (0.299f * red(c) + 0.587f * green(c) + 0.114f * blue(c)) / 255;
        uint32_t tinted = rgba(clampByte(l * red(color)), clampByte(l * green(color)), clampByte(l * blue(color)), alpha(c));
        p.colors[i] = mix(c, tinted, t);
    }
    return p;
}

Palette Palette::rotated(int first, int last, int steps) const {
    Palette p = *this;
    first = std::max(0, first);
    last = std::min(255, last);
    int n = last - first + 1;
    if (n <= 1) {
        return p;
    }
    for (int i = 0; i < n; i++) {
        p.colors[first + ((i + steps) % n + n) % n] = colors[first + i];
    }
    return p;
}

// --- fonts ---------------------------------------------------------------------

const Font &Font::builtin(int pixels, bool bold) {
    // the baked fonts: their index, size and weight; the largest one not
    // above the size asked, then enlarged by a whole factor (sharp pixels)
    // the baked fonts (index, size): the largest one not above the size
    // asked; above 32 pixels, the one of 13 (16 bold) enlarged by a whole
    // factor, which keeps the look of the pixels
    struct Baked {
        int index;
        int size;
    };
    static const Baked regular[] = {{2, 11}, {0, 13}, {6, 22}, {4, 26}};
    static const Baked boldOnes[] = {{2, 11}, {1, 13}, {3, 16}, {5, 26}, {7, 32}};
    const Baked *list = bold ? boldOnes : regular;
    int count = bold ? 5 : 4;
    const Baked *pick = &list[0];
    int factor = 1;
    if (pixels > 32) {
        pick = bold ? &boldOnes[2] : &regular[1];
        factor = std::max(1, (pixels + pick->size / 2) / pick->size);
    } else {
        for (int i = 0; i < count; i++) {
            if (list[i].size <= std::max(pixels, 11)) {
                pick = &list[i];
            }
        }
    }
    int key = pick->index * 64 + factor;
    static std::map<int, std::unique_ptr<Font>> fonts;
    auto &slot = fonts[key];
    if (slot) {
        return *slot;
    }
    int index = pick->index;
    const detail::BakedFont &b = detail::bakedFont();
    auto f = std::make_unique<Font>();
    float ascent = b.metrics[index][1];
    float descent = b.metrics[index][2];
    f->height = (int)std::ceil(ascent - descent) * factor;
    for (int code = 0; code < 256; code++) {
        if (code < 32 || (code > 126 && code < 160)) {
            continue;
        }
        int g = detail::glyphIndex(code);
        const short *box = b.boxes[index][g];
        const float *m = b.glyphs[index][g];
        Glyph &gl = f->glyphs[code];
        int w = box[2] - box[0];
        int h = box[3] - box[1];
        gl.width = w * factor;
        gl.height = h * factor;
        gl.xoff = (int)std::lround(m[0]) * factor;
        gl.yoff = (int)std::lround(ascent + m[1]) * factor;
        gl.advance = (int)std::lround(m[2]) * factor;
        gl.mask.resize((size_t)std::max(0, gl.width * gl.height));
        // thresholded: the pixels of the old screens have no shades
        for (int y = 0; y < gl.height; y++) {
            for (int x = 0; x < gl.width; x++) {
                gl.mask[(size_t)y * gl.width + x] = b.pixels[(size_t)(box[1] + y / factor) * b.width + box[0] + x / factor] >= 110;
            }
        }
    }
    slot = std::move(f);
    return *slot;
}

namespace {

// the next Latin-1 code of an UTF-8 string
int nextCode(const char *&s) {
    unsigned char c = (unsigned char)*s++;
    if (c < 0x80) {
        return c;
    }
    if ((c & 0xe0) == 0xc0 && (*s & 0xc0) == 0x80) {
        int code = (c & 0x1f) << 6 | (*s++ & 0x3f);
        return code <= 255 ? code : '?';
    }
    while ((*s & 0xc0) == 0x80) {
        s++;
    }
    return '?';
}

} // namespace

int Font::width(const std::string &text) const {
    int w = 0;
    for (const char *p = text.c_str(); *p;) {
        w += glyphs[nextCode(p)].advance;
    }
    return w;
}

// --- surfaces ------------------------------------------------------------------

Surface::Surface(int width, int height, Format format) : width_(std::max(0, width)), height_(std::max(0, height)), format_(format) {
    data_.assign(pitch() * height_, 0);
    resetClip();
}

void Surface::setClip(int x, int y, int w, int h) {
    cx0_ = std::max(0, x);
    cy0_ = std::max(0, y);
    cx1_ = std::min(width_, x + w);
    cy1_ = std::min(height_, y + h);
}

namespace {

// a true color over another
uint32_t over(uint32_t dst, uint32_t src) {
    uint32_t a = alpha(src);
    if (a == 255) {
        return src;
    }
    if (a == 0) {
        return dst;
    }
    float t = a / 255.0f;
    auto m = [&](int s) {
        return clampByte(((dst >> s) & 0xff) * (1 - t) + ((src >> s) & 0xff) * t);
    };
    uint8_t outA = clampByte(alpha(dst) + a * (1 - alpha(dst) / 255.0f));
    return rgba(m(0), m(8), m(16), outA);
}

} // namespace

void Surface::put(int x, int y, uint32_t c) {
    if (x < cx0_ || x >= cx1_ || y < cy0_ || y >= cy1_) {
        return;
    }
    if (format_ == Format::Indexed8) {
        data_[(size_t)y * width_ + x] = (uint8_t)c;
    } else {
        uint32_t *p = reinterpret_cast<uint32_t *>(data_.data()) + (size_t)y * width_ + x;
        *p = blend ? over(*p, c) : c;
    }
}

void Surface::span(int x0, int x1, int y, uint32_t c) {
    if (y < cy0_ || y >= cy1_) {
        return;
    }
    if (x0 > x1) {
        std::swap(x0, x1);
    }
    x0 = std::max(x0, cx0_);
    x1 = std::min(x1, cx1_ - 1);
    if (x0 > x1) {
        return;
    }
    if (format_ == Format::Indexed8) {
        std::memset(&data_[(size_t)y * width_ + x0], (uint8_t)c, (size_t)(x1 - x0 + 1));
    } else {
        uint32_t *p = reinterpret_cast<uint32_t *>(data_.data()) + (size_t)y * width_;
        if (blend && alpha(c) != 255) {
            for (int x = x0; x <= x1; x++) {
                p[x] = over(p[x], c);
            }
        } else {
            std::fill(p + x0, p + x1 + 1, c);
        }
    }
}

void Surface::clear(uint32_t color) {
    touch();
    if (format_ == Format::Indexed8) {
        std::fill(data_.begin(), data_.end(), (uint8_t)color);
    } else {
        std::fill(reinterpret_cast<uint32_t *>(data_.data()), reinterpret_cast<uint32_t *>(data_.data()) + (size_t)width_ * height_, color);
    }
}

void Surface::pixel(int x, int y, uint32_t color) {
    touch();
    put(x, y, color);
}

uint32_t Surface::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return 0;
    }
    if (format_ == Format::Indexed8) {
        return data_[(size_t)y * width_ + x];
    }
    return reinterpret_cast<const uint32_t *>(data_.data())[(size_t)y * width_ + x];
}

void Surface::hline(int x0, int x1, int y, uint32_t color) {
    touch();
    span(x0, x1, y, color);
}

void Surface::vline(int x, int y0, int y1, uint32_t color) {
    touch();
    if (y0 > y1) {
        std::swap(y0, y1);
    }
    for (int y = y0; y <= y1; y++) {
        put(x, y, color);
    }
}

void Surface::line(int x0, int y0, int x1, int y1, uint32_t color) {
    lineDashed(x0, y0, x1, y1, color, 1, 0);
}

void Surface::lineDashed(int x0, int y0, int x1, int y1, uint32_t color, int on, int off) {
    touch();
    // Bresenham
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int period = std::max(1, on + off);
    int step = 0;
    while (true) {
        if (off <= 0 || step % period < on) {
            put(x0, y0, color);
        }
        step++;
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Surface::lineThick(int x0, int y0, int x1, int y1, uint32_t color, int thickness) {
    if (thickness <= 1) {
        line(x0, y0, x1, y1, color);
        return;
    }
    // a quad around the line, and round ends
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float l = std::sqrt(dx * dx + dy * dy);
    float h = thickness * 0.5f;
    if (l > 0) {
        float nx = -dy / l * h;
        float ny = dx / l * h;
        int xy[8] = {(int)std::lround(x0 + nx), (int)std::lround(y0 + ny), (int)std::lround(x1 + nx), (int)std::lround(y1 + ny), (int)std::lround(x1 - nx), (int)std::lround(y1 - ny), (int)std::lround(x0 - nx), (int)std::lround(y0 - ny)};
        fillPolygon(xy, 4, color);
    }
    int r = std::max(0, (thickness - 1) / 2);
    fillCircle(x0, y0, r, color);
    fillCircle(x1, y1, r, color);
}

void Surface::rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    touch();
    span(x, x + w - 1, y, color);
    span(x, x + w - 1, y + h - 1, color);
    for (int yy = y + 1; yy < y + h - 1; yy++) {
        put(x, yy, color);
        put(x + w - 1, yy, color);
    }
}

void Surface::fillRect(int x, int y, int w, int h, uint32_t color) {
    touch();
    for (int yy = y; yy < y + h; yy++) {
        span(x, x + w - 1, yy, color);
    }
}

void Surface::circle(int cx, int cy, int r, uint32_t color) {
    ellipse(cx, cy, r, r, color);
}
void Surface::fillCircle(int cx, int cy, int r, uint32_t color) {
    fillEllipse(cx, cy, r, r, color);
}

void Surface::ellipse(int cx, int cy, int rx, int ry, uint32_t color) {
    touch();
    if (rx <= 0 || ry <= 0) {
        line(cx - rx, cy - ry, cx + rx, cy + ry, color);
        return;
    }
    // the midpoint algorithm, in two regions
    long a2 = (long)rx * rx;
    long b2 = (long)ry * ry;
    long x = 0;
    long y = ry;
    long px = 0;
    long py = 2 * a2 * y;
    auto plot4 = [&](long X, long Y) {
        put(cx + (int)X, cy + (int)Y, color);
        put(cx - (int)X, cy + (int)Y, color);
        put(cx + (int)X, cy - (int)Y, color);
        put(cx - (int)X, cy - (int)Y, color);
    };
    plot4(x, y);
    double p = b2 - a2 * ry + 0.25 * a2;
    while (px < py) {
        x++;
        px += 2 * b2;
        if (p < 0) {
            p += b2 + px;
        } else {
            y--;
            py -= 2 * a2;
            p += b2 + px - py;
        }
        plot4(x, y);
    }
    p = b2 * (x + 0.5) * (x + 0.5) + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y > 0) {
        y--;
        py -= 2 * a2;
        if (p > 0) {
            p += a2 - py;
        } else {
            x++;
            px += 2 * b2;
            p += a2 - py + px;
        }
        plot4(x, y);
    }
}

void Surface::fillEllipse(int cx, int cy, int rx, int ry, uint32_t color) {
    touch();
    if (rx < 0 || ry < 0) {
        return;
    }
    for (int y = -ry; y <= ry; y++) {
        float t = ry > 0 ? (float)y / ry : 0;
        int w = (int)std::floor(rx * std::sqrt(std::max(0.0f, 1 - t * t)) + 0.5f);
        span(cx - w, cx + w, cy + y, color);
    }
}

void Surface::fillPolygon(const int *xy, int count, uint32_t color) {
    if (count < 3) {
        return;
    }
    touch();
    int ymin = xy[1];
    int ymax = xy[1];
    for (int i = 1; i < count; i++) {
        ymin = std::min(ymin, xy[2 * i + 1]);
        ymax = std::max(ymax, xy[2 * i + 1]);
    }
    ymin = std::max(ymin, cy0_);
    ymax = std::min(ymax, cy1_ - 1);
    std::vector<float> xs;
    for (int y = ymin; y <= ymax; y++) {
        // the crossings of the edges with the center of the row
        float fy = y + 0.5f;
        xs.clear();
        for (int i = 0; i < count; i++) {
            int j = (i + 1) % count;
            float y0 = (float)xy[2 * i + 1];
            float y1 = (float)xy[2 * j + 1];
            if ((y0 <= fy && y1 > fy) || (y1 <= fy && y0 > fy)) {
                float x0 = (float)xy[2 * i];
                float x1 = (float)xy[2 * j];
                xs.push_back(x0 + (fy - y0) / (y1 - y0) * (x1 - x0));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int a = (int)std::ceil(xs[k] - 0.5f);
            int b = (int)std::floor(xs[k + 1] - 0.5f);
            if (a <= b) {
                span(a, b, y, color);
            }
        }
    }
}

void Surface::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    int xy[6] = {x0, y0, x1, y1, x2, y2};
    fillPolygon(xy, 3, color);
}

void Surface::blit(const Surface &src, int dx, int dy, int sx, int sy, int sw, int sh, int colorOffset, const Palette *palette) {
    touch();
    if (sw <= 0) {
        sw = src.width_ - sx;
    }
    if (sh <= 0) {
        sh = src.height_ - sy;
    }
    for (int y = 0; y < sh; y++) {
        int ty = dy + y;
        int fy = sy + y;
        if (ty < cy0_ || ty >= cy1_ || fy < 0 || fy >= src.height_) {
            continue;
        }
        for (int x = 0; x < sw; x++) {
            int tx = dx + x;
            int fx = sx + x;
            if (tx < cx0_ || tx >= cx1_ || fx < 0 || fx >= src.width_) {
                continue;
            }
            uint32_t c = src.get(fx, fy);
            if (src.format_ == Format::Indexed8) {
                if ((int)c == src.transparentIndex) {
                    continue;
                }
                if (format_ == Format::Indexed8) {
                    put(tx, ty, (c + colorOffset) & 0xff);
                } else {
                    put(tx, ty, palette ? palette->colors[(c + colorOffset) & 0xff] : rgba(c, c, c));
                }
            } else if (format_ == Format::RGBA8) {
                put(tx, ty, c);
            } else if (palette) {
                // true colors into an indexed surface: the nearest of the palette
                if (alpha(c) >= 128) {
                    put(tx, ty, palette->nearest(red(c), green(c), blue(c)));
                }
            }
        }
    }
}

void Surface::blitRaw(const uint8_t *src, int sw, int sh, size_t srcPitch, int dx, int dy, int key) {
    touch();
    int bpp = bytesPerPixel();
    for (int y = 0; y < sh; y++) {
        int ty = dy + y;
        if (ty < cy0_ || ty >= cy1_) {
            continue;
        }
        const uint8_t *row = src + (size_t)y * srcPitch;
        for (int x = 0; x < sw; x++) {
            int tx = dx + x;
            if (tx < cx0_ || tx >= cx1_) {
                continue;
            }
            uint32_t c;
            if (bpp == 1) {
                c = row[x];
            } else {
                std::memcpy(&c, row + 4 * x, 4);
            }
            if (key >= 0 && c == (uint32_t)key) {
                continue;
            }
            put(tx, ty, c);
        }
    }
}

void Surface::blitScaled(const Surface &src, int dx, int dy, int dw, int dh, const Palette *palette) {
    if (dw <= 0 || dh <= 0 || src.width_ <= 0 || src.height_ <= 0) {
        return;
    }
    touch();
    for (int y = 0; y < dh; y++) {
        int ty = dy + y;
        if (ty < cy0_ || ty >= cy1_) {
            continue;
        }
        int fy = (int)((long)y * src.height_ / dh);
        for (int x = 0; x < dw; x++) {
            int tx = dx + x;
            if (tx < cx0_ || tx >= cx1_) {
                continue;
            }
            int fx = (int)((long)x * src.width_ / dw);
            uint32_t c = src.get(fx, fy);
            if (src.format_ == Format::Indexed8) {
                if ((int)c == src.transparentIndex) {
                    continue;
                }
                put(tx, ty, format_ == Format::Indexed8 ? c : palette ? palette->colors[c] : rgba(c, c, c));
            } else if (format_ == Format::RGBA8) {
                put(tx, ty, c);
            } else if (palette && alpha(c) >= 128) {
                put(tx, ty, palette->nearest(red(c), green(c), blue(c)));
            }
        }
    }
}

void Surface::scroll(int dx, int dy, uint32_t fill) {
    touch();
    int w = cx1_ - cx0_;
    int h = cy1_ - cy0_;
    if (w <= 0 || h <= 0) {
        return;
    }
    int bpp = bytesPerPixel();
    std::vector<uint8_t> copy((size_t)w * h * bpp);
    for (int y = 0; y < h; y++) {
        std::memcpy(&copy[(size_t)y * w * bpp], &data_[((size_t)(cy0_ + y) * width_ + cx0_) * bpp], (size_t)w * bpp);
    }
    bool saveBlend = blend;
    blend = false;
    for (int y = 0; y < h; y++) {
        span(cx0_, cx1_ - 1, cy0_ + y, fill);
    }
    for (int y = 0; y < h; y++) {
        int ty = y + dy;
        if (ty < 0 || ty >= h) {
            continue;
        }
        int x0 = std::max(0, dx);
        int x1 = std::min(w, w + dx); // the target columns
        if (x0 >= x1) {
            continue;
        }
        std::memcpy(&data_[((size_t)(cy0_ + ty) * width_ + cx0_ + x0) * bpp], &copy[((size_t)y * w + (x0 - dx)) * bpp], (size_t)(x1 - x0) * bpp);
    }
    blend = saveBlend;
}

int Surface::text(const Font &font, int x, int y, const std::string &text, uint32_t color) {
    touch();
    int pen = x;
    for (const char *p = text.c_str(); *p;) {
        const Font::Glyph &g = font.glyphs[nextCode(p)];
        for (int gy = 0; gy < g.height; gy++) {
            for (int gx = 0; gx < g.width; gx++) {
                if (g.mask[(size_t)gy * g.width + gx]) {
                    put(pen + g.xoff + gx, y + g.yoff + gy, color);
                }
            }
        }
        pen += g.advance;
    }
    return pen - x;
}

int Surface::text(const Font &font, int x, int y, const std::string &text, uint32_t color, Align align) {
    int w = font.width(text);
    if (align == Align::Center) {
        x -= w / 2;
    } else if (align == Align::Right) {
        x -= w;
    }
    return this->text(font, x, y, text, color);
}

void Surface::toRGBA(const Palette *palette, uint8_t *out, bool bottomUp) const {
    for (int y = 0; y < height_; y++) {
        uint8_t *row = out + (size_t)(bottomUp ? height_ - 1 - y : y) * width_ * 4;
        if (format_ == Format::RGBA8) {
            std::memcpy(row, &data_[(size_t)y * width_ * 4], (size_t)width_ * 4);
            continue;
        }
        for (int x = 0; x < width_; x++) {
            uint8_t i = data_[(size_t)y * width_ + x];
            uint32_t c = palette ? palette->colors[i] : rgba(i, i, i);
            if ((int)i == transparentIndex) {
                c &= 0x00ffffff;
            }
            std::memcpy(row + 4 * x, &c, 4);
        }
    }
}

} // namespace r4::raster
