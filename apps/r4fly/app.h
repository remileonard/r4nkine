// app.h - the state of r4fly, shared by its loop (main.cpp) and its GUI
// (gui.cpp).
#pragma once

#include "r4/assets/model.h"
#include "r4/gpu/device.h"
#include "r4/render/orbit.h"
#include "r4/render/postprocess.h"
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
    float radius = 1;
    float distance = 3;
    // the sliders: heading (yaw about Z), pitch (the eye above), roll (about
    // the line of sight), read from the orbit and written back when moved
    float heading = 30;
    float pitch = 25;
    float roll = 0;
    Vec3 pan;
    float fov = 45;
    bool autoClip = true;
    float znear = 0.1f;
    float zfar = 1000;
    render::Orbit orbit; // the orientation
    // the mouse: in the axes of the screen (trackball, as perfly), or about
    // the vertical of the world (turntable: never a roll)
    bool turntable = false;

    void reset(const Sphere &s, float h, float p, float zoom) {
        center = s.empty() ? Vec3{} : s.center;
        radius = s.empty() || s.radius <= 0 ? 1 : s.radius;
        distance = radius * 2.6f / (zoom > 0 ? zoom : 1);
        setAngles(h, p, 0);
        pan = {};
    }
    void setAngles(float h, float p, float r) {
        orbit.setAngles(h, p, r);
        readAngles();
    }
    void readAngles() {
        orbit.angles(heading, pitch, roll);
    }
    void writeAngles() {
        orbit.setAngles(heading, pitch, roll);
    }
    // the left button: dx turns about the up of the screen, dy about its right
    void turn(float dx, float dy) {
        if (turntable) {
            heading -= dx;
            heading -= 360 * std::floor((heading + 180) / 360);
            pitch = std::min(89.9f, std::max(-89.9f, pitch + dy));
            writeAngles();
        } else {
            orbit.turn(dx, dy);
            readAngles();
        }
    }
    Vec3 target() const {
        return center + pan;
    }
    Vec3 eye() const {
        return target() - orbit.forward() * distance;
    }
    void apply(render::Camera &cam, float aspect) {
        orbit.target = target();
        orbit.distance = distance;
        if (autoClip) {
            znear = std::max(distance - radius * 2, distance * 0.01f);
            zfar = distance + radius * 4;
        }
        orbit.apply(cam, fov, aspect, znear, zfar);
    }
    // the right button: the center moves with the mouse, in the plane of the view
    void move(float dx, float dy, int viewHeight) {
        float k = 2 * distance * std::tan(radians(fov) * 0.5f) / std::max(viewHeight, 1);
        pan = pan - orbit.right() * (dx * k) + orbit.up() * (dy * k);
    }
};

struct LoadedFile {
    std::string path;
    std::shared_ptr<assets::Model> model;
    scene::NodePtr node;
    size_t triangles = 0;
    size_t lines = 0;
    size_t points = 0;
};

struct App {
    SDL_Window *window = nullptr;
    std::unique_ptr<gpu::Device> device;
    std::unique_ptr<render::Renderer> renderer;
    std::unique_ptr<render::PostProcess> post; // effects on the frame
    render::Camera camera;
    Trackball ball;
    float viewH = 30;
    float viewP = 25;
    float viewZ = 1;

    std::shared_ptr<scene::Group> top;    // the models and the overlay
    std::shared_ptr<scene::Group> models; // what is shown
    std::shared_ptr<scene::Instancer> forest;
    std::vector<LoadedFile> files;

    // the light: over the shoulder of the viewer, or fixed in the world
    bool headlight = true;
    float lightAzimuth = 45;
    float lightElevation = 50;

    // the GUI
    std::unique_ptr<ui::Context> ui;
    ui::Input input;        // the mouse of the frame
    bool sceneDrag = false; // a drag that started in the view
    bool quit = false;
    bool showGui = true;
    bool showGraph = true;
    bool showPerf = true;
    // the tree drawn over the view, as the "Show Tree" of perfly
    bool showTree = false;
    bool openTree = false;
    float treePanX = 0; // < 0: fitted to the view
    float treePanY = 0;
    float treeScale = -1;
    float treeDepth = 6; // the levels drawn
    // the open sections of the panel, the open nodes of the tree
    bool openCamera = true;
    bool openLight = false;
    bool openEnv = false;
    bool openRender = false;
    bool openCull = false;
    bool openOptim = false;
    bool openPost = false;
    std::map<scene::Node *, bool> openNodes;
    bool vsync = true;
    bool printStats = false;
    scene::Node *selected = nullptr;
    Mat4 selectedWorld;
    bool showSelectedBounds = true;
    std::map<scene::Node *, uint32_t> hiddenMasks; // the nodes hidden from the graph window
    std::shared_ptr<scene::Geode> boundsMarker;    // the bounding sphere of the selection

    // the measures: the last frames
    std::deque<float> frameMs;
    std::deque<float> cullMs;
    std::deque<float> drawMs;
    double lastFrameTime = 0;
    std::string snapRequest; // a capture without the GUI at the next frame

    // the trackball about the models, the reach of the shadows about their size
    void resetView() {
        ball.reset(models->bounds(), viewH, viewP, viewZ);
        if (renderer) {
            renderer->options().shadowDistance = ball.radius * 4;
            renderer->options().shadowCasterReach = ball.radius * 2;
        }
    }
};

// gui.cpp: the panels, in the look of perfly (r4::ui)
void guiFrame(App &app, int width, int height, float pixelScale); // builds the panels (points)
bool guiWantsMouse(const App &app);
// the part of the window left to the scene, beside the panels (points)
ui::Rect sceneViewport(const App &app, int width, int height);
// the marker of the bounds of the selection, made with the renderer
std::shared_ptr<scene::Geode> makeBoundsMarker(render::Renderer &renderer);

bool savePPM(const char *path, int w, int h, const std::vector<uint8_t> &rgba);

} // namespace r4fly
