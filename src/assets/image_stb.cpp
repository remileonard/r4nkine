// image_stb.cpp - PNG, JPEG, BMP, TGA and GIF with stb_image.
#include "r4/assets/model.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstring>

namespace r4::assets {

// the image from memory, rows from the bottom as R4nkine keeps them
bool decodeImageStb(const uint8_t *data, size_t size, Image &img, std::string *error) {
    int w;
    int h;
    int n;
    unsigned char *p = stbi_load_from_memory(data, (int)size, &w, &h, &n, 0);
    if (!p) {
        if (error) {
            *error = std::string("cannot decode the image: ") + stbi_failure_reason();
        }
        return false;
    }
    img.width = w;
    img.height = h;
    img.channels = n;
    img.pixels.resize((size_t)w * h * n);
    size_t row = (size_t)w * n;
    for (int y = 0; y < h; y++) {
        std::memcpy(&img.pixels[(size_t)(h - 1 - y) * row], p + (size_t)y * row, row);
    }
    stbi_image_free(p);
    return true;
}

} // namespace r4::assets
