// postprocess.cpp - the effects on the whole frame: a pass of FXAA when
// asked, then one pass for everything else (CRT, colors, palette).
#include "r4/render/postprocess.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <vector>

namespace r4::render {

namespace {

// a rectangle covering the viewport, from the vertex index
const char *kVertex = R"(#version 330
out vec2 vUV;
void main() {
    int i = gl_VertexID;
    int c = i < 3 ? i : (i == 3 ? 2 : (i == 4 ? 1 : 3));
    vec2 t = vec2(c & 1, c >> 1);
    vUV = t;
    gl_Position = vec4(t * 2.0 - 1.0, 0.0, 1.0);
}
)";

// FXAA 3.11, the light version (Timothy Lottes)
const char *kFxaa = R"(#version 330
layout(std140) uniform Post {
    vec4 size;      // of the source: w, h, 1/w, 1/h
    vec4 grade;     // brightness, contrast, saturation, gamma
    vec4 tint;      // rgb, fade
    vec4 fadeColor; // rgb, pixelate
    vec4 crt;       // scanline strength, scanline size, curvature, aberration
    vec4 misc;      // vignette, quantize, dither, output height
};
uniform sampler2D source;
in vec2 vUV;
out vec4 fragColor;
void main() {
    vec2 inv = size.zw;
    vec3 rgbNW = texture(source, vUV + vec2(-1.0, -1.0) * inv).rgb;
    vec3 rgbNE = texture(source, vUV + vec2(1.0, -1.0) * inv).rgb;
    vec3 rgbSW = texture(source, vUV + vec2(-1.0, 1.0) * inv).rgb;
    vec3 rgbSE = texture(source, vUV + vec2(1.0, 1.0) * inv).rgb;
    vec4 center = texture(source, vUV);
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lNW = dot(rgbNW, luma);
    float lNE = dot(rgbNE, luma);
    float lSW = dot(rgbSW, luma);
    float lSE = dot(rgbSE, luma);
    float lM = dot(center.rgb, luma);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 / 8.0), 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * inv;
    vec3 a = 0.5 * (texture(source, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture(source, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(source, vUV - dir * 0.5).rgb + texture(source, vUV + dir * 0.5).rgb);
    float lB = dot(b, luma);
    fragColor = vec4((lB < lMin || lB > lMax) ? a : b, center.a);
}
)";

const char *kFinal = R"(#version 330
layout(std140) uniform Post {
    vec4 size;
    vec4 grade;
    vec4 tint;
    vec4 fadeColor;
    vec4 crt;
    vec4 misc;
};
uniform sampler2D source;
uniform sampler2D lut; // 32 x 32 x 32 colors: x = r + 32 b, y = g
in vec2 vUV;
out vec4 fragColor;

float bayer4(ivec2 p) {
    int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return (float(m[(p.y & 3) * 4 + (p.x & 3)]) + 0.5) / 16.0;
}

void main() {
    vec2 uv = vUV;
    // the bulge of the glass
    if (crt.z > 0.0) {
        vec2 c = uv * 2.0 - 1.0;
        c += c * (c.yx * c.yx) * crt.z;
        uv = c * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }
    // big pixels
    float px = max(fadeColor.w, 1.0);
    vec2 cells = size.xy / px;
    vec2 suv = px > 1.0 ? (floor(uv * cells) + 0.5) / cells : uv;
    vec3 col;
    if (crt.w > 0.0) {
        vec2 d = (suv - 0.5) * crt.w * size.zw * 2.0;
        col = vec3(texture(source, suv + d).r, texture(source, suv).g, texture(source, suv - d).b);
    } else {
        col = texture(source, suv).rgb;
    }
    // the colors
    col = (col - 0.5) * grade.y + 0.5 + grade.x;
    float l = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(l), col, grade.z);
    col = pow(max(col, vec3(0.0)), vec3(1.0 / max(grade.w, 0.01)));
    col *= tint.rgb;
    col = mix(col, fadeColor.rgb, tint.a);
    // the palette
    if (misc.y > 0.5) {
        col = clamp(col, 0.0, 1.0);
        if (misc.z > 0.5) {
            col = clamp(col + (bayer4(ivec2(floor(suv * cells))) - 0.5) * 0.09, 0.0, 1.0);
        }
        ivec3 q = ivec3(col * 31.0 + 0.5);
        col = texelFetch(lut, ivec2(q.r + 32 * q.b, q.g), 0).rgb;
    }
    // the lines of the screen, following the bulge
    if (crt.x > 0.0) {
        float line = fract(uv.y * misc.w / max(crt.y, 1.0));
        col *= 1.0 - crt.x * (0.5 - 0.5 * cos(6.2831853 * line));
    }
    if (misc.x > 0.0) {
        col *= 1.0 - misc.x * smoothstep(0.25, 0.75, length(uv - 0.5));
    }
    fragColor = vec4(col, 1.0);
}
)";

