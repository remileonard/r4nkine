// r4/render/overlay.h - a surface of r4::raster on the GPU: over the scene
// (a cockpit, a HUD, a map), or as a texture on a mesh (the screens of a
// virtual cockpit).
//
// An indexed surface goes up as bytes, its palette as 256 colors: the shader
// looks the colors up, so a new palette (a fade, a tint, a cycling) costs
// one small upload. A true-color surface goes up as it is. A surface is sent
// again only when it changed (Surface::version), and the texture follows its
// size.
#pragma once

#include "r4/gpu/device.h"
#include "r4/raster/raster.h"

namespace r4::render {

class Overlay {
public:
    explicit Overlay(gpu::Device &device);
    ~Overlay();
    Overlay(const Overlay &) = delete;
    Overlay &operator=(const Overlay &) = delete;

    // sends the surface when it changed, and the palette of an indexed one
    void update(const raster::Surface &surface, const raster::Palette *palette = nullptr);
    void setPalette(const raster::Palette &palette);

    bool smooth = false; // filtered when scaled (blends the colors, not the indices), else sharp pixels
    float opacity = 1;

    // drawn in the pass in progress, in the rectangle x, y, w, h (pixels from
    // the top left of a pass of targetWidth x targetHeight)
    void draw(int x, int y, int w, int h, int targetWidth, int targetHeight);

    // the largest rectangle of a w x h area showing an image of the aspect
    // given (4/3 for 320x200: the pixels of the VGA were not square),
    // centered: the bands left on the sides or above and below
    static void fit(float aspect, int areaWidth, int areaHeight, int &x, int &y, int &w, int &h);

    // the surface in true colors, for a material: drawn into a texture of its
    // size when it changed. Call it before the passes of the frame.
    gpu::TextureHandle texture();

private:
    struct Impl;
    Impl *d;
};

} // namespace r4::render
