// forest - an Instancer: up to 50000 trees on a ground, two levels of detail.
//
// Each tree is culled on its own and takes its level by its distance; the
// visible trees of each part are one draw call (GPU instancing). The panel
// changes the number of trees, the distances of the levels and the stages
// of the renderer, and shows what they cost.
#include "common/example.h"
#include "common/shapes.h"

#include "r4/render/renderer.h"
#include "r4/scene/node.h"
#include "r4/ui/ui.h"

#include <random>

using namespace r4;

int main(int argc, char **argv) {
    example::Window win;
    if (!win.open("forest", argc, argv)) {
        return 1;
    }
    render::Renderer renderer(*win.device);
    renderer.environment().clearColor = {0.55f, 0.7f, 0.9f, 1};
    renderer.environment().fog = true;
    renderer.environment().fogColor = {0.55f, 0.7f, 0.9f};
    renderer.environment().fogStart = 150;
    renderer.environment().fogEnd = 450;

    Vec4 bark{0.45f, 0.3f, 0.18f, 1};
    Vec4 leaves{0.2f, 0.5f, 0.2f, 1};
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
    forest->addLevel(near, 60); // up to 60 m
    forest->addLevel(far, 500); // then up to 500 m
    // the trees: always the same places for a count
    auto plant = [&](int count) {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> place(-240, 240);
        std::uniform_real_distribution<float> turn(0, 6.2832f);
        std::uniform_real_distribution<float> size(0.7f, 1.4f);
        std::vector<Mat4> trees;
        for (int i = 0; i < count; i++) {
            float x = place(rng);
            float y = place(rng);
            float a = turn(rng);
            float k = size(rng);
            trees.push_back(translate({x, y, 0}) * rotate(a, {0, 0, 1}) * scale({k, k, k}));
        }
        forest->setInstances(std::move(trees));
    };
    float count = 20000;
    float planted = 0;

    auto root = scene::make<scene::Group>();
    auto ground = scene::make<scene::Geode>();
    ground->addDrawable(example::drawable(renderer, example::ground(520), {0.35f, 0.42f, 0.25f, 1}));
    root->addChild(ground);
    root->addChild(forest);

    win.distance = 60;
    win.orbit.setAngles(30, 12, 0);
    render::Camera camera;
    render::RenderOptions &o = renderer.options();
    render::Environment &env = renderer.environment();
    bool openRender = true;
    while (win.poll()) {
        if ((int)count != (int)planted) {
            plant((int)count);
            planted = count;
        }
        const render::RenderStats &st = renderer.stats();
        ui::Context &gui = win.beginGui();
        gui.beginPanel("forest", {0, 0, 260, (float)win.pointHeight}, "forest");
        gui.slider("Trees", count, 100, 50000, "%.0f", true);
        gui.slider("LOD Scale", o.lodScale, 0.1f, 5, "%.2f", true);
        win.cameraControls(500);
        if (gui.section("Renderer", openRender)) {
            gui.toggle("Instancing", o.instancing);
            gui.toggle("View Frustum Culling", o.frustumCulling);
            gui.toggle("Freeze Culling", o.freezeCulling);
            gui.toggle("Fog", env.fog);
            gui.slider("Far Fog", env.fogEnd, 100, 2000, "%.0f", true);
        }
        char buf[4][64];
        std::snprintf(buf[0], 64, "%u", st.gpu.drawCalls);
        std::snprintf(buf[1], 64, "%u", st.gpu.triangles);
        std::snprintf(buf[2], 64, "%u / %u", st.instancesCulled, st.instancesVisited);
        std::snprintf(buf[3], 64, "%.2f ms", st.cullMs + st.sortMs + st.uploadMs + st.drawMs);
        gui.table({{"Draw calls", buf[0]}, {"Triangles", buf[1]}, {"Trees culled", buf[2]}, {"CPU", buf[3]}});
        gui.endPanel();
        win.endGui();

        win.apply(camera, 0.5f, 2000);
        renderer.render(*root, camera, 0, 0, win.width, win.height);
        win.drawGui();
        win.swap();
    }
    return 0;
}
