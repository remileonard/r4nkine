//
//  R4Screen.cpp
//  libRealSpace
//
#include "R4Screen.h"

#include "engine/Config.hpp"
#include "engine/GLExtensions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr float kTvDuration = 3.0f;
constexpr int kTvWidth = 400;
constexpr int kTvHeight = 300;

// the 27 colors of the Amstrad CPC: 0, 128 or 255 on each channel
r4::raster::Palette cpcPalette() {
    r4::raster::Palette p;
    const uint8_t levels[3] = {0, 128, 255};
    int n = 0;
    for (uint8_t r : levels) {
        for (uint8_t g : levels) {
            for (uint8_t b : levels) {
                p.colors[n] = r4::raster::rgba(r, g, b);
                n++;
            }
        }
    }
    return p;
}

void *glProc(const char *name) {
    return SDL_GL_GetProcAddress(name);
}

} // namespace

R4Screen::R4Screen() = default;

R4Screen::~R4Screen() {
    tv_.reset();
    post_.reset();
    device_.reset();
}

R4Screen *R4Screen::current() {
    if (!RSScreen::hasInstance()) {
        return nullptr;
    }
    return dynamic_cast<R4Screen *>(&RSScreen::instance());
}

void R4Screen::init(int width, int height, bool fullscreen) {
    this->width = width;
    this->height = height;
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
        std::printf("[R4Screen] SDL_Init: %s\n", SDL_GetError());
        return;
    }
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (fullscreen) {
        flags = (SDL_WindowFlags)(SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    m_window = SDL_CreateWindow("Neo Strike Commander (R4nkine)", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, this->width, this->height, flags);
    if (!m_window) {
        std::printf("[R4Screen] SDL_CreateWindow: %s\n", SDL_GetError());
        return;
    }
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GL_GetDrawableSize(m_window, &drawableWidth, &drawableHeight);
    if (drawableWidth > 0 && drawableHeight > 0) {
        this->width = drawableWidth;
        this->height = drawableHeight;
    }
    SDL_GetWindowSize(m_window, &this->logical_width, &this->logical_height);
    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        std::printf("[R4Screen] SDL_GL_CreateContext: %s\n", SDL_GetError());
        return;
    }
    SDL_GL_MakeCurrent(m_window, m_glContext);
    if (!initGLExtensions()) {
        std::printf("[R4Screen] the OpenGL extensions of the game are missing\n");
    }
    SDL_GL_SetSwapInterval(1);
    std::string error;
    device_ = r4::gpu::createGLDevice(glProc, &error);
    if (!device_) {
        std::printf("[R4Screen] R4nkine: %s\n", error.c_str());
        return;
    }
    std::printf("[R4Screen] %s\n", device_->name());
    device_->beginHostInterop();
    post_ = std::make_unique<r4::render::PostProcess>(*device_);
    tv_ = std::make_unique<r4::render::Overlay>(*device_);
    device_->endHostInterop();
    tv_->smooth = true;
    tvSurface_ = r4::raster::Surface(kTvWidth, kTvHeight, r4::raster::Format::RGBA8);
    tvSurface_.blend = true;
    initPostProcess();
    openScreen();
    SDL_ShowWindow(m_window);
    beginFrame();
}

void R4Screen::letterbox(int &x, int &y, int &w, int &h) const {
    r4::render::Overlay::fit(4.0f / 3.0f, this->width, this->height, x, y, w, h);
}

void R4Screen::openScreen(void) {
    if (!device_) {
        return;
    }
    device_->beginHostInterop();
    r4::gpu::ClearValues clear;
    device_->beginPass(r4::gpu::RenderTargetHandle{}, 0, 0, this->width, this->height, clear);
    device_->endPass();
    device_->endHostInterop();
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    letterbox(x, y, w, h);
    glViewport(x, this->height - y - h, w, h);
}

