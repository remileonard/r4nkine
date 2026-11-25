// r4fly - the model viewer of R4nkine, heir of perfly.
//
//   r4fly [options] file ...
//     -size WxH        the window size (default 1280x800)
//     -wire            start in wireframe
//     -shadows         the shadows of the light on
//     -nogui           start without the GUI (F1 shows it)
//     -core            an OpenGL core context (always on macOS)
//     -post LIST       effects on the frame separated by commas: msaaN, fxaa, crt, scanlines, vignette, pixelN, grey, vga, ega, cga, handheld, amber
//     -scale S         the size of the GUI (Retina screens are already taken into account)
//     -frames N        quit after N frames
//     -snap file.ppm   save the last frame before quitting
//     -stats           print the statistics once a second
//     -view H,P,Z      the trackball: heading, pitch (degrees), zoom (2: twice closer)
//     -state MASK      the state of the models (their default otherwise)
//     -F dir:dir...    the folders where the textures and references are searched
//     -vr              the views in a headset, with OpenXR (Windows)
//     -sound file.wav  a sound that turns about the models, heard from the camera (r4nkine_audio, OpenAL Soft)
//     -hrtf            with -sound: the HRTF of OpenAL Soft, for headphones
//
// The files are loaded side by side under one root. Mouse: left turns, wheel zooms, right moves. Keys: F1 the GUI, w wireframe, r reset, s statistics,
// v the headset on or off, Esc quit. In the headset, the viewer stands at the eye of the trackball, on a floor under the center of the view.
#include "app.h"

#include "r4/ui/controls.h"

#ifdef R4FLY_XR
#include "r4/xr/xr.h"
#endif

#ifdef R4FLY_AUDIO
#include "r4/audio/audio.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace r4fly;