struct Block {
    float size[4];
    float grade[4];
    float tint[4];
    float fadeColor[4];
    float crt[4];
    float misc[4];
};

} // namespace

struct PostProcess::Impl {
    gpu::Device &device;
    gpu::ShaderHandle fxaaShader;
    gpu::ShaderHandle finalShader;
    gpu::PipelineHandle fxaaPipeline;
    gpu::PipelineHandle finalPipeline;
    gpu::BufferHandle ubo;
    gpu::RenderTargetHandle scene;
    gpu::RenderTargetHandle temp;
    int width = 0;
    int height = 0;
    int samples = 1;
    gpu::TextureHandle lut;
    raster::Palette lutPalette;
    int lutSize = -1;
    bool begun = false;

    explicit Impl(gpu::Device &d) : device(d) {
    }

    gpu::PipelineHandle pipeline(gpu::ShaderHandle s) {
        gpu::PipelineDesc pd;
        pd.shader = s;
        pd.cull = gpu::CullMode::NONE;
        pd.depthTest = pd.depthWrite = false;
        return device.createPipeline(pd);
    }

    void freeTargets() {
        if (scene) {
            device.destroyRenderTarget(scene);
        }
        if (temp) {
            device.destroyRenderTarget(temp);
        }
        scene = temp = {};
    }

    // the nearest color of the palette for each of 32 x 32 x 32 colors
    void buildLut(const raster::Palette &p, int count) {
        std::vector<uint32_t> px(1024 * 32);
        for (int g = 0; g < 32; g++) {
            for (int b = 0; b < 32; b++) {
                for (int r = 0; r < 32; r++) {
                    auto level = [](int v) {
                        return (uint8_t)(v * 255 / 31);
                    };
                    int i = p.nearest(level(r), level(g), level(b), count);
                    px[(size_t)g * 1024 + b * 32 + r] = p.colors[i] | 0xff000000u;
                }
            }
        }
        if (!lut) {
            gpu::TextureDesc td;
            td.width = 1024;
            td.height = 32;
            td.format = gpu::TextureFormat::RGBA8;
            td.minFilter = td.magFilter = gpu::Filter::NEAREST;
            td.mipmaps = false;
            td.wrapS = td.wrapT = gpu::Wrap::CLAMP;
            lut = device.createTexture(td, px.data());
        } else {
            device.updateTexture(lut, 0, 0, 1024, 32, px.data());
        }
        lutPalette = p;
        lutSize = count;
    }
};

PostProcess::PostProcess(gpu::Device &device) : d(new Impl(device)) {
    auto make = [&](const char *fragment, const char *name, bool withLut) {
        gpu::ShaderDesc sd;
        sd.debugName = name;
        sd.vertex = kVertex;
        sd.fragment = fragment;
        sd.uniformBlocks = {{"Post", 0}};
        sd.samplers = {{"source", 0}};
        if (withLut) {
            sd.samplers.push_back({"lut", 1});
        }
        return device.createShader(sd);
    };
    d->fxaaShader = make(kFxaa, "r4::PostProcess fxaa", false);
    d->finalShader = make(kFinal, "r4::PostProcess final", true);
    d->fxaaPipeline = d->pipeline(d->fxaaShader);
    d->finalPipeline = d->pipeline(d->finalShader);
    Block b{};
    d->ubo = device.createBuffer(gpu::BufferUsage::UNIFORM, &b, sizeof b, true);
}

PostProcess::~PostProcess() {
    gpu::Device &dev = d->device;
    d->freeTargets();
    if (d->lut) {
        dev.destroyTexture(d->lut);
    }
    dev.destroyBuffer(d->ubo);
    dev.destroyPipeline(d->fxaaPipeline);
    dev.destroyPipeline(d->finalPipeline);
    dev.destroyShader(d->fxaaShader);
    dev.destroyShader(d->finalShader);
    delete d;
}

