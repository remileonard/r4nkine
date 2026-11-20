// hierarchy - a plane and its missiles in the scene graph: Space fires a missile, R reloads, G the landing gear (a Switch), a LOD far away.
#include "common/example.h"
#include "common/shapes.h"

#include "r4/render/renderer.h"
#include "r4/scene/node.h"
#include "r4/ui/ui.h"

#include <vector>

using namespace r4;

namespace {

struct Missile {
    std::shared_ptr<scene::Geode> node;
    std::shared_ptr<scene::Group> pylon; // where it hangs, null when flying
    Vec3 velocity;
};

} // namespace

int main(int argc, char **argv) {
    example::Window win;
    if (!win.open("hierarchy", argc, argv)) {
        return 1;
    }
    render::Renderer renderer(*win.device);
    renderer.environment().clearColor = {0.55f, 0.7f, 0.9f, 1};
    renderer.environment().lightDirection = normalize(Vec3{0.3f, 0.5f, -0.8f});
    Vec4 grey{0.7f, 0.72f, 0.75f, 1};
    Vec4 dark{0.3f, 0.32f, 0.35f, 1};
    Vec4 white{0.95f, 0.95f, 0.95f, 1};
    Vec4 red{0.85f, 0.2f, 0.15f, 1};

    auto root = scene::make<scene::Group>();
    root->setName("world");
    auto ground = scene::make<scene::Geode>();
    ground->addDrawable(example::drawable(renderer, example::ground(400), {0.35f, 0.45f, 0.3f, 1}));
    root->addChild(ground);

    auto plane = scene::make<scene::Group>();
    plane->setName("plane");
    root->addChild(plane);
    auto lod = scene::make<scene::LOD>();
    lod->setRanges({0, 120, 2000});
    plane->addChild(lod);
    auto detailed = scene::make<scene::Group>();
    lod->addChild(detailed);
    auto body = scene::make<scene::Geode>();
    body->addDrawable(example::drawable(renderer, example::box({1, 7, 1}), grey));
    body->addDrawable(example::drawable(renderer, example::box({9, 2, 0.2f}), grey));
    body->addDrawable(example::drawable(renderer, example::box({3.4f, 1, 0.15f}), dark));
    detailed->addChild(body);
    auto tail = scene::make<scene::Geode>();
    tail->setMatrix(translate({0, -3, 0.9f}));
    tail->addDrawable(example::drawable(renderer, example::box({0.15f, 1.2f, 1.6f}), dark));
    detailed->addChild(tail);
    auto coarse = scene::make<scene::Geode>();
    coarse->addDrawable(example::drawable(renderer, example::box({9, 7, 1}), grey));
    lod->addChild(coarse);

    auto gear = scene::make<scene::Switch>();
    gear->setName("gear");
    for (Vec3 at : {Vec3{0, 2.5f, -0.9f}, Vec3{-1.5f, -0.5f, -0.9f}, Vec3{1.5f, -0.5f, -0.9f}}) {
        auto leg = scene::make<scene::Geode>();
        leg->setMatrix(translate(at));
        leg->addDrawable(example::drawable(renderer, example::box({0.2f, 0.2f, 0.9f}), dark));
        gear->addChild(leg);
    }
    detailed->addChild(gear);

    auto missileGeode = [&]() {
        auto m = scene::make<scene::Geode>();
        m->setName("missile");
        m->setMatrix(rotate(radians(-90), {1, 0, 0}));
        m->addDrawable(example::drawable(renderer, example::cylinder(0.13f, 0.13f, -1.2f, 1.0f, 10), white));
        m->addDrawable(example::drawable(renderer, example::cylinder(0.13f, 0, 1.0f, 1.4f, 10), red));
        return m;
    };
    std::vector<Missile> missiles;
    for (float x : {-3.6f, -2.4f, 2.4f, 3.6f}) {
        auto pylon = scene::make<scene::Group>();
        pylon->setName("pylon");
        pylon->setMatrix(translate({x, 0.3f, -0.35f}));
        detailed->addChild(pylon);
        missiles.push_back({missileGeode(), pylon, {}});
        pylon->addChild(missiles.back().node);
    }
    auto reload = [&]() {
        for (Missile &m : missiles) {
            if (!m.pylon) {
                root->removeChild(m.node.get());
                m.node->setMatrix(rotate(radians(-90), {1, 0, 0}));
                for (const auto &c : detailed->children()) {
                    if (c->name() == "pylon" && c->children().empty()) {
                        m.pylon = std::static_pointer_cast<scene::Group>(c);
                        m.pylon->addChild(m.node);
                        break;
                    }
                }
            }
        }
    };
    auto fire = [&]() {
        for (Missile &m : missiles) {
            if (m.pylon) {
                Mat4 world = plane->matrix() * m.pylon->matrix() * m.node->matrix();
                m.pylon->removeChild(m.node.get());
                m.pylon = nullptr;
                m.node->setMatrix(world);
                root->addChild(m.node);
                m.velocity = normalize(transformVector(plane->matrix(), {0, 1, 0})) * 35.0f;
                return;
            }
        }
    };

    win.distance = 28;
    win.orbit.setAngles(30, 20, 0);
    render::Camera camera;
    float t = 0;
    float speed = 0.4f;
    float bank = 25;
    float lodFar = 120;
    bool follow = true;
    auto toggleGear = [&]() {
        gear->setValue(gear->value() == scene::Switch::kAll ? scene::Switch::kNone : scene::Switch::kAll);
    };
    while (win.poll([&](const SDL_Event &e) {
        if (e.type != SDL_KEYDOWN) {
            return;
        }
        if (e.key.keysym.sym == SDLK_SPACE) {
            fire();
        }
        if (e.key.keysym.sym == SDLK_r) {
            reload();
        }
        if (e.key.keysym.sym == SDLK_g) {
            toggleGear();
        }
    })) {
        int hanging = 0;
        for (const Missile &m : missiles) {
            hanging += m.pylon != nullptr;
        }
        ui::Context &gui = win.beginGui();
        gui.beginPanel("plane", {0, 0, 250, (float)win.pointHeight}, "hierarchy");
        char msg[96];
        std::snprintf(msg, sizeof msg, "%d missiles on the pylons\n%d in flight", hanging, (int)missiles.size() - hanging);
        gui.message(msg);
        gui.columns(2);
        if (gui.button("Fire (Space)", hanging > 0)) {
            fire();
        }
        if (gui.button("Reload (R)")) {
            reload();
        }
        gui.columns(1);
        bool down = gear->value() == scene::Switch::kAll;
        if (gui.toggle("Landing Gear (G)", down)) {
            toggleGear();
        }
        gui.slider("Turn Rate", speed, 0, 1.5f, "%.2f rad/s");
        gui.slider("Bank", bank, -60, 60, "%.0f deg");
        if (gui.slider("LOD Switch", lodFar, 20, 500, "%.0f m", true)) {
            lod->setRanges({0, lodFar, 5000});
        }
        gui.toggle("Follow the Plane", follow);
        win.cameraControls(300);
        gui.endPanel();
        win.endGui();

        float dt = 1 / 60.0f;
        t += dt * speed;
        Mat4 flight = translate({40 * std::cos(t), 40 * std::sin(t), 12}) * rotate(t, {0, 0, 1}) * rotate(radians(-bank), {0, 1, 0});
        plane->setMatrix(flight);
        for (Missile &m : missiles) {
            if (!m.pylon) {
                m.node->setMatrix(translate(m.velocity * dt) * m.node->matrix());
            }
        }
        win.target = follow ? translationOf(flight) : Vec3{0, 0, 10};
        win.apply(camera, 0.5f, 3000);
        renderer.render(*root, camera, 0, 0, win.width, win.height);
        win.drawGui();
        win.swap();
    }
    return 0;
}