namespace r4fly {

bool savePPM(const char *path, int w, int h, const std::vector<uint8_t> &rgba) {
    FILE *f = std::fopen(path, "wb");
    if (!f) {
        return false;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            std::fwrite(&rgba[((size_t)y * w + x) * 4], 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

} // namespace r4fly

namespace {

// the core context: 4.1 is the newest of macOS, 3.3 enough elsewhere
#ifdef __APPLE__
constexpr int kCoreMajor = 4;
constexpr int kCoreMinor = 1;
#else
constexpr int kCoreMajor = 3;
constexpr int kCoreMinor = 3;
#endif

void usage() {
    std::fprintf(stderr, "usage: r4fly [-size WxH] [-wire] [-shadows] [-core] [-scale S] [-post LIST] [-nogui] [-frames N] [-snap file.ppm] [-stats] [-view H,P,Z] "
                         "[-state MASK] [-F dirs] [-vr] [-sound file.wav] [-hrtf] file ...\n");
    std::exit(1);
}

void applyPostList(render::PostSettings &s, const std::string &list) {
    size_t p = 0;
    while (p < list.size()) {
        size_t c = list.find(',', p);
        if (c == std::string::npos) {
            c = list.size();
        }
        std::string w = list.substr(p, c - p);
        p = c + 1;
        auto palette = [&](int index) {
            const auto &presets = ui::palettePresets();
            s.quantize = true;
            s.palette = presets[index].palette;
            s.paletteSize = presets[index].size;
        };
        if (w.rfind("msaa", 0) == 0) {
            s.msaa = std::max(1, std::atoi(w.c_str() + 4));
        } else if (w == "fxaa") {
            s.fxaa = true;
        } else if (w == "crt") {
            s.scanlines = true;
            s.curvature = 0.08f;
            s.aberration = 1;
            s.vignette = 0.5f;
        } else if (w == "scanlines") {
            s.scanlines = true;
        } else if (w == "vignette") {
            s.vignette = 0.6f;
        } else if (w.rfind("pixel", 0) == 0) {
            s.pixelate = std::max(1, std::atoi(w.c_str() + 5));
        } else if (w == "grey") {
            s.saturation = 0;
        } else if (w == "vga") {
            palette(0);
        } else if (w == "ega") {
            palette(1);
        } else if (w == "cga") {
            palette(2);
        } else if (w == "handheld") {
            palette(3);
        } else if (w == "amber") {
            palette(4);
        } else {
            std::fprintf(stderr, "r4fly: -post: %s unknown\n", w.c_str());
        }
    }
}

#ifdef R4FLY_XR
// the tracking space under the eye of the trackball, turned to its heading; the center of the view at the height of the eyes
Mat4 vrOrigin(const Trackball &ball, bool floorLevel) {
    constexpr float kEyeHeight = 1.6f;
    Vec3 forward = ball.orbit.forward();
    float heading = axes::headingOf(forward);
    Vec3 flat = forward - axes::kUp * axes::heightOf(forward);
    flat = length(flat) > 1e-4f ? normalize(flat) : axes::kForward;
    Vec3 target = ball.target();
    Vec3 place = target - flat * ball.distance;
    place -= axes::kUp * (axes::heightOf(place) - axes::heightOf(target) + (floorLevel ? kEyeHeight : 0));
    return translate(place) * rotate(radians(heading), axes::kUp) * axes::fromYUpMatrix();
}

// a controller: its ray (half a meter along -Z) and a small cross, in the axes of OpenXR
std::shared_ptr<scene::Geode> makeHandMarker(render::Renderer &renderer) {
    assets::Mesh m;
    m.name = "hand";
    m.primitive = assets::Primitive::LINES;
    auto line = [&](Vec3 a, Vec3 b, uint32_t color) {
        for (Vec3 p : {a, b}) {
            assets::Vertex v;
            v.position = p;
            v.color = color;
            m.indices.push_back((uint32_t)m.vertices.size());
            m.vertices.push_back(v);
        }
    };
    const float k = 0.03f;
    line({0, 0, 0}, {0, 0, -0.5f}, 0xffffffffu);
    line({-k, 0, 0}, {k, 0, 0}, 0xff4040ffu);
    line({0, -k, 0}, {0, k, 0}, 0xff40ff40u);
    line({0, 0, -k}, {0, 0, k}, 0xffff4040u);
    m.computeBounds();
    assets::Material mat;
    mat.name = "hand";
    mat.shading = assets::Shading::UNLIT;
    mat.vertexColors = true;
    auto g = scene::make<scene::Geode>();
    g->setName("hand marker");
    g->addDrawable({renderer.uploadMesh(m), renderer.createMaterial(mat)});
    return g;
}
#endif

double now() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void push(std::deque<float> &d, float v) {
    d.push_back(v);
    while (d.size() > 240) {
        d.pop_front();
    }
}

} // namespace

int main(int argc, char **argv) {
    App app;
    int width = 1280;
    int height = 800;
    int frames = -1;
    bool wire = false;
    bool shadows = false;
    float uiScale = 1;
    std::string postList;
#ifdef __APPLE__
    bool core = true;
#else
    bool core = false;
#endif
    const char *snap = nullptr;
    long state = -1;
    bool vr = false;
    std::string soundFile;
    bool hrtf = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-size" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2) {
                usage();
            }
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
            if (std::sscanf(argv[++i], "%f,%f,%f", &app.viewH, &app.viewP, &app.viewZ) < 2) {
                usage();
            }
        } else if (a == "-state" && i + 1 < argc) {
            state = std::strtol(argv[++i], nullptr, 0);
        } else if (a == "-F" && i + 1 < argc) {
            std::string list = argv[++i];
            size_t p = 0;
            while (p <= list.size()) {
                size_t c = list.find(':', p);
                if (c == std::string::npos) {
                    c = list.size();
                }
                assets::addSearchPath(list.substr(p, c - p));
                p = c + 1;
            }
        } else if (a == "-vr") {
            vr = true;
        } else if (a == "-sound" && i + 1 < argc) {
            soundFile = argv[++i];
        } else if (a == "-hrtf") {
            hrtf = true;
        } else if (a == "-stats") {
            app.printStats = true;
        } else if (!a.empty() && a[0] == '-') {
            usage();
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) {
        usage();
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "r4fly: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext context = nullptr;
    for (int attempt = core ? 1 : 0; attempt < 2 && !context; attempt++) {
        bool coreProfile = attempt == 1;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, coreProfile ? kCoreMajor : 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, coreProfile ? kCoreMinor : 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, coreProfile ? SDL_GL_CONTEXT_PROFILE_CORE : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, coreProfile ? SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG : 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        if (app.window) {
            SDL_DestroyWindow(app.window);
        }
        app.window = SDL_CreateWindow("r4fly", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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
    app.device = gpu::createGLDevice(
        [](const char *n) {
            return SDL_GL_GetProcAddress(n);
        },
        &error);
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

    app.top = scene::make<scene::Group>();
    app.top->setName("top");
    app.models = scene::make<scene::Group>();
    app.models->setName("root");
    app.top->addChild(app.models);
    float x = 0;
    for (const std::string &f : files) {
        auto model = assets::loadModel(f, &error);
        if (!model) {
            std::fprintf(stderr, "r4fly: %s\n", error.c_str());
            continue;
        }
        scene::NodePtr node = renderer.upload(*model);
        if (state >= 0) {
            node->setStateFilter((uint32_t)state);
        }
        Sphere s = node->bounds();
        LoadedFile lf;
        lf.path = f;
        lf.model = model;
        lf.node = node;
        for (const auto &m : model->meshes) {
            switch (m.primitive) {
            case assets::Primitive::TRIANGLES:
                lf.triangles += m.indices.size() / 3;
                break;
            case assets::Primitive::LINES:
                lf.lines += m.indices.size() / 2;
                break;
            case assets::Primitive::POINTS:
                lf.points += m.indices.size();
                break;
            }
        }
        std::printf("r4fly: %s: %zu meshes, %zu triangles, %zu lines, %zu points, %zu materials, %zu images\n", f.c_str(), model->meshes.size(), lf.triangles, lf.lines, lf.points, model->materials.size(), model->images.size());
        auto place = scene::make<scene::Group>();
        place->setName(f);
        if (!s.empty()) {
            float k = 1.0f / std::max(s.radius, 1e-6f);
            place->setMatrix(translate({x + 1, 0, 0}) * scale({k, k, k}) * translate(-s.center));
        }
        place->addChild(node);
        app.models->addChild(place);
        x += 2.2f;
        app.files.push_back(std::move(lf));
    }

    app.boundsMarker = makeBoundsMarker(renderer);
    app.boundsMarker->setMask(0);
    app.top->addChild(app.boundsMarker);

#ifdef R4FLY_AUDIO
    // a sound that turns about the models in 8 s, on a circle of twice their radius; destroyed before its context
    std::unique_ptr<audio::Context> sound;
    std::shared_ptr<audio::SoundSource> soundNode;
    const Sphere soundAround = app.models->bounds();
    if (!soundFile.empty()) {
        audio::Settings as;
        as.hrtf = hrtf;
        sound = audio::Context::create(as, &error);
        std::shared_ptr<audio::Sound> wav = sound ? audio::loadWAV(soundFile, &error) : nullptr;
        if (!wav) {
            std::fprintf(stderr, "r4fly: -sound: %s\n", error.c_str());
        } else {
            if (wav->channels() != 1) {
                std::fprintf(stderr, "r4fly: -sound: %s is stereo: it has no place in space\n", soundFile.c_str());
            }
            soundNode = std::make_shared<audio::SoundSource>(sound->createSource(wav));
            soundNode->setName("sound");
            soundNode->source().loop = true;
            soundNode->source().referenceDistance = std::max(soundAround.radius, 0.1f);
            soundNode->source().play();
            app.top->addChild(soundNode);
            std::printf("r4fly: %s on %s%s\n", soundFile.c_str(), sound->deviceName().c_str(), sound->hrtf() ? ", HRTF" : "");
        }
    }
    double lastSound = now();
#else
    if (!soundFile.empty()) {
        std::fprintf(stderr, "r4fly: built without r4nkine_audio, no -sound\n");
    }
    (void)hrtf;
#endif

    app.resetView();
    app.ui = std::make_unique<ui::Context>(*app.device);

#ifdef R4FLY_XR
    std::unique_ptr<xr::Session> headset;
    // the walk of the viewer with the controllers, on top of the place given by the trackball
    Mat4 vrMove(1.0f);
    double lastMove = now();
    bool snapArmed = true;
    std::shared_ptr<scene::Geode> hands[xr::kHands];
    for (auto &hand : hands) {
        hand = makeHandMarker(renderer);
        hand->setMask(0);
        app.top->addChild(hand);
    }
    // left stick: walk where the head looks (faster with the left trigger); right stick: turn by steps, up and down; B or Y: back to the start
    auto moveVR = [&](const Mat4 &head, float dt) {
        const xr::Controller &left = headset->controller(xr::Hand::LEFT);
        const xr::Controller &right = headset->controller(xr::Hand::RIGHT);
        float speed = 1.5f * std::max(app.ball.radius, 1.0f) * (1 + 4 * left.trigger);
        Vec3 forward = -Vec3(head[2]);
        Vec3 side = Vec3(head[0]);
        forward -= axes::kUp * axes::heightOf(forward);
        side -= axes::kUp * axes::heightOf(side);
        forward = length(forward) > 1e-4f ? normalize(forward) : axes::kForward;
        side = length(side) > 1e-4f ? normalize(side) : axes::kRight;
        Vec3 step = (side * left.stick.x + forward * left.stick.y) * (speed * dt);
        if (std::abs(right.stick.y) > 0.2f) {
            step += axes::kUp * (right.stick.y * speed * dt);
        }
        vrMove = translate(step) * vrMove;
        if (std::abs(right.stick.x) > 0.7f && snapArmed) {
            Vec3 pivot = Vec3(head[3]);
            float angle = right.stick.x > 0 ? -30.0f : 30.0f;
            vrMove = translate(pivot) * rotate(radians(angle), axes::kUp) * translate(-pivot) * vrMove;
            snapArmed = false;
            headset->vibrate(xr::Hand::RIGHT, 0.3f, 0.03f);
        } else if (std::abs(right.stick.x) < 0.3f) {
            snapArmed = true;
        }
        for (int i = 0; i < xr::kHands; i++) {
            if (headset->controller((xr::Hand)i).secondary.pressed) {
                vrMove = Mat4(1.0f);
                headset->vibrate((xr::Hand)i, 0.6f, 0.1f);
            }
        }
    };
    auto startVR = [&]() {
        xr::Settings xs;
        xs.appName = "r4fly";
        headset = xr::Session::create(*app.device, xs, &error);
        if (!headset) {
            std::fprintf(stderr, "r4fly: VR: %s\n", error.c_str());
            return;
        }
        std::printf("r4fly: VR: %s, %s, %d eyes of %dx%d\n", headset->runtimeName().c_str(), headset->systemName().c_str(), headset->eyeCount(), headset->eye(0).width, headset->eye(0).height);
        // the headset gives the pace: the window does not wait for the screen
        SDL_GL_SetSwapInterval(0);
    };
    auto stopVR = [&]() {
        headset.reset();
        for (auto &hand : hands) {
            hand->setMask(0);
        }
        SDL_GL_SetSwapInterval(app.vsync ? 1 : 0);
    };
    if (vr) {
        startVR();
    }
#else
    if (vr) {
        std::fprintf(stderr, "r4fly: VR: built without OpenXR (the xr feature of vcpkg.json)\n");
    }
#endif

    bool running = true;
    int frame = 0;
    double lastStats = now();
    double frameStart = now();
    int framesSinceStats = 0;
    std::vector<uint8_t> pixels;
    while (running && !app.quit) {
        int w;
        int h;
        int ww;
        int wh;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        SDL_GetWindowSize(app.window, &ww, &wh);
        float pixelScale = (ww > 0 ? (float)w / ww : 1) * uiScale;
        ww = std::max(1, (int)(ww / uiScale));
        wh = std::max(1, (int)(wh / uiScale));
        app.input.wheel = 0;
        app.input.pressed = app.input.released = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            bool gui = guiWantsMouse(app) && !app.sceneDrag;
            switch (e.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_F1:
                    app.showGui = !app.showGui;
                    break;
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_w:
                    renderer.setWireframe(!renderer.wireframe());
                    break;
                case SDLK_r:
                    app.resetView();
#ifdef R4FLY_XR
                    vrMove = Mat4(1.0f);
#endif
                    break;
                case SDLK_s:
                    app.printStats = !app.printStats;
                    break;
#ifdef R4FLY_XR
                case SDLK_v:
                    if (headset) {
                        stopVR();
                    } else {
                        startVR();
                    }
                    break;
#endif
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                bool down = e.type == SDL_MOUSEBUTTONDOWN;
                app.input.mouseX = e.button.x / uiScale;
                app.input.mouseY = e.button.y / uiScale;
                if (down && !gui && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
                    app.sceneDrag = true;
                }
                if (!app.sceneDrag && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
                    (e.button.button == SDL_BUTTON_LEFT ? app.input.left : app.input.right) = down;
                    (down ? app.input.pressed : app.input.released) = true;
                }
                if (!down && !(SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) {
                    app.sceneDrag = false;
                }
                break;
            }
            case SDL_MOUSEMOTION:
                app.input.mouseX = e.motion.x / uiScale;
                app.input.mouseY = e.motion.y / uiScale;
                if (!app.sceneDrag) {
                    break;
                }
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    app.ball.turn(e.motion.xrel * 0.4f, e.motion.yrel * 0.4f);
                } else if (e.motion.state & SDL_BUTTON_RMASK) {
                    app.ball.move((float)e.motion.xrel, (float)e.motion.yrel, wh);
                }
                break;
            case SDL_MOUSEWHEEL:
                if (gui) {
                    app.input.wheel += (float)e.wheel.y;
                } else {
                    app.ball.distance *= std::exp(-e.wheel.y * 0.12f);
                }
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_LEAVE) {
                    app.input.mouseX = app.input.mouseY = -1;
                }
                break;
            }
        }
        ui::Rect view = sceneViewport(app, ww, wh);
        int vx = (int)std::lround(view.x * pixelScale);
        int vw = std::max(1, (int)std::lround(view.w * pixelScale));
        app.ball.apply(app.camera, h > 0 ? (float)vw / h : 1);
#ifdef R4FLY_AUDIO
        if (soundNode) {
            double ts = now();
            float dt = std::min(0.1f, (float)(ts - lastSound) / 1000.0f);
            lastSound = ts;
            float angle = (float)std::fmod(ts / 8000.0, 1.0) * 2 * 3.14159265f;
            float r = 2 * std::max(soundAround.radius, 0.1f);
            soundNode->setMatrix(translate(soundAround.center + Vec3{r * std::cos(angle), r * std::sin(angle), 0}));
            sound->listener().fromView(app.camera.view);
#ifdef R4FLY_XR
            if (headset && headset->running()) {
                sound->listener().fromCameraPose(headset->head());
            }
#endif
            sound->update(*app.top, dt);
        }
#endif
        if (app.headlight) {
            Vec3 toTarget = normalize(app.ball.target() - app.ball.eye());
            renderer.environment().lightDirection = normalize(toTarget + Vec3{0.3f, 0, -0.6f});
        } else {
            float az = radians(app.lightAzimuth);
            float el = radians(app.lightElevation);
            renderer.environment().lightDirection = -Vec3{std::cos(el) * std::sin(az), std::cos(el) * std::cos(az), std::sin(el)};
        }

        if (app.selected && app.showSelectedBounds && app.showGui && app.showGraph) {
            Sphere s = app.selected->bounds();
            Mat4 parentWorld = app.selectedWorld * inverse(app.selected->matrix());
            float r = std::max(s.radius, 1e-6f);
            app.boundsMarker->setMatrix(parentWorld * translate(s.center) * scale({r, r, r}));
            app.boundsMarker->setMask(~0u);
        } else {
            app.boundsMarker->setMask(0);
        }

#ifdef R4FLY_XR
        if (headset && !headset->pollEvents()) {
            std::printf("r4fly: VR: the session is finished\n");
            stopVR();
        }
        if (headset && headset->running()) {
            double tv = now();
            float dt = std::min(0.1f, (float)(tv - lastMove) / 1000.0f);
            lastMove = tv;
            moveVR(headset->head(), dt);
            headset->setOrigin(vrMove * vrOrigin(app.ball, headset->floorLevel()));
            bool draw = headset->beginFrame();
            for (int i = 0; i < xr::kHands; i++) {
                const xr::Controller &c = headset->controller((xr::Hand)i);
                hands[i]->setMask(c.active ? ~0u : 0u);
                if (c.active) {
                    hands[i]->setMatrix(c.aim);
                }
            }
            if (draw) {
                for (int i = 0; i < headset->eyeCount(); i++) {
                    const xr::Eye &eye = headset->eye(i);
                    renderer.setTarget(eye.target);
                    renderer.render(*app.top, eye.camera, 0, 0, eye.width, eye.height);
                }
                renderer.setTarget({});
            }
            headset->endFrame();
        }
#endif

        guiFrame(app, ww, wh, pixelScale);
        gpu::RenderTargetHandle target = app.post->begin(vw, h);
        renderer.setTarget(target);
        renderer.render(*app.top, app.camera, target ? 0 : vx, 0, vw, h);
        renderer.setTarget({});
        app.post->end(vx, 0, vw, h, w, h);
        if (!app.snapRequest.empty()) {
            pixels.resize((size_t)vw * h * 4);
            if (app.device->readPixels(vx, 0, vw, h, pixels.data()) && savePPM(app.snapRequest.c_str(), vw, h, pixels)) {
                std::printf("r4fly: %s saved\n", app.snapRequest.c_str());
            }
            app.snapRequest.clear();
        }
        app.ui->render();
        app.device->takeStats();

        frame++;
        framesSinceStats++;
        bool last = frames > 0 && frame >= frames;
        if (last && snap) {
            pixels.resize((size_t)w * h * 4);
            if (app.device->readPixels(0, 0, w, h, pixels.data()) && savePPM(snap, w, h, pixels)) {
                std::printf("r4fly: %s saved\n", snap);
            }
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
            std::snprintf(title, sizeof title, "r4fly - %.0f fps, %u draws, %u triangles, %u/%u nodes culled, %u/%u instances culled", fps, st.gpu.drawCalls, st.gpu.triangles, st.nodesCulled, st.nodesVisited, st.instancesCulled, st.instancesVisited);
            SDL_SetWindowTitle(app.window, title);
            if (app.printStats) {
                std::printf("%s\n", title + 8);
            }
            lastStats = t;
            framesSinceStats = 0;
        }
        SDL_GL_SwapWindow(app.window);
        if (last) {
            running = false;
        }
    }
#ifdef R4FLY_XR
    headset.reset();
    for (auto &hand : hands) {
        hand.reset();
    }
#endif
#ifdef R4FLY_AUDIO
    if (soundNode) {
        app.top->removeChild(soundNode.get());
    }
    soundNode.reset();
    sound.reset();
#endif
    app.ui.reset();
    app.selected = nullptr;
    app.boundsMarker.reset();
    app.files.clear();
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
