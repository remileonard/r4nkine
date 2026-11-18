// r4fly - the model viewer of R4nkine, heir of perfly.
//
//   r4fly [options] file ...
//     -size WxH        the window size (default 1280x800)
//     -wire            start in wireframe
//     -shadows         the shadows of the light on
//     -nogui           start without the GUI (F1 shows it)
//     -core            an OpenGL core context (always on macOS)
//     -post LIST       effects on the frame, as msaa4,fxaa,crt,pixel3,ega
//                      (msaaN, fxaa, crt, scanlines, vignette, pixelN, grey,
//                      vga, ega, cga, handheld, amber)
//     -scale S         the size of the GUI (2: twice as large; Retina screens
//                      are already taken into account)
//     -frames N        quit after N frames
//     -snap file.ppm   save the last frame before quitting
//     -stats           print the statistics once a second
//     -view H,P,Z      the trackball: heading, pitch (degrees), zoom (2: twice closer)
//     -state MASK      the state of the models (their default otherwise)
//     -F dir:dir...    the folders where the textures and references are searched
//     -forest N[,F]    N copies of the files planted on a square (an Instancer):
//                      the first file near, the next ones beyond F, 2F... units
//
// The files are loaded side by side under one root. The mouse: the left
// button turns, the wheel zooms, the right button moves. Keys: F1 the GUI,
// w wireframe, r reset, s statistics, Esc quit. The GUI (r4::ui, in the look
// of perfly) holds the settings of the camera, the light, the environment,
// the rendering, the culling and the optimisations, the scene graph and the
// performances.
#include "app.h"

#include "r4/ui/controls.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace r4fly;

namespace r4fly {

bool savePPM(const char *path, int w, int h, const std::vector<uint8_t> &rgba) {
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++)
            std::fwrite(&rgba[((size_t)y * w + x) * 4], 1, 3, f);
    std::fclose(f);
    return true;
}

} // namespace r4fly

namespace {

// the core context: 4.1 is the newest of macOS, 3.3 enough elsewhere
#ifdef __APPLE__
constexpr int kCoreMajor = 4, kCoreMinor = 1;
#else
constexpr int kCoreMajor = 3, kCoreMinor = 3;
#endif

void usage() {
    std::fprintf(stderr, "usage: r4fly [-size WxH] [-wire] [-shadows] [-core] [-scale S] [-post LIST] [-nogui] [-frames N] [-snap file.ppm] [-stats] [-view H,P,Z] "
                         "[-state MASK] [-F dirs] [-forest N[,F]] file ...\n");
    std::exit(1);
}

// -post: msaa2/4/8, fxaa, crt, scanlines, vignette, pixel2..16, grey, and
// the palettes vga, ega, cga, handheld, amber (separated by commas)
void applyPostList(render::PostSettings &s, const std::string &list) {
    size_t p = 0;
    while (p < list.size()) {
        size_t c = list.find(',', p);
        if (c == std::string::npos)
            c = list.size();
        std::string w = list.substr(p, c - p);
        p = c + 1;
        auto palette = [&](int index) {
            const auto &presets = ui::palettePresets();
            s.quantize = true;
            s.palette = presets[index].palette;
            s.paletteSize = presets[index].size;
        };
        if (w.rfind("msaa", 0) == 0)
            s.msaa = std::max(1, std::atoi(w.c_str() + 4));
        else if (w == "fxaa")
            s.fxaa = true;
        else if (w == "crt") {
            s.scanlines = true;
            s.curvature = 0.08f;
            s.aberration = 1;
            s.vignette = 0.5f;
        } else if (w == "scanlines")
            s.scanlines = true;
        else if (w == "vignette")
            s.vignette = 0.6f;
        else if (w.rfind("pixel", 0) == 0)
            s.pixelate = std::max(1, std::atoi(w.c_str() + 5));
        else if (w == "grey")
            s.saturation = 0;
        else if (w == "vga")
            palette(0);
        else if (w == "ega")
            palette(1);
        else if (w == "cga")
            palette(2);
        else if (w == "handheld")
            palette(3);
        else if (w == "amber")
            palette(4);
        else
            std::fprintf(stderr, "r4fly: -post: %s unknown\n", w.c_str());
    }
}

double now() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void push(std::deque<float> &d, float v) {
    d.push_back(v);
    while (d.size() > 240)
        d.pop_front();
}

} // namespace

