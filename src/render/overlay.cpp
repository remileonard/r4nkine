// overlay.cpp - a raster surface shown by the GPU: the palette applied in the
// shader for the indexed surfaces.
#include "r4/render/overlay.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <vector>

namespace r4::render {

namespace {

const char *kVertex = R"(#version 330
layout(std140) uniform Overlay {
    vec4 rect;   // x0 y0 x1 y1 in clip space: x0 y0 where the first row starts
    vec4 params; // indexed, transparent index, opacity, smooth
    vec4 size;   // the surface: width, height
};
out vec2 vUV;
void main() {
    // two triangles from the vertex index
    int i = gl_VertexID;
    int c = i < 3 ? i : (i == 3 ? 2 : (i == 4 ? 1 : 3));
    vec2 t = vec2(c & 1, c >> 1);
    vUV = t; // v = 0 on the first row of the surface (its top)
    gl_Position = vec4(mix(rect.x, rect.z, t.x), mix(rect.y, rect.w, t.y), 0.0, 1.0);
}
)";

const char *kFragment = R"(#version 330
layout(std140) uniform Overlay {
    vec4 rect;
    vec4 params;
    vec4 size;
};
uniform sampler2D image;   // the indices (R8) or the colors (RGBA8), first row at v = 0
uniform sampler2D palette; // 256 x 1
in vec2 vUV;
out vec4 fragColor;
vec4 lookup(ivec2 p) {
    p = clamp(p, ivec2(0), ivec2(size.xy) - 1);
    int index = int(texelFetch(image, p, 0).r * 255.0 + 0.5);
    vec4 c = texelFetch(palette, ivec2(index, 0), 0);
    if (index == int(params.y))
        c.a = 0.0;
    return c;
}
void main() {
    vec4 c;
    if (params.x > 0.5) {
        vec2 p = vUV * size.xy;
        if (params.w > 0.5) {
            // the 4 nearest pixels looked up, then mixed: the colors are
            // filtered, not the indices
            vec2 q = p - 0.5;
            ivec2 b = ivec2(floor(q));
            vec2 f = q - vec2(b);
            c = mix(mix(lookup(b), lookup(b + ivec2(1, 0)), f.x),
                    mix(lookup(b + ivec2(0, 1)), lookup(b + ivec2(1, 1)), f.x), f.y);
        } else {
            c = lookup(ivec2(floor(p)));
        }
    } else {
        c = texture(image, vUV);
    }
    fragColor = vec4(c.rgb, c.a * params.z);
}
)";

struct Block {
    float rect[4];
    float params[4];
    float size[4];
};

} // namespace

struct Overlay::Impl {
    gpu::Device &device;
    gpu::ShaderHandle shader;
    gpu::PipelineHandle pipeline;
    gpu::BufferHandle ubo;
    gpu::TextureHandle image, palette;
    int width = 0, height = 0;
    raster::Format format = raster::Format::Indexed8;
    bool smoothTexture = false;
    int transparent = -1;
    uint64_t version = ~0ull;
    const raster::Surface *last = nullptr;
    // the texture for the materials
    gpu::RenderTargetHandle target;
    bool targetDirty = true;

    explicit Impl(gpu::Device &d) : device(d) {}

    void makeImage(int w, int h, raster::Format f, bool smoothFilter) {
        if (image)
            device.destroyTexture(image);
        gpu::TextureDesc td;
        td.width = w;
        td.height = h;
        td.format = f == raster::Format::Indexed8 ? gpu::TextureFormat::R8 : gpu::TextureFormat::RGBA8;
        // indices are never filtered by the sampler; true colors when smooth
        td.minFilter = td.magFilter =
            f == raster::Format::RGBA8 && smoothFilter ? gpu::Filter::Linear : gpu::Filter::Nearest;
        td.mipmaps = false;
        td.wrapS = td.wrapT = gpu::Wrap::Clamp;
        image = device.createTexture(td, nullptr);
        width = w;
        height = h;
        format = f;
        smoothTexture = smoothFilter;
        if (target) {
            device.destroyRenderTarget(target);
            target = {};
        }
    }

    void drawQuad(float x0, float y0, float x1, float y1, float opacity) {
        Block b{{x0, y0, x1, y1},
                {format == raster::Format::Indexed8 ? 1.0f : 0.0f, (float)transparent, opacity,
                 smoothTexture ? 1.0f : 0.0f},
                {(float)width, (float)height, 0, 0}};
        device.updateBuffer(ubo, 0, &b, sizeof b);
        device.bindPipeline(pipeline);
        device.bindUniformBuffer(0, ubo, 0, sizeof b);
        device.bindTexture(0, image);
        device.bindTexture(1, palette);
        device.draw(6);
    }
};

