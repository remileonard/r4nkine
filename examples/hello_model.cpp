// hello_model - a model in a window and a panel to look at it. Usage: hello_model [model], a box when none is given.
#include "common/example.h"
#include "common/shapes.h"

#include "r4/assets/model.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"

int main(int argc, char **argv) {
    example::Window win;
    if (!win.open("hello_model", argc, argv)) {
        return 1;
    }
    r4::render::Renderer renderer(*win.device);
    renderer.environment().lightDirection = r4::normalize(r4::Vec3{0.4f, 0.7f, -0.6f});

    auto root = r4::scene::make<r4::scene::Group>();
    std::string what = "a box made in code";
    if (argc > 1 && argv[1][0] != '-') {
        std::string error;
        auto model = r4::assets::loadModel(argv[1], &error);
        if (!model) {
            std::fprintf(stderr, "hello_model: %s\n", error.c_str());
            return 1;
        }
        root->addChild(renderer.upload(*model));
        what = std::string(argv[1]) + ": " + std::to_string(model->meshes.size()) + " meshes";
    } else {
        auto geode = r4::scene::make<r4::scene::Geode>();
        geode->addDrawable(example::drawable(renderer, example::box({2, 1, 0.6f}), {0.9f, 0.55f, 0.2f, 1}));
        root->addChild(geode);
    }

    r4::Sphere bounds = root->bounds();
    win.target = bounds.center;
    win.distance = bounds.radius * 3;
    r4::render::Camera camera;
    r4::render::Environment &env = renderer.environment();
    r4::Vec3 background{env.clearColor.x, env.clearColor.y, env.clearColor.z};
    bool wireframe = false;
    bool openLight = false;
    while (win.poll()) {
        r4::ui::Context &gui = win.beginGui();
        gui.beginPanel("model", {0, 0, 250, (float)win.pointHeight}, "hello_model");
        gui.message(what.c_str());
        win.cameraControls(bounds.radius * 20);
        if (gui.button("Reset View")) {
            win.orbit.setAngles(30, 25, 0);
            win.distance = bounds.radius * 3;
        }
        gui.columns(2);
        gui.toggle("Wireframe", wireframe);
        gui.toggle("Textures", renderer.options().textures);
        gui.columns(1);
        if (gui.color("Background", background)) {
            env.clearColor = {background, 1};
        }
        if (gui.section("Light", openLight)) {
            gui.toggle("Lighting", renderer.options().lighting);
            gui.color("Light Color", env.lightColor);
            gui.color("Ambient", env.ambient);
        }
        gui.endPanel();
        win.endGui();

        renderer.setWireframe(wireframe);
        win.apply(camera, win.distance * 0.01f, win.distance + bounds.radius * 4);
        renderer.render(*root, camera, 0, 0, win.width, win.height);
        win.drawGui();
        win.swap();
    }
    return 0;
}
