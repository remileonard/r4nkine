// r4/render/postprocess.h - effects on the whole frame: antialiasing, the look of a CRT, colors, a palette, big pixels.
// begin() gives the target to draw the frame into, null when no effect is on (the frame then goes straight into the window); end() applies the effects into the window.
#pragma once

#include "r4/gpu/device.h"
#include "r4/math.h"
#include "r4/raster/raster.h"

namespace r4::render {

struct PostSettings {
    int msaa = 1;      // samples: 1, 2, 4, 8
    bool fxaa = false; // FXAA 3.11

    // the frame drawn as if it were width / pixelate wide
    int pixelate = 1;

    float brightness = 0; // -1..1, added
    float contrast = 1;   // around the middle grey
    float saturation = 1; // 0: greys
    float gamma = 1;
    Vec3 tint{1, 1, 1}; // multiplied
    Vec3 fadeColor{0, 0, 0};
    float fade = 0; // 0..1 towards fadeColor

    // every pixel takes the nearest color of the palette
    bool quantize = false;
    raster::Palette palette = raster::Palette::vga();
    int paletteSize = 256; // its first colors only
    bool dither = true;    // Bayer 4x4

    bool scanlines = false;
    float scanlineStrength = 0.35f; // how dark the lines between are
    float scanlineSize = 3;         // pixels per line of the screen
    float curvature = 0;            // 0..0.3
    float aberration = 0;           // in pixels, at the edges
    float vignette = 0;             // 0..1
};

class PostProcess {
public:
    explicit PostProcess(gpu::Device &device);
    ~PostProcess();
    PostProcess(const PostProcess &) = delete;
    PostProcess &operator=(const PostProcess &) = delete;

    PostSettings settings;

    bool active() const;
    // made again when the size or the samples change
    gpu::RenderTargetHandle begin(int width, int height);
    // into the rectangle x, y, w, h of the window, in pixels from its top left
    void end(int x, int y, int width, int height, int windowWidth, int windowHeight);

private:
    struct Impl;
    Impl *d;
};

} // namespace r4::render
