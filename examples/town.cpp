// town - a walk in the OpenFlight town of data/openflight. Usage: town [file.flt], ville_with_sky.flt by default.
// Keys: W A S D (Z Q S D on AZERTY) or the arrows move, the left button drags the view, Shift runs, F walks or flies,
// Space and C go up and down when flying (through the walls), R walks again from the center, F1 shows or hides the panel, Esc quits.
// On foot, the eye follows the ground and the walls stop it: the triangles of the model are tested on the CPU.
#include "common/example.h"

#include "r4/assets/model.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"

#include <vector>

namespace {

struct Triangle {
    r4::Vec3 a;
    r4::Vec3 b;
    r4::Vec3 c;
};

// the triangles of the model in the world
void collect(const r4::assets::Model &model, int index, const r4::Mat4 &parent, std::vector<Triangle> &out) {
    const r4::assets::Node &node = model.nodes[index];
    r4::Mat4 world = parent * node.local;
    for (int m : node.meshes) {
        const r4::assets::Mesh &mesh = model.meshes[m];
        if (mesh.primitive != r4::assets::Primitive::TRIANGLES) {
            continue;
        }
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            Triangle t;
            t.a = r4::Vec3(world * r4::Vec4(mesh.vertices[mesh.indices[i]].position, 1));
            t.b = r4::Vec3(world * r4::Vec4(mesh.vertices[mesh.indices[i + 1]].position, 1));
            t.c = r4::Vec3(world * r4::Vec4(mesh.vertices[mesh.indices[i + 2]].position, 1));
            out.push_back(t);
        }
    }
    for (int child : node.children) {
        collect(model, child, world, out);
    }
}

// the distance along dir (unit) to the nearest triangle, up to reach; reach when nothing is hit (Moller-Trumbore)
float cast(const std::vector<Triangle> &triangles, r4::Vec3 from, r4::Vec3 dir, float reach) {
    float best = reach;
    for (const Triangle &t : triangles) {
        r4::Vec3 e1 = t.b - t.a;
        r4::Vec3 e2 = t.c - t.a;
        r4::Vec3 p = r4::cross(dir, e2);
        float det = r4::dot(e1, p);
        if (std::fabs(det) < 1e-9f) {
            continue;
        }
        float inv = 1.0f / det;
        r4::Vec3 s = from - t.a;
        float u = r4::dot(s, p) * inv;
        if (u < 0 || u > 1) {
            continue;
        }
        r4::Vec3 q = r4::cross(s, e1);
        float v = r4::dot(dir, q) * inv;
        if (v < 0 || u + v > 1) {
            continue;
        }
        float d = r4::dot(e2, q) * inv;
        if (d > 0 && d < best) {
            best = d;
        }
    }
    return best;
}

std::string fmtPosition(r4::Vec3 p) {
    char text[64];
    std::snprintf(text, sizeof text, "x %.0f  y %.0f  z %.1f", p.x, p.y, p.z);
    return text;
}

std::string findTown(int argc, char **argv) {
    if (argc > 1 && argv[1][0] != '-') {
        return argv[1];
    }
    std::string base;
    if (char *b = SDL_GetBasePath()) {
        base = b;
        SDL_free(b);
    }
    const char *tries[] = {"data/openflight/", "samples/openflight/", "../data/openflight/", "../../data/openflight/"};
    for (const std::string &dir : {std::string(), base}) {
        for (const char *t : tries) {
            std::string path = dir + t + "ville_with_sky.flt";
            if (FILE *f = std::fopen(path.c_str(), "rb")) {
                std::fclose(f);
                return path;
            }
        }
    }
    return "data/openflight/ville_with_sky.flt";
}

} // namespace

