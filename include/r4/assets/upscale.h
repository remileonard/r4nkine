// r4/assets/upscale.h - pixel art scalers to enlarge textures once at load: 2xSaI, xBR 2x, xBR 4x.
// The alpha counts as a color, so the clear holes keep sharp edges; the edges of the image clamped or repeated as its clampS / clampT.
#pragma once

#include "r4/assets/model.h"

namespace r4::assets {

enum class Upscale {
    NONE,
    SAI_2X,
    XBR_2X,
    XBR_4X
};

int upscaleFactor(Upscale u);
const char *upscaleName(Upscale u);

// an invalid image comes back as it is
Image upscale(const Image &image, Upscale u);

} // namespace r4::assets
