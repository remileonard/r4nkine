// r4fly - the model viewer of R4nkine, heir of perfly.
//
//   r4fly [options] file ...
//     -size WxH        the window size (default 1024x768)
//     -wire            start in wireframe
//     -frames N        quit after N frames
//     -snap file.ppm   save the last frame before quitting
//     -stats           print the statistics once a second
//
// The files are loaded side by side under one root. The mouse turns the
// trackball (left), pans (middle), zooms (right or the wheel). Keys: w
// wireframe, r reset, s statistics, Esc quit.
#include "r4/assets/model.h"
#include "r4/gpu/device.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace r4;

namespace {

struct Trackball {
    Vec3 center;
    float radius = 1, distance = 3, heading = 30, pitch = 25;
    Vec3 pan;
    void reset(const Sphere &s) {
        center = s.empty() ? Vec3{} : s.center;
        radius = s.empty() || s.radius <= 0 ? 1 : s.radius;
        distance = radius * 2.6f;
        heading = 30;
        pitch = 25;
        pan = {};
    }
    void apply(render::Camera &cam, float aspect) const {
        float h = radians(heading), p = radians(pitch);
        Vec3 target = center + pan;
        Vec3 eye = target + Vec3{std::cos(p) * std::sin(h), -std::cos(p) * std::cos(h), std::sin(p)} * distance;
        cam.lookAt(eye, target, {0, 0, 1});
        cam.perspective(45, aspect, std::max(distance - radius * 2, distance * 0.01f), distance + radius * 4);
    }
};

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

void usage() {
    std::fprintf(stderr, "usage: r4fly [-size WxH] [-wire] [-frames N] [-snap file.ppm] [-stats] file ...\n");
    std::exit(1);
}

} // namespace

int main(int argc, char **argv) {
    int width = 1024, height = 768, frames = -1;
    bool wire = false, printStats = false;
    const char *snap = nullptr;
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-size" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2)
                usage();
        } else if (a == "-wire") {
            wire = true;
        } else if (a == "-frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (a == "-snap" && i + 1 < argc) {
            snap = argv[++i];
        } else if (a == "-stats") {
            printStats = true;
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
    // a compatibility context: what a host game in fixed-pipeline GL has
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *window = SDL_CreateWindow("r4fly", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "r4fly: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "r4fly: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    std::string error;
    auto device = gpu::createGLDevice([](const char *n) { return SDL_GL_GetProcAddress(n); }, &error);
    if (!device) {
        std::fprintf(stderr, "r4fly: %s\n", error.c_str());
        return 1;
    }
    std::printf("r4fly: %s\n", device->name());
    {
        render::Renderer renderer(*device);
        if (!renderer.error().empty()) {
            std::fprintf(stderr, "r4fly: %s\n", renderer.error().c_str());
            return 1;
        }
        renderer.setWireframe(wire);

        // the files side by side along X, each centered on the ground
        auto root = scene::make<scene::Group>();
        root->setName("root");
        float x = 0;
        for (const std::string &f : files) {
            auto model = assets::loadModel(f, &error);
            if (!model) {
                std::fprintf(stderr, "r4fly: %s\n", error.c_str());
                continue;
            }
            scene::NodePtr node = renderer.upload(*model);
            Sphere s = node->bounds();
            auto place = scene::make<scene::Group>();
            place->setName(f);
            if (!s.empty()) {
                float scale = 1.0f / std::max(s.radius, 1e-6f);
                place->setMatrix(Mat4::translate({x + 1, 0, 0}) * Mat4::scale({scale, scale, scale}) *
                                 Mat4::translate(-s.center));
            }
            place->addChild(node);
            root->addChild(place);
            x += 2.2f;
            size_t tris = 0;
            for (const auto &m : model->meshes)
                tris += m.indices.size() / 3;
            std::printf("r4fly: %s: %zu meshes, %zu triangles, %zu materials, %zu images\n", f.c_str(),
                        model->meshes.size(), tris, model->materials.size(), model->images.size());
        }

        Trackball ball;
        ball.reset(root->bounds());
        render::Camera camera;
        bool running = true;
        int frame = 0;
        Uint32 lastStats = SDL_GetTicks();
        int framesSinceStats = 0;
        std::vector<uint8_t> pixels;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_w: renderer.setWireframe(!renderer.wireframe()); break;
                    case SDLK_r: ball.reset(root->bounds()); break;
                    case SDLK_s: printStats = !printStats; break;
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        ball.heading += e.motion.xrel * 0.4f;
                        ball.pitch = std::min(89.0f, std::max(-89.0f, ball.pitch + e.motion.yrel * 0.4f));
                    } else if (e.motion.state & SDL_BUTTON_RMASK) {
                        ball.distance *= std::exp(e.motion.yrel * 0.01f);
                    } else if (e.motion.state & SDL_BUTTON_MMASK) {
                        float k = ball.distance * 0.0015f, h = radians(ball.heading);
                        ball.pan += Vec3{-std::cos(h), -std::sin(h), 0} * (e.motion.xrel * k);
                        ball.pan.z += e.motion.yrel * k;
                    }
                    break;
                case SDL_MOUSEWHEEL: ball.distance *= std::exp(-e.wheel.y * 0.1f); break;
                }
            }
            int w, h;
            SDL_GL_GetDrawableSize(window, &w, &h);
            ball.apply(camera, h > 0 ? (float)w / h : 1);
            renderer.render(*root, camera, 0, 0, w, h);
            frame++;
            framesSinceStats++;
            bool last = frames > 0 && frame >= frames;
            if (last && snap) {
                pixels.resize((size_t)w * h * 4);
                if (device->readPixels(0, 0, w, h, pixels.data()) && savePPM(snap, w, h, pixels))
                    std::printf("r4fly: %s saved\n", snap);
            }
            const render::RenderStats &st = renderer.stats();
            Uint32 now = SDL_GetTicks();
            if (now - lastStats >= 1000 || last) {
                char title[256];
                float fps = framesSinceStats * 1000.0f / std::max<Uint32>(1, now - lastStats);
                std::snprintf(title, sizeof title, "r4fly - %.0f fps, %u draws, %u triangles, %u/%u nodes culled", fps,
                              st.gpu.drawCalls, st.gpu.triangles, st.nodesCulled, st.nodesVisited);
                SDL_SetWindowTitle(window, title);
                if (printStats)
                    std::printf("%s\n", title + 8);
                lastStats = now;
                framesSinceStats = 0;
            }
            SDL_GL_SwapWindow(window);
            if (last)
                running = false;
        }
    }
    device.reset();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
