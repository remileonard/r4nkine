// r4/render/postprocess.h - effects on the whole frame, after the scene:
// antialiasing (MSAA, FXAA), the look of a CRT (scanlines, curvature,
// aberration, vignette), the color (brightness, contrast, saturation, gamma,
// tint, fade) and the colors of a palette (the frame quantized to 256
// colors or fewer, dithered), and big pixels.
//
//   gpu::RenderTargetHandle target = post.begin(width, height);
//   renderer.setTarget(target);
//   renderer.render(root, camera, 0, 0, width, height);   // into the target
//   overlay.draw(...);                                    // the HUD too, if it is to be processed
//   post.end(x, y, width, height, windowWidth, windowHeight); // the effects, into the window
//
// When no effect is on, begin() returns a null handle: the scene is drawn
// straight into the window and end() does nothing.
#pragma once

#include "r4/gpu/device.h"
#include "r4/math.h"
#include "r4/raster/raster.h"

namespace r4::render {

struct PostSettings {
    // antialiasing
    int msaa = 1;      // samples of the scene: 1, 2, 4, 8
    bool fxaa = false; // the edges smoothed by a shader (FXAA 3.11, the light version)

    // big pixels: the frame drawn as if it were width / pixelate wide
    int pixelate = 1;

    // the colors
    float brightness = 0; // added, -1..1
    float contrast = 1;   // around the middle grey
    float saturation = 1; // 0: greys
    float gamma = 1;
    Vec3 tint{1, 1, 1};   // multiplied
    Vec3 fadeColor{0, 0, 0};
    float fade = 0;       // 0..1 towards fadeColor

    // the palette: every pixel takes the nearest of its colors
    bool quantize = false;
    raster::Palette palette = raster::Palette::vga();
    int paletteSize = 256; // the first colors only (16 for the EGA...)
    bool dither = true;    // ordered dithering (Bayer 4x4) before the choice

    // the CRT
    bool scanlines = false;
    float scanlineStrength = 0.35f; // how dark the lines between are
    float scanlineSize = 3;         // pixels per line of the screen
    float curvature = 0;            // the bulge of the glass, 0..0.3
    float aberration = 0;           // the colors apart at the edges, in pixels
    float vignette = 0;             // the corners darker, 0..1
};

class PostProcess {
public:
    explicit PostProcess(gpu::Device &device);
    ~PostProcess();
    PostProcess(const PostProcess &) = delete;
    PostProcess &operator=(const PostProcess &) = delete;

    PostSettings settings;

    // true when an effect is on
    bool active() const;
    // the target to draw the frame into, at its size (made again when the
    // size or the samples change); null when no effect is on
    gpu::RenderTargetHandle begin(int width, int height);
    // the effects, from the target into the rectangle x, y, w, h of the
    // window (pixels from its top left; the window is windowWidth x windowHeight)
    void end(int x, int y, int width, int height, int windowWidth, int windowHeight);

private:
    struct Impl;
    Impl *d;
};

} // namespace r4::render
