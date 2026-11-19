// r4/assets/upscale.h - the pixel art scalers, to enlarge the small textures
// of the old games once, at load, before mipmaps and anisotropic filtering.
//
// - 2xSaI (Kreed, 1999): doubles, keeps the lines sharp, interpolates the rest;
// - xBR (Hyllian, 2011): finds the edges by the colors (YUV) around each pixel
//   and blends along them, rounder than 2xSaI; 4x is 2x twice.
// The images are read as RGBA (grey ones expanded) and come out RGBA. The
// alpha counts as a color: a clear texel against an opaque one is an edge,
// kept sharp (the holes of the textures of the 8-bit games). The edges of the
// image are clamped or repeated as its clampS / clampT.
#pragma once

#include "r4/assets/model.h"

namespace r4::assets {

enum class Upscale {
    NONE,
    SAI_2X,
    XBR_2X,
    XBR_4X
};

// the factor of a scaler (1, 2 or 4)
int upscaleFactor(Upscale u);
// the name for the menus: "None", "2xSaI", "xBR 2x", "xBR 4x"
const char *upscaleName(Upscale u);

// the image enlarged; its other fields (name, wrap, mipmaps) kept. An image
// that is not valid comes back as it is.
Image upscale(const Image &image, Upscale u);

} // namespace r4::assets
