// r4/render/overlay.h - a surface of r4::raster on the GPU, over the scene or as the texture of a mesh. An indexed surface goes up with its palette, looked up by the shader; a surface is sent again only when it changed.
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

    void update(const raster::Surface &surface, const raster::Palette *palette = nullptr);
    void setPalette(const raster::Palette &palette);

    bool smooth = false; // filtered when scaled, else sharp pixels
    float opacity = 1;

    // in the rectangle x, y, w, h, in pixels from the top left of a pass of targetWidth x targetHeight
    void draw(int x, int y, int w, int h, int targetWidth, int targetHeight);

    // the largest centered rectangle of a w x h area showing an image of this aspect (4/3 for 320x200)
    static void fit(float aspect, int areaWidth, int areaHeight, int &x, int &y, int &w, int &h);

    // drawn into a texture when it changed; called before the passes of the frame
    gpu::TextureHandle texture();

private:
    struct Impl;
    Impl *d;
};

} // namespace r4::render
