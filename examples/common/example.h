// example.h - what the examples of R4nkine share: an SDL2 window with an
// OpenGL context (core on macOS), the device, and a camera turning about a
// point with the mouse (left button: turn, wheel: zoom).
//
// It holds nothing of R4nkine itself: the examples show that part.
#pragma once

#include "r4/gpu/device.h"
#include "r4/render/renderer.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace example {

struct Window {
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    std::unique_ptr<r4::gpu::Device> device;
    int width = 0, height = 0;          // the framebuffer, in pixels
    int pointWidth = 0, pointHeight = 0; // the window, in points (the mouse)
    // the camera: about the target, at a distance, heading and pitch in degrees
    r4::Vec3 target{0, 0, 0};
    float distance = 10, heading = 30, pitch = 25;
    bool quit = false;
    int frames = -1; // -frames N: quit after N frames (the tests)

    ~Window() {
        device.reset();
        if (context)
            SDL_GL_DeleteContext(context);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }

    // false when no OpenGL 3.3 context could be made
    bool open(const char *title, int argc, char **argv, int w = 1024, int h = 640) {
        for (int i = 1; i + 1 < argc; i++)
            if (std::string(argv[i]) == "-frames")
                frames = std::atoi(argv[i + 1]);
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            return fail(SDL_GetError());
#ifdef __APPLE__
        // macOS: a core context, 4.1 the newest
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window || !(context = SDL_GL_CreateContext(window)))
            return fail(SDL_GetError());
        SDL_GL_SetSwapInterval(1);
        std::string error;
        device = r4::gpu::createGLDevice([](const char *n) { return SDL_GL_GetProcAddress(n); }, &error);
        if (!device)
            return fail(error.c_str());
        std::printf("%s: %s\n", title, device->name());
        return true;
    }

    // the events of a frame; false when the window is to close. The mouse
    // turns the camera unless the caller says it is taken (by a GUI).
    template <class OnEvent> bool poll(OnEvent &&onEvent, bool mouseTaken = false) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            onEvent(e);
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                quit = true;
            if (mouseTaken)
                continue;
            if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                heading -= e.motion.xrel * 0.4f;
                pitch = std::min(89.0f, std::max(-89.0f, pitch + e.motion.yrel * 0.4f));
            }
            if (e.type == SDL_MOUSEWHEEL)
                distance *= std::exp(-e.wheel.y * 0.12f);
        }
        SDL_GL_GetDrawableSize(window, &width, &height);
        SDL_GetWindowSize(window, &pointWidth, &pointHeight);
        if (frames >= 0 && frames-- == 0)
            quit = true;
        return !quit;
    }
    bool poll() {
        return poll([](const SDL_Event &) {});
    }

    // the eye of the camera
    r4::Vec3 eye() const {
        float h = r4::radians(heading), p = r4::radians(pitch);
        return target + r4::Vec3{std::cos(p) * std::sin(h), -std::cos(p) * std::cos(h), std::sin(p)} * distance;
    }
    void apply(r4::render::Camera &camera, float znear, float zfar) const {
        camera.lookAt(eye(), target, {0, 0, 1});
        camera.perspective(45, height > 0 ? (float)width / height : 1, znear, zfar);
    }

    void swap() { SDL_GL_SwapWindow(window); }

private:
    bool fail(const char *why) {
        std::fprintf(stderr, "example: %s\n", why);
        return false;
    }
};

} // namespace example
