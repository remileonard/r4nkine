// hello_model - the smallest program of R4nkine: a model in a window.
//
//   hello_model [model]     any format of R4nkine (OBJ, glTF, pfb, flt...);
//                           a box when none is given
//
// The mouse turns the camera (left button) and zooms (wheel).
#include "common/example.h"
#include "common/shapes.h"

#include "r4/assets/model.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"

int main(int argc, char **argv) {
    example::Window win;
    if (!win.open("hello_model", argc, argv))
        return 1;
    r4::render::Renderer renderer(*win.device);
    renderer.environment().lightDirection = r4::normalize(r4::Vec3{0.4f, 0.7f, -0.6f}); // from behind the eye

    auto root = r4::scene::make<r4::scene::Group>();
    if (argc > 1 && argv[1][0] != '-') {
        // a file: loaded into the neutral model, then uploaded as a subgraph
        std::string error;
        auto model = r4::assets::loadModel(argv[1], &error);
        if (!model) {
            std::fprintf(stderr, "hello_model: %s\n", error.c_str());
            return 1;
        }
        root->addChild(renderer.upload(*model));
    } else {
        // no file: a mesh made in code, in a Geode
        auto geode = r4::scene::make<r4::scene::Geode>();
        geode->addDrawable(example::drawable(renderer, example::box({2, 1, 0.6f}), {0.9f, 0.55f, 0.2f, 1}));
        root->addChild(geode);
    }

    // the camera looks at the bounds of the scene
    r4::Sphere bounds = root->bounds();
    win.target = bounds.center;
    win.distance = bounds.radius * 3;
    r4::render::Camera camera;
    while (win.poll()) {
        win.apply(camera, win.distance * 0.01f, win.distance + bounds.radius * 4);
        renderer.render(*root, camera, 0, 0, win.width, win.height);
        win.swap();
    }
    return 0;
}
