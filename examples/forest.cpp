// forest - an Instancer: 20000 trees on a ground, two levels of detail.
//
// Each tree is culled on its own and takes its level by its distance; the
// visible trees of each part are one draw call (GPU instancing). The title
// of the window gives the draw calls and the trees culled.
#include "common/example.h"
#include "common/shapes.h"

#include "r4/render/renderer.h"
#include "r4/scene/node.h"

#include <random>

using namespace r4;

int main(int argc, char **argv) {
    example::Window win;
    if (!win.open("forest", argc, argv))
        return 1;
    render::Renderer renderer(*win.device);
    renderer.environment().clearColor = {0.55f, 0.7f, 0.9f, 1};
    renderer.environment().fog = true;
    renderer.environment().fogColor = {0.55f, 0.7f, 0.9f};
    renderer.environment().fogStart = 150;
    renderer.environment().fogEnd = 450;

    Vec4 bark{0.45f, 0.3f, 0.18f, 1}, leaves{0.2f, 0.5f, 0.2f, 1};
    // the near tree: a trunk and two cones; the far one: a single cone
    std::vector<scene::InstancePart> near = {
        {example::drawable(renderer, example::cylinder(0.25f, 0.18f, 0, 2), bark), Mat4(1.0f)},
        {example::drawable(renderer, example::cylinder(1.6f, 0, 1.5f, 4.2f), leaves), Mat4(1.0f)},
        {example::drawable(renderer, example::cylinder(1.1f, 0, 3.2f, 5.6f), leaves), Mat4(1.0f)},
    };
    std::vector<scene::InstancePart> far = {
        {example::drawable(renderer, example::cylinder(1.6f, 0, 0, 5.6f, 6), leaves), Mat4(1.0f)},
    };

    auto forest = scene::make<scene::Instancer>();
    forest->addLevel(near, 60);  // up to 60 m
    forest->addLevel(far, 500);  // then up to 500 m
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> place(-240, 240), turn(0, 6.2832f), size(0.7f, 1.4f);
    for (int i = 0; i < 20000; i++) {
        float k = size(rng);
        forest->addInstance(translate({place(rng), place(rng), 0}) * rotate(turn(rng), {0, 0, 1}) *
                            scale({k, k, k}));
    }

    auto root = scene::make<scene::Group>();
    auto ground = scene::make<scene::Geode>();
    ground->addDrawable(example::drawable(renderer, example::ground(520), {0.35f, 0.42f, 0.25f, 1}));
    root->addChild(ground);
    root->addChild(forest);

    win.distance = 60;
    win.pitch = 12;
    render::Camera camera;
    Uint32 last = SDL_GetTicks();
    while (win.poll()) {
        win.apply(camera, 0.5f, 600);
        renderer.render(*root, camera, 0, 0, win.width, win.height);
        win.swap();
        if (SDL_GetTicks() - last > 500) {
            const render::RenderStats &s = renderer.stats();
            char title[160];
            std::snprintf(title, sizeof title, "forest - %u draw calls, %u triangles, %u of %u trees culled",
                          s.gpu.drawCalls, s.gpu.triangles, s.instancesCulled, s.instancesVisited);
            SDL_SetWindowTitle(win.window, title);
            last = SDL_GetTicks();
        }
    }
    return 0;
}
