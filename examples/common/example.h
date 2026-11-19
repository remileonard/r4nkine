// example.h - what the examples of R4nkine share: an SDL2 window with an
// OpenGL context (core on macOS), the device, a camera turning about a point
// with the mouse (left button: turn, wheel: zoom), and the GUI (r4::ui) with
// its mouse: over a panel, the mouse goes to the GUI.
//
// It holds nothing of R4nkine itself: the examples show that part.
#pragma once

#include "r4/gpu/device.h"
#include "r4/render/orbit.h"
#include "r4/render/renderer.h"
#include "r4/ui/ui.h"

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
    std::unique_ptr<r4::ui::Context> gui;
    r4::ui::Input input; // the mouse for the GUI, in points
    int width = 0;       // the framebuffer, in pixels
    int height = 0;
    int pointWidth = 0; // the window, in points (the mouse)
    int pointHeight = 0;
    // the camera: about the target, at a distance; the mouse turns it in the
    // axes of the screen (r4::render::Orbit)
    r4::Vec3 target{0, 0, 0};
    float distance = 10;
    r4::render::Orbit orbit;
    bool quit = false;
    int frames = -1; // -frames N: quit after N frames (the tests)

    ~Window() {
        gui.reset();
        device.reset();
        if (context) {
            SDL_GL_DeleteContext(context);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }

    // false when no OpenGL 3.3 context could be made
    bool open(const char *title, int argc, char **argv, int w = 1024, int h = 640) {
        for (int i = 1; i + 1 < argc; i++) {
            if (std::string(argv[i]) == "-frames") {
                frames = std::atoi(argv[i + 1]);
            }
        }
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            return fail(SDL_GetError());
        }
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
        window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window || !(context = SDL_GL_CreateContext(window))) {
            return fail(SDL_GetError());
        }
        SDL_GL_SetSwapInterval(1);
        std::string error;
        device = r4::gpu::createGLDevice(
            [](const char *n) {
                return SDL_GL_GetProcAddress(n);
            },
            &error);
        if (!device) {
            return fail(error.c_str());
        }
        std::printf("%s: %s\n", title, device->name());
        gui = std::make_unique<r4::ui::Context>(*device);
        return true;
    }

    // the events of a frame; false when the window is to close. The mouse
    // goes to the GUI over its panels, else it turns the camera.
    template <class OnEvent> bool poll(OnEvent &&onEvent) {
        input.pressed = input.released = false;
        input.wheel = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            onEvent(e);
            bool mouseTaken = gui->wantsMouse() && !sceneDrag;
            if (e.type == SDL_MOUSEMOTION) {
                input.mouseX = (float)e.motion.x;
                input.mouseY = (float)e.motion.y;
            } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                bool down = e.type == SDL_MOUSEBUTTONDOWN;
                if (down && !mouseTaken) {
                    sceneDrag = true; // a drag that starts in the view stays there
                }
                if (!sceneDrag) {
                    (e.button.button == SDL_BUTTON_LEFT ? input.left : input.right) = down;
                    (down ? input.pressed : input.released) = true;
                }
                if (!down && !(SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) {
                    sceneDrag = false;
                }
            } else if (e.type == SDL_MOUSEWHEEL && mouseTaken) {
                input.wheel += (float)e.wheel.y;
            }
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                quit = true;
            }
            if (!sceneDrag && e.type != SDL_MOUSEWHEEL) {
                continue;
            }
            if (mouseTaken && e.type == SDL_MOUSEWHEEL) {
                continue;
            }
            if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                orbit.turn(e.motion.xrel * 0.4f, e.motion.yrel * 0.4f); // across: yaw, up and down: pitch
            }
            if (e.type == SDL_MOUSEWHEEL) {
                distance *= std::exp(-e.wheel.y * 0.12f);
            }
        }
        SDL_GL_GetDrawableSize(window, &width, &height);
        SDL_GetWindowSize(window, &pointWidth, &pointHeight);
        if (frames >= 0 && frames-- == 0) {
            quit = true;
        }
        return !quit;
    }
    bool poll() {
        return poll([](const SDL_Event &) {
        });
    }

    // the GUI of the frame: between beginGui() and endGui(), the panels;
    // drawGui() after the scene, over it
    r4::ui::Context &beginGui() {
        float scale = pointWidth > 0 ? (float)width / pointWidth : 1;
        gui->beginFrame(pointWidth, pointHeight, input, scale);
        return *gui;
    }
    void endGui() {
        gui->endFrame();
    }
    void drawGui() {
        gui->render();
        device->takeStats(); // the draws of the GUI are not those of the scene
    }
    // the sliders of the camera, in the panel being built
    void cameraControls(float maxDistance) {
        float heading;
        float pitch;
        float roll;
        orbit.angles(heading, pitch, roll);
        bool turned = gui->slider("Yaw (Heading)", heading, -180, 180, "%.0f");
        turned |= gui->slider("Pitch", pitch, -89, 89, "%.0f");
        turned |= gui->slider("Roll", roll, -180, 180, "%.0f");
        if (turned) {
            orbit.setAngles(heading, pitch, roll);
        }
        gui->slider("Distance", distance, maxDistance * 0.01f, maxDistance, "%.1f", true);
    }

    void apply(r4::render::Camera &camera, float znear, float zfar) {
        orbit.target = target;
        orbit.distance = distance;
        orbit.apply(camera, 45, height > 0 ? (float)width / height : 1, znear, zfar);
    }

    void swap() {
        SDL_GL_SwapWindow(window);
    }

private:
    bool sceneDrag = false;
    bool fail(const char *why) {
        std::fprintf(stderr, "example: %s\n", why);
        return false;
    }
};

} // namespace example