int main(int argc, char **argv) {
    example::Window win;
    if (!win.open("town", argc, argv)) {
        return 1;
    }
    std::string path = findTown(argc, argv);
    std::string error;
    auto model = r4::assets::loadModel(path, &error);
    if (!model) {
        std::fprintf(stderr, "town: %s\n", error.c_str());
        return 1;
    }
    r4::render::Renderer renderer(*win.device);
    r4::render::Environment &env = renderer.environment();
    env.lightDirection = r4::normalize(r4::Vec3{0.5f, 0.6f, -0.65f});
    env.ambient = {0.35f, 0.37f, 0.42f};
    env.clearColor = {0.55f, 0.7f, 0.9f, 1};
    env.fogColor = {0.75f, 0.82f, 0.92f};
    env.fogStart = 150;
    env.fogEnd = 1500;
    renderer.options().shadows = true;
    auto root = r4::scene::make<r4::scene::Group>();
    root->addChild(renderer.upload(*model));
    std::vector<Triangle> triangles;
    collect(*model, 0, r4::Mat4(1), triangles);
    std::printf("town: %s, %zu triangles\n", path.c_str(), triangles.size());

    const float radius = 0.4f; // the body against the walls
    const float step = 0.6f;   // the steps it climbs
    r4::Vec3 start{0, 0, 0};
    float eyeHeight = 1.7f;
    float speed = 4;
    float fov = 60;
    bool flying = false;
    bool showGui = true;
    bool looking = false;
    float yaw = 0;
    float pitch = 0;
    r4::Vec3 feet = start;
    auto ground = [&](r4::Vec3 at, float above) {
        float d = cast(triangles, at + r4::Vec3{0, 0, above}, {0, 0, -1}, 10000);
        return at.z + above - d;
    };
    auto reset = [&]() {
        feet = start;
        feet.z = ground(start, 1000);
        flying = false;
        yaw = 0;
        pitch = 0;
    };
    reset();

    uint64_t last = SDL_GetPerformanceCounter();
    r4::render::Camera camera;
    while (win.poll([&](const SDL_Event &e) {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !(showGui && win.gui->wantsMouse())) {
            looking = true;
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            looking = false;
        } else if (e.type == SDL_MOUSEMOTION && looking) {
            yaw -= e.motion.xrel * 0.25f;
            pitch = std::clamp(pitch - e.motion.yrel * 0.25f, -85.0f, 85.0f);
        } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.scancode == SDL_SCANCODE_F) {
                flying = !flying;
            } else if (e.key.keysym.scancode == SDL_SCANCODE_R) {
                reset();
            } else if (e.key.keysym.sym == SDLK_F1) {
                showGui = !showGui;
            }
        }
    })) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = std::min((float)(now - last) / (float)SDL_GetPerformanceFrequency(), 0.1f);
        last = now;

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_LEFT]) {
            yaw += 90 * dt;
        }
        if (keys[SDL_SCANCODE_RIGHT]) {
            yaw -= 90 * dt;
        }
        float y = r4::radians(yaw);
        r4::Vec3 ahead{-std::sin(y), std::cos(y), 0};
        r4::Vec3 right{std::cos(y), std::sin(y), 0};
        r4::Vec3 move{0, 0, 0};
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
            move += ahead;
        }
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
            move -= ahead;
        }
        if (keys[SDL_SCANCODE_D]) {
            move += right;
        }
        if (keys[SDL_SCANCODE_A]) {
            move -= right;
        }
        float run = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? 4.0f : 1.0f;
        if (r4::length(move) > 0) {
            move = r4::normalize(move) * speed * run * dt;
        }
        if (flying) {
            if (keys[SDL_SCANCODE_SPACE]) {
                feet.z += speed * run * dt;
            }
            if (keys[SDL_SCANCODE_C]) {
                feet.z -= speed * run * dt;
            }
            feet += move;
        } else {
            // each axis alone, so that the body slides along a wall
            for (int axis = 0; axis < 2; axis++) {
                float d = move[axis];
                if (d == 0) {
                    continue;
                }
                r4::Vec3 dir{0, 0, 0};
                dir[axis] = d > 0 ? 1.0f : -1.0f;
                float reach = std::fabs(d) + radius;
                float knee = cast(triangles, feet + r4::Vec3{0, 0, step}, dir, reach);
                float chest = cast(triangles, feet + r4::Vec3{0, 0, eyeHeight * 0.8f}, dir, reach);
                if (std::min(knee, chest) >= reach) {
                    feet[axis] += d;
                }
            }
            feet.z = ground(feet, step);
        }

        if (showGui) {
            r4::ui::Context &gui = win.beginGui();
            gui.beginPanel("town", {0, 0, 260, (float)win.pointHeight}, "town");
            gui.message("W A S D / arrows: move");
            gui.message("left drag: look, Shift: run");
            gui.message("F: walk / fly, Space C: up down");
            gui.message("R: center, F1: panel");
            gui.toggle("Fly", flying);
            gui.slider("Speed (m/s)", speed, 1, 50, "%.1f", true);
            gui.slider("Eye Height", eyeHeight, 0.5f, 5, "%.2f");
            gui.slider("Field of View", fov, 30, 100, "%.0f");
            gui.columns(2);
            gui.toggle("Shadows", renderer.options().shadows);
            gui.toggle("Fog", env.fog);
            gui.columns(1);
            if (gui.button("Back to Center")) {
                reset();
            }
            gui.message(fmtPosition(feet).c_str());
            gui.endPanel();
            win.endGui();
        }

        float p = r4::radians(pitch);
        r4::Vec3 eye = feet + r4::Vec3{0, 0, eyeHeight};
        r4::Vec3 look = ahead * std::cos(p) + r4::Vec3{0, 0, std::sin(p)};
        camera.lookAt(eye, eye + look);
        camera.perspective(fov, win.height > 0 ? (float)win.width / win.height : 1, 0.1f, 6000);
        renderer.render(*root, camera, 0, 0, win.width, win.height);
        if (showGui) {
            win.drawGui();
        }
        win.swap();
    }
    return 0;
}