void R4Screen::initPostProcess() {
    if (!post_) {
        return;
    }
    Config &config = Config::getInstance();
    this->fx_cpc_palette = config.getBool("video", "fx_cpc_palette", false);
    this->fx_scanlines = config.getBool("video", "fx_scanlines", false);
    this->fx_pixel_scale = (float)config.getInt("video", "fx_pixel_scale", 1);
    this->fx_fxaa = config.getBool("video", "fx_fxaa", false);
    r4::render::PostSettings &s = post_->settings;
    s.msaa = std::max(1, config.getInt("Video", "msaa", 1));
    s.fxaa = this->fx_fxaa;
    s.pixelate = std::max(1, (int)std::lround(this->fx_pixel_scale));
    s.scanlines = this->fx_scanlines;
    s.quantize = this->fx_cpc_palette;
    if (this->fx_cpc_palette) {
        s.palette = cpcPalette();
        s.paletteSize = 27;
        s.dither = false;
    }
}

void R4Screen::beginFrame() {
    frameTarget_ = r4::gpu::RenderTargetHandle{};
    if (!post_ || !post_->active()) {
        return;
    }
    device_->beginHostInterop();
    frameTarget_ = post_->begin(this->width, this->height);
    r4::gpu::ClearValues clear;
    device_->beginPass(frameTarget_, 0, 0, this->width, this->height, clear);
    device_->endPass();
    device_->endHostInterop();
    device_->bindForHost(frameTarget_);
}

void R4Screen::beginGamePass() {
    device_->beginHostInterop();
    r4::gpu::ClearValues keep;
    keep.color = false;
    keep.depth = false;
    device_->beginPass(frameTarget_, 0, 0, this->width, this->height, keep);
}

void R4Screen::endGamePass() {
    device_->endPass();
    device_->endHostInterop();
}

void R4Screen::refresh(void) {
    if (!device_) {
        SDL_GL_SwapWindow(m_window);
        return;
    }
    if (frameTarget_) {
        device_->bindForHost(r4::gpu::RenderTargetHandle{});
        device_->beginHostInterop();
        post_->end(0, 0, this->width, this->height, this->width, this->height);
        device_->endHostInterop();
    }
    SDL_GL_SwapWindow(m_window);
    device_->beginHostInterop();
    r4::gpu::ClearValues clear;
    device_->beginPass(r4::gpu::RenderTargetHandle{}, 0, 0, this->width, this->height, clear);
    device_->endPass();
    device_->endHostInterop();
    beginFrame();
}

void R4Screen::fxTurnOnTv() {
    if (!device_) {
        is_spfx_finished = true;
        return;
    }
    tvTime_ = std::min(tvTime_ + 1.0f / 60.0f, kTvDuration);
    const float t = tvTime_ / kTvDuration;
    if (t >= 1.0f) {
        is_spfx_finished = true;
        return;
    }
    const uint32_t white = r4::raster::rgba(255, 255, 255);
    auto alpha = [](float a) {
        return r4::raster::rgba(255, 255, 255, (uint8_t)std::clamp(a * 255.0f, 0.0f, 255.0f));
    };
    const int w = kTvWidth;
    const int h = kTvHeight;
    tvSurface_.clear(r4::raster::rgba(0, 0, 0));
    if (t < 0.1f) {
        const int lineWidth = (int)(t / 0.1f * w);
        tvSurface_.fillRect((w - lineWidth) / 2, h / 2 - 1, lineWidth, 2, white);
    } else if (t < 0.2f) {
        const int lineHeight = (int)((t - 0.1f) / 0.1f * h);
        tvSurface_.fillRect(0, (h - lineHeight) / 2, w, std::max(lineHeight, 2), white);
    }
    if (t >= 0.3f && t < 0.7f) {
        const uint32_t noise = alpha(1.0f - t / 0.8f);
        for (int i = 0; i < 4000; i++) {
            const int size = 1 + std::rand() % 2;
            tvSurface_.fillRect(std::rand() % w, std::rand() % h, size, size, noise);
        }
    }
    if (t > 0.3f && t < 0.8f) {
        const float lineProgress = (t - 0.2f) / 0.6f;
        const int lineY = (int)((1.0f - lineProgress) * h);
        tvSurface_.fillRect(0, lineY - h / 40, w, h / 20, alpha(0.8f));
    }
    if (t > 0.5f) {
        const uint32_t scanline = alpha((t - 0.5f) / 0.5f * 0.3f * 0.15f);
        for (int y = 0; y < h; y += 2) {
            tvSurface_.hline(0, w - 1, y, scanline);
        }
    }
    tvSurface_.touch();
    beginGamePass();
    tv_->update(tvSurface_);
    tv_->draw(0, 0, this->width, this->height, this->width, this->height);
    endGamePass();
}
