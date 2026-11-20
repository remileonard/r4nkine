// ui_panel - r4::ui over a scene: a panel drives the spin, color and style of a box.
#include "common/example.h"
#include "common/shapes.h"

#include "r4/render/renderer.h"
#include "r4/scene/node.h"
#include "r4/ui/ui.h"

using namespace r4;

int main(int argc, char **argv) {
    example::Window win;
    if (!win.open("ui_panel", argc, argv)) {
        return 1;
    }
    render::Renderer renderer(*win.device);
    renderer.environment().lightDirection = normalize(Vec3{0.4f, 0.7f, -0.6f});

    auto root = scene::make<scene::Group>();
    auto spinner = scene::make<scene::Group>();
    root->addChild(spinner);
    assets::Material material;
    material.specular = {0.3f, 0.3f, 0.3f};
    auto geode = scene::make<scene::Geode>();
    auto mesh = renderer.uploadMesh(example::box({2, 2, 2}));
    spinner->addChild(geode);

    bool spin = true;
    bool lighting = true;
    float speed = 45;
    float angle = 0;
    float fov = 45;
    Vec3 color{0.9f, 0.55f, 0.2f};
    Vec3 last{-1, -1, -1};
    int style = 0;
    bool openColor = true;

    win.distance = 7;
    render::Camera camera;
    while (win.poll()) {
        ui::Context &gui = win.beginGui();
        gui.beginPanel("box", {0, 0, 260, (float)win.pointHeight}, "ui_panel");
        gui.message("Drag in the view to turn.");
        gui.columns(2);
        gui.toggle("Spin", spin);
        if (gui.button("Reset")) {
            angle = 0;
        }
        gui.columns(1);
        gui.slider("Speed", speed, 0, 360, "%.0f deg/s");
        gui.slider("Field of View", fov, 20, 90, "%.0f");
        gui.menu("Style", style, {"Filled", "Lines"});
        gui.toggle("Lighting", lighting);
        if (gui.section("Color", openColor)) {
            gui.color("Box", color);
        }
        win.cameraControls(30);
        gui.endPanel();
        win.endGui();

        if (spin) {
            angle += speed / 60.0f;
        }
        spinner->setMatrix(rotate(radians(angle), {0, 0, 1}));
        if (color != last) {
            material.diffuse = {color, 1};
            geode->clearDrawables();
            geode->addDrawable({mesh, renderer.createMaterial(material)});
            last = color;
        }
        renderer.setWireframe(style == 1);
        renderer.options().lighting = lighting;

        win.apply(camera, 0.1f, 100);
        camera.perspective(fov, win.height > 0 ? (float)win.width / win.height : 1, 0.1f, 100);
        renderer.render(*root, camera, 0, 0, win.width, win.height);
        win.drawGui();
        win.swap();
    }
    return 0;
}