int main(int argc, char **argv) {
    App app;
    int width = 1280, height = 800, frames = -1;
    bool wire = false, shadows = false;
    float uiScale = 1;
    std::string postList;
#ifdef __APPLE__
    bool core = true;
#else
    bool core = false;
#endif
    const char *snap = nullptr;
    long state = -1;
    int forest = 0;
    float forestFar = 0;
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-size" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2)
                usage();
        } else if (a == "-wire") {
            wire = true;
        } else if (a == "-shadows") {
            shadows = true;
        } else if (a == "-scale" && i + 1 < argc) {
            uiScale = std::max(0.5f, (float)std::atof(argv[++i]));
        } else if (a == "-post" && i + 1 < argc) {
            postList = argv[++i];
        } else if (a == "-core") {
            core = true;
        } else if (a == "-nogui") {
            app.showGui = false;
        } else if (a == "-frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (a == "-snap" && i + 1 < argc) {
            snap = argv[++i];
        } else if (a == "-view" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%f,%f,%f", &app.viewH, &app.viewP, &app.viewZ) < 2)
                usage();
        } else if (a == "-state" && i + 1 < argc) {
            state = std::strtol(argv[++i], nullptr, 0);
        } else if (a == "-F" && i + 1 < argc) {
            std::string list = argv[++i];
            size_t p = 0;
            while (p <= list.size()) {
                size_t c = list.find(':', p);
                if (c == std::string::npos)
                    c = list.size();
                assets::addSearchPath(list.substr(p, c - p));
                p = c + 1;
            }
        } else if (a == "-forest" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%f", &forest, &forestFar) < 1)
                usage();
        } else if (a == "-stats") {
            app.printStats = true;
        } else if (!a.empty() && a[0] == '-') {
            usage();
        } else {
            files.push_back(a);
        }
    }
    if (files.empty())
        usage();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "r4fly: %s\n", SDL_GetError());
        return 1;
    }
    // a compatibility context, what a host game in fixed-pipeline GL has; a
    // core one on macOS (it has no other above 2.1) or with -core, and when
    // the compatibility one is refused
    SDL_GLContext context = nullptr;
    for (int attempt = core ? 1 : 0; attempt < 2 && !context; attempt++) {
        bool coreProfile = attempt == 1;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, coreProfile ? kCoreMajor : 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, coreProfile ? kCoreMinor : 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            coreProfile ? SDL_GL_CONTEXT_PROFILE_CORE : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, coreProfile ? SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG : 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        if (app.window)
            SDL_DestroyWindow(app.window);
        app.window = SDL_CreateWindow("r4fly", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!app.window) {
            std::fprintf(stderr, "r4fly: %s\n", SDL_GetError());
            return 1;
        }
        context = SDL_GL_CreateContext(app.window);
    }
    if (!context) {
        std::fprintf(stderr, "r4fly: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(app.window, context);
    SDL_GL_SetSwapInterval(1);

    std::string error;
    app.device = gpu::createGLDevice([](const char *n) { return SDL_GL_GetProcAddress(n); }, &error);
    if (!app.device) {
        std::fprintf(stderr, "r4fly: %s\n", error.c_str());
        return 1;
    }
    std::printf("r4fly: %s\n", app.device->name());
    app.renderer = std::make_unique<render::Renderer>(*app.device);
    app.post = std::make_unique<render::PostProcess>(*app.device);
    applyPostList(app.post->settings, postList);
    if (!app.renderer->error().empty()) {
        std::fprintf(stderr, "r4fly: %s\n", app.renderer->error().c_str());
        return 1;
    }
    render::Renderer &renderer = *app.renderer;
    renderer.setWireframe(wire);
    renderer.options().shadows = shadows;

    // the files side by side along X, each centered on the ground
    app.top = scene::make<scene::Group>();
    app.top->setName("top");
    app.models = scene::make<scene::Group>();
    app.models->setName("root");
    app.top->addChild(app.models);
    float x = 0;
    auto instancer = scene::make<scene::Instancer>();
    int level = 0;
    for (const std::string &f : files) {
        auto model = assets::loadModel(f, &error);
        if (!model) {
            std::fprintf(stderr, "r4fly: %s\n", error.c_str());
            continue;
        }
        scene::NodePtr node = renderer.upload(*model);
        if (state >= 0)
            node->setStateFilter((uint32_t)state);
        Sphere s = node->bounds();
        LoadedFile lf;
        lf.path = f;
        lf.model = model;
        lf.node = node;
        for (const auto &m : model->meshes)
            switch (m.primitive) {
            case assets::Primitive::Triangles: lf.triangles += m.indices.size() / 3; break;
            case assets::Primitive::Lines: lf.lines += m.indices.size() / 2; break;
            case assets::Primitive::Points: lf.points += m.indices.size(); break;
            }
        std::printf("r4fly: %s: %zu meshes, %zu triangles, %zu lines, %zu points, %zu materials, %zu images\n",
                    f.c_str(), model->meshes.size(), lf.triangles, lf.lines, lf.points, model->materials.size(),
                    model->images.size());
        if (forest > 0) {
            // the model as a level of the instancer, its base on the ground
            Box box = model->bounds();
            float unit = s.empty() ? 1 : 1.0f / std::max(s.radius, 1e-6f);
            Mat4 m = scale({unit, unit, unit}) * translate({-s.center.x, -s.center.y, -box.min.z});
            auto parts = scene::collectParts(*node);
            for (auto &p : parts)
                p.local = m * p.local;
            float limit = forestFar > 0 && level + 1 < (int)files.size() ? forestFar * (level + 1) : 1e30f;
            instancer->addLevel(parts, limit);
            level++;
        } else {
            auto place = scene::make<scene::Group>();
            place->setName(f);
            if (!s.empty()) {
                float k = 1.0f / std::max(s.radius, 1e-6f);
                place->setMatrix(translate({x + 1, 0, 0}) * scale({k, k, k}) *
                                 translate(-s.center));
            }
            place->addChild(node);
            app.models->addChild(place);
            x += 2.2f;
        }
        app.files.push_back(std::move(lf));
    }

    if (forest > 0) {
        // a square of instances, turned and scaled at random
        int side = (int)std::ceil(std::sqrt((double)forest));
        unsigned seed = 1;
        auto rnd = [&]() {
            seed = seed * 1103515245u + 12345u;
            return (seed >> 8 & 0xffff) / 65535.0f;
        };
        std::vector<Mat4> inst;
        for (int i = 0; i < forest; i++) {
            float px = (i % side + rnd() * 0.6f - 0.3f) * 2.5f, py = (i / side + rnd() * 0.6f - 0.3f) * 2.5f;
            float k = 0.7f + 0.6f * rnd();
            inst.push_back(translate({px, py, 0}) * rotate(rnd() * 6.2832f, {0, 0, 1}) *
                           scale({k, k, k}));
        }
        instancer->setInstances(inst);
        instancer->setName("forest");
        app.models->addChild(instancer);
        app.forest = instancer;
        std::printf("r4fly: %d instances, %d levels\n", forest, level);
    }

    // the bounds of the selection, beside the models (so not in their bounds)
    app.boundsMarker = makeBoundsMarker(renderer);
    app.boundsMarker->setMask(0);
    app.top->addChild(app.boundsMarker);

    app.resetView();
    app.ui = std::make_unique<ui::Context>(*app.device);

    bool running = true;
    int frame = 0;
    double lastStats = now(), frameStart = now();
    int framesSinceStats = 0;
    std::vector<uint8_t> pixels;
    while (running && !app.quit) {
        int w, h, ww, wh;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        SDL_GetWindowSize(app.window, &ww, &wh);
        // the GUI and the mouse in points, the framebuffer in pixels (twice
        // as many on a Retina screen); -scale enlarges the GUI further
        float pixelScale = (ww > 0 ? (float)w / ww : 1) * uiScale;
        ww = std::max(1, (int)(ww / uiScale));
        wh = std::max(1, (int)(wh / uiScale));
        app.input.wheel = 0;
        app.input.pressed = app.input.released = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            bool gui = guiWantsMouse(app) && !app.sceneDrag;
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_F1: app.showGui = !app.showGui; break;
                case SDLK_ESCAPE: running = false; break;
                case SDLK_w: renderer.setWireframe(!renderer.wireframe()); break;
                case SDLK_r: app.resetView(); break;
                case SDLK_s: app.printStats = !app.printStats; break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                bool down = e.type == SDL_MOUSEBUTTONDOWN;
                app.input.mouseX = e.button.x / uiScale;
                app.input.mouseY = e.button.y / uiScale;
                // a press in the view belongs to the view until released
                if (down && !gui && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT))
                    app.sceneDrag = true;
                if (!app.sceneDrag && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
                    (e.button.button == SDL_BUTTON_LEFT ? app.input.left : app.input.right) = down;
                    (down ? app.input.pressed : app.input.released) = true;
                }
                if (!down && !(SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)))
                    app.sceneDrag = false;
                break;
            }
            case SDL_MOUSEMOTION:
                app.input.mouseX = e.motion.x / uiScale;
                app.input.mouseY = e.motion.y / uiScale;
                if (!app.sceneDrag)
                    break;
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    // the left button turns the view in the axes of the screen: across, the
                    // yaw; up and down, the pitch
                    app.ball.turn(e.motion.xrel * 0.4f, e.motion.yrel * 0.4f);
                } else if (e.motion.state & SDL_BUTTON_RMASK) {
                    // the right button moves the center with the mouse
                    app.ball.move((float)e.motion.xrel, (float)e.motion.yrel, wh);
                }
                break;
            case SDL_MOUSEWHEEL:
                if (gui)
                    app.input.wheel += (float)e.wheel.y; // scrolls the panel under the mouse
                else
                    app.ball.distance *= std::exp(-e.wheel.y * 0.12f);
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_LEAVE)
                    app.input.mouseX = app.input.mouseY = -1;
                break;
            }
        }
        ui::Rect view = sceneViewport(app, ww, wh);
        int vx = (int)std::lround(view.x * pixelScale), vw = std::max(1, (int)std::lround(view.w * pixelScale));
        app.ball.apply(app.camera, h > 0 ? (float)vw / h : 1);
        if (app.headlight) {
            // a light over the shoulder of the viewer, as the eye light of perfly
            Vec3 toTarget = normalize(app.ball.target() - app.ball.eye());
            renderer.environment().lightDirection = normalize(toTarget + Vec3{0.3f, 0, -0.6f});
        } else {
            float az = radians(app.lightAzimuth), el = radians(app.lightElevation);
            renderer.environment().lightDirection =
                -Vec3{std::cos(el) * std::sin(az), std::cos(el) * std::cos(az), std::sin(el)};
        }

        // the marker on the selection: its world matrix comes from the tree
        // of the last frame
        if (app.selected && app.showSelectedBounds && app.showGui && app.showGraph) {
            Sphere s = app.selected->bounds(); // in the space of its parent
            Mat4 parentWorld = app.selectedWorld * inverse(app.selected->matrix());
            float r = std::max(s.radius, 1e-6f);
            app.boundsMarker->setMatrix(parentWorld * translate(s.center) * scale({r, r, r}));
            app.boundsMarker->setMask(~0u);
        } else {
            app.boundsMarker->setMask(0);
        }

        guiFrame(app, ww, wh, pixelScale);
        // the scene into the target of the post process when an effect is on
        gpu::RenderTargetHandle target = app.post->begin(vw, h);
        renderer.setTarget(target);
        renderer.render(*app.top, app.camera, target ? 0 : vx, 0, vw, h);
        renderer.setTarget({});
        app.post->end(vx, 0, vw, h, w, h);
        if (!app.snapRequest.empty()) {
            // the scene alone, without the GUI
            pixels.resize((size_t)vw * h * 4);
            if (app.device->readPixels(vx, 0, vw, h, pixels.data()) && savePPM(app.snapRequest.c_str(), vw, h, pixels))
                std::printf("r4fly: %s saved\n", app.snapRequest.c_str());
            app.snapRequest.clear();
        }
        app.ui->render();
        app.device->takeStats(); // the draws of the GUI are not those of the scene

        frame++;
        framesSinceStats++;
        bool last = frames > 0 && frame >= frames;
        if (last && snap) {
            pixels.resize((size_t)w * h * 4);
            if (app.device->readPixels(0, 0, w, h, pixels.data()) && savePPM(snap, w, h, pixels))
                std::printf("r4fly: %s saved\n", snap);
        }
        const render::RenderStats &st = renderer.stats();
        double t = now();
        push(app.frameMs, (float)(t - frameStart));
        push(app.cullMs, (float)st.cullMs);
        push(app.drawMs, (float)(st.uploadMs + st.drawMs));
        frameStart = t;
        if (t - lastStats >= 1000 || last) {
            char title[256];
            float fps = (float)(framesSinceStats * 1000.0 / std::max(1.0, t - lastStats));
            std::snprintf(title, sizeof title,
                          "r4fly - %.0f fps, %u draws, %u triangles, %u/%u nodes culled, %u/%u instances culled", fps,
                          st.gpu.drawCalls, st.gpu.triangles, st.nodesCulled, st.nodesVisited, st.instancesCulled,
                          st.instancesVisited);
            SDL_SetWindowTitle(app.window, title);
            if (app.printStats)
                std::printf("%s\n", title + 8);
            lastStats = t;
            framesSinceStats = 0;
        }
        SDL_GL_SwapWindow(app.window);
        if (last)
            running = false;
    }
    app.ui.reset();
    app.selected = nullptr;
    app.boundsMarker.reset();
    app.files.clear();
    app.forest.reset();
    app.models.reset();
    app.top.reset();
    app.post.reset();
    app.renderer.reset();
    app.device.reset();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