Overlay::Overlay(gpu::Device &device) : d(new Impl(device)) {
    gpu::ShaderDesc sd;
    sd.debugName = "r4::render::Overlay";
    sd.vertex = kVertex;
    sd.fragment = kFragment;
    sd.uniformBlocks = {{"Overlay", 0}};
    sd.samplers = {{"image", 0}, {"palette", 1}};
    d->shader = device.createShader(sd);
    gpu::PipelineDesc pd;
    pd.shader = d->shader;
    pd.cull = gpu::CullMode::None;
    pd.blend = gpu::BlendMode::Alpha;
    pd.depthTest = pd.depthWrite = false;
    d->pipeline = device.createPipeline(pd);
    Block b{};
    d->ubo = device.createBuffer(gpu::BufferUsage::Uniform, &b, sizeof b, true);
    gpu::TextureDesc td;
    td.width = 256;
    td.height = 1;
    td.format = gpu::TextureFormat::RGBA8;
    td.minFilter = td.magFilter = gpu::Filter::Nearest;
    td.mipmaps = false;
    td.wrapS = td.wrapT = gpu::Wrap::Clamp;
    raster::Palette grey = raster::Palette::grey();
    d->palette = device.createTexture(td, grey.colors.data());
}

Overlay::~Overlay() {
    gpu::Device &dev = d->device;
    if (d->target)
        dev.destroyRenderTarget(d->target);
    if (d->image)
        dev.destroyTexture(d->image);
    dev.destroyTexture(d->palette);
    dev.destroyBuffer(d->ubo);
    dev.destroyPipeline(d->pipeline);
    dev.destroyShader(d->shader);
    delete d;
}

void Overlay::setPalette(const raster::Palette &palette) {
    d->device.updateTexture(d->palette, 0, 0, 256, 1, palette.colors.data());
    d->targetDirty = true;
}

void Overlay::update(const raster::Surface &s, const raster::Palette *palette) {
    if (palette)
        setPalette(*palette);
    if (s.width() <= 0 || s.height() <= 0)
        return;
    if (!d->image || s.width() != d->width || s.height() != d->height || s.format() != d->format ||
        (s.format() == raster::Format::RGBA8 && smooth != d->smoothTexture)) {
        d->makeImage(s.width(), s.height(), s.format(), smooth);
        d->version = ~0ull;
    }
    d->smoothTexture = smooth;
    d->transparent = s.format() == raster::Format::Indexed8 ? s.transparentIndex : -1;
    if (&s == d->last && s.version() == d->version)
        return;
    // the rows as they are, the first one at v = 0: the shader reads them so
    d->device.updateTexture(d->image, 0, 0, s.width(), s.height(), s.pixels());
    d->version = s.version();
    d->last = &s;
    d->targetDirty = true;
}

void Overlay::draw(int x, int y, int w, int h, int targetWidth, int targetHeight) {
    if (!d->image || w <= 0 || h <= 0 || targetWidth <= 0 || targetHeight <= 0)
        return;
    float x0 = 2.0f * x / targetWidth - 1, x1 = 2.0f * (x + w) / targetWidth - 1;
    float y0 = 1 - 2.0f * y / targetHeight, y1 = 1 - 2.0f * (y + h) / targetHeight;
    d->drawQuad(x0, y0, x1, y1, opacity);
}

void Overlay::fit(float aspect, int areaWidth, int areaHeight, int &x, int &y, int &w, int &h) {
    if (aspect <= 0 || areaWidth <= 0 || areaHeight <= 0) {
        x = y = 0;
        w = areaWidth;
        h = areaHeight;
        return;
    }
    if ((float)areaWidth / areaHeight > aspect) {
        h = areaHeight;
        w = (int)std::lround(areaHeight * aspect);
    } else {
        w = areaWidth;
        h = (int)std::lround(areaWidth / aspect);
    }
    x = (areaWidth - w) / 2;
    y = (areaHeight - h) / 2;
}

gpu::TextureHandle Overlay::texture() {
    if (!d->image)
        return {};
    if (!d->target) {
        gpu::RenderTargetDesc rd;
        rd.width = d->width;
        rd.height = d->height;
        rd.depth = false;
        rd.filter = smooth ? gpu::Filter::Linear : gpu::Filter::Nearest;
        d->target = d->device.createRenderTarget(rd);
        d->targetDirty = true;
    }
    if (d->targetDirty && d->target) {
        gpu::ClearValues clear;
        clear.depth = false;
        clear.r = clear.g = clear.b = clear.a = 0;
        d->device.beginPass(d->target, 0, 0, d->width, d->height, clear);
        // the first row of the surface at the top of the texture (v = 1), as
        // the images of the materials
        d->drawQuad(-1, 1, 1, -1, 1);
        d->device.endPass();
        d->device.resolveRenderTarget(d->target);
        d->targetDirty = false;
    }
    return d->device.renderTargetTexture(d->target);
}

} // namespace r4::render
