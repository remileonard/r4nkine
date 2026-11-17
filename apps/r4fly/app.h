// app.h - the state of r4fly, shared by its loop (main.cpp) and its GUI
// (gui.cpp).
#pragma once

#include "r4/assets/model.h"
#include "r4/gpu/device.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"
#include "r4/ui/ui.h"

#include <SDL.h>

#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace r4fly {

using namespace r4;

// the eye turning about a center: the left button turns, the wheel zooms,
// the right button moves the center in the plane of the view
struct Trackball {
    Vec3 center;
    float radius = 1, distance = 3, heading = 30, pitch = 25;
    Vec3 pan;
    float fov = 45;
    bool autoClip = true;
    float znear = 0.1f, zfar = 1000;

    void reset(const Sphere &s, float h, float p, float zoom) {
        center = s.empty() ? Vec3{} : s.center;
        radius = s.empty() || s.radius <= 0 ? 1 : s.radius;
        distance = radius * 2.6f / (zoom > 0 ? zoom : 1);
        heading = h;
        pitch = p;
        pan = {};
    }
    Vec3 target() const { return center + pan; }
    Vec3 eye() const {
        float h = radians(heading), p = radians(pitch);
        return target() + Vec3{std::cos(p) * std::sin(h), -std::cos(p) * std::cos(h), std::sin(p)} * distance;
    }
    void apply(render::Camera &cam, float aspect) {
        cam.lookAt(eye(), target(), {0, 0, 1});
        if (autoClip) {
            znear = std::max(distance - radius * 2, distance * 0.01f);
            zfar = distance + radius * 4;
        }
        cam.perspective(fov, aspect, znear, zfar);
    }
    // the right button: the center moves with the mouse, in the plane of the view
    void move(float dx, float dy, int viewHeight) {
        float h = radians(heading), p = radians(pitch);
        Vec3 right{std::cos(h), std::sin(h), 0};
        Vec3 up{-std::sin(p) * std::sin(h), std::sin(p) * std::cos(h), std::cos(p)};
        float k = 2 * distance * std::tan(radians(fov) * 0.5f) / std::max(viewHeight, 1);
        pan = pan - right * (dx * k) + up * (dy * k);
    }
};

struct LoadedFile {
    std::string path;
    std::shared_ptr<assets::Model> model;
    scene::NodePtr node;
    size_t triangles = 0, lines = 0, points = 0;
};

struct App {
    SDL_Window *window = nullptr;
    std::unique_ptr<gpu::Device> device;
    std::unique_ptr<render::Renderer> renderer;
    render::Camera camera;
    Trackball ball;
    float viewH = 30, viewP = 25, viewZ = 1;

    std::shared_ptr<scene::Group> top;    // the models and the overlay
    std::shared_ptr<scene::Group> models; // what is shown
    std::shared_ptr<scene::Instancer> forest;
    std::vector<LoadedFile> files;

    // the light: over the shoulder of the viewer, or fixed in the world
    bool headlight = true;
    float lightAzimuth = 45, lightElevation = 50;

    // the GUI
    std::unique_ptr<ui::Context> ui;
    ui::Input input;          // the mouse of the frame
    bool sceneDrag = false;   // a drag that started in the view
    bool quit = false;
    bool showGui = true, showGraph = true, showPerf = true;
    // the open sections of the panel, the open nodes of the tree
    bool openCamera = true, openLight = false, openEnv = false, openRender = false, openCull = false,
         openOptim = false;
    std::map<scene::Node *, bool> openNodes;
    bool vsync = true;
    bool printStats = false;
    scene::Node *selected = nullptr;
    Mat4 selectedWorld;
    bool showSelectedBounds = true;
    std::map<scene::Node *, uint32_t> hiddenMasks; // the nodes hidden from the graph window
    std::shared_ptr<scene::Geode> boundsMarker;     // the bounding sphere of the selection

    // the measures: the last frames
    std::deque<float> frameMs, cullMs, drawMs;
    double lastFrameTime = 0;
    std::string snapRequest; // a capture without the GUI at the next frame

    void resetView() { ball.reset(models->bounds(), viewH, viewP, viewZ); }
};

// gui.cpp: the panels, in the look of perfly (r4::ui)
void guiFrame(App &app, int width, int height); // builds the panels
bool guiWantsMouse(const App &app);
// the part of the window left to the scene, beside the panels
ui::Rect sceneViewport(const App &app, int width, int height);
// the marker of the bounds of the selection, made with the renderer
std::shared_ptr<scene::Geode> makeBoundsMarker(render::Renderer &renderer);

bool savePPM(const char *path, int w, int h, const std::vector<uint8_t> &rgba);

} // namespace r4fly