bool PostProcess::active() const {
    const PostSettings &s = settings;
    return s.msaa > 1 || s.fxaa || s.pixelate > 1 || s.brightness != 0 || s.contrast != 1 || s.saturation != 1 || s.gamma != 1 || s.tint != Vec3(1) || s.fade > 0 || s.quantize || s.scanlines || s.curvature > 0 || s.aberration > 0 || s.vignette > 0;
}

gpu::RenderTargetHandle PostProcess::begin(int width, int height) {
    d->begun = false;
    if (!active() || width <= 0 || height <= 0) {
        return {};
    }
    int samples = std::max(1, std::min(settings.msaa, d->device.limits().maxSamples));
    if (width != d->width || height != d->height || samples != d->samples || !d->scene) {
        d->freeTargets();
        gpu::RenderTargetDesc rd;
        rd.width = width;
        rd.height = height;
        rd.samples = samples;
        d->scene = d->device.createRenderTarget(rd);
        d->width = width;
        d->height = height;
        d->samples = samples;
    }
    if (settings.fxaa && !d->temp) {
        gpu::RenderTargetDesc rd;
        rd.width = width;
        rd.height = height;
        rd.depth = false;
        d->temp = d->device.createRenderTarget(rd);
    }
    d->begun = (bool)d->scene;
    return d->scene;
}

void PostProcess::end(int x, int y, int width, int height, int windowWidth, int windowHeight) {
    if (!d->begun) {
        return;
    }
    d->begun = false;
    (void)windowWidth;
    gpu::Device &dev = d->device;
    const PostSettings &s = settings;
    dev.resolveRenderTarget(d->scene);
    gpu::TextureHandle source = dev.renderTargetTexture(d->scene);

    Block b{};
    b.size[0] = (float)d->width;
    b.size[1] = (float)d->height;
    b.size[2] = 1.0f / d->width;
    b.size[3] = 1.0f / d->height;
    b.grade[0] = s.brightness;
    b.grade[1] = s.contrast;
    b.grade[2] = s.saturation;
    b.grade[3] = s.gamma;
    b.tint[0] = s.tint.x;
    b.tint[1] = s.tint.y;
    b.tint[2] = s.tint.z;
    b.tint[3] = s.fade;
    b.fadeColor[0] = s.fadeColor.x;
    b.fadeColor[1] = s.fadeColor.y;
    b.fadeColor[2] = s.fadeColor.z;
    b.fadeColor[3] = (float)std::max(1, s.pixelate);
    b.crt[0] = s.scanlines ? s.scanlineStrength : 0;
    b.crt[1] = s.scanlineSize;
    b.crt[2] = s.curvature;
    b.crt[3] = s.aberration;
    b.misc[0] = s.vignette;
    b.misc[1] = s.quantize ? 1.0f : 0.0f;
    b.misc[2] = s.dither ? 1.0f : 0.0f;
    b.misc[3] = (float)height;
    dev.updateBuffer(d->ubo, 0, &b, sizeof b);

    gpu::ClearValues none;
    none.color = none.depth = false;
    if (s.fxaa && d->temp) {
        dev.beginPass(d->temp, 0, 0, d->width, d->height, none);
        dev.bindPipeline(d->fxaaPipeline);
        dev.bindUniformBuffer(0, d->ubo, 0, sizeof b);
        dev.bindTexture(0, source);
        dev.draw(6);
        dev.endPass();
        source = dev.renderTargetTexture(d->temp);
    }
    if (s.quantize && (d->lutSize != s.paletteSize || std::memcmp(d->lutPalette.colors.data(), s.palette.colors.data(), 256 * 4) != 0)) {
        d->buildLut(s.palette, s.paletteSize);
    }
    dev.beginPass(gpu::RenderTargetHandle{}, x, windowHeight - y - height, width, height, none);
    dev.bindPipeline(d->finalPipeline);
    dev.bindUniformBuffer(0, d->ubo, 0, sizeof b);
    dev.bindTexture(0, source);
    if (d->lut) {
        dev.bindTexture(1, d->lut);
    }
    dev.draw(6);
    dev.endPass();
}

} // namespace r4::render
