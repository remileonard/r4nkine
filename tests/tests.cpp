// tests.cpp - the tests of R4nkine that need no GPU: math, culling, scene
// graph bounds, loaders. Usage: r4_tests <the tests/data folder>
#include "r4/assets/model.h"
#include "r4/math.h"
#include "r4/scene/node.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace r4;

static int failures = 0, checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool near(Vec3 a, Vec3 b, float eps = 1e-4f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

static void testMath() {
    Mat4 m = Mat4::translate({1, 2, 3}) * Mat4::rotate(radians(90), {0, 0, 1}) * Mat4::scale({2, 2, 2});
    CHECK(near(m.transformPoint({1, 0, 0}), {1, 4, 3}));
    Mat4 i = inverse(m);
    CHECK(near(i.transformPoint(m.transformPoint({0.3f, -2, 5})), {0.3f, -2, 5}));
    CHECK(near(m.maxScale(), 2));
    // the normals of a non-uniform scale
    Mat4 n = normalMatrix(Mat4::scale({2, 1, 1}));
    CHECK(near(normalize(n.transformVector({1, 1, 0})), normalize(Vec3{0.5f, 1, 0})));
    Mat4 v = Mat4::lookAt({0, -10, 0}, {0, 0, 0}, {0, 0, 1});
    CHECK(near(v.transformPoint({0, 0, 0}), {0, 0, -10}));
}

static void testBounds() {
    Sphere a{{0, 0, 0}, 1}, b{{4, 0, 0}, 1};
    a.extend(b);
    CHECK(near(a.center, {2, 0, 0}) && near(a.radius, 3));
    Sphere c{{0, 0, 0}, 5};
    c.extend({{1, 0, 0}, 1}); // inside
    CHECK(near(c.radius, 5));

    Mat4 vp = Mat4::perspective(radians(90), 1, 1, 100) * Mat4::lookAt({0, 0, 0}, {0, 1, 0}, {0, 0, 1});
    Frustum f = Frustum::fromMatrix(vp);
    CHECK(f.test({{0, 10, 0}, 1}) == Frustum::Inside);
    CHECK(f.test({{0, -10, 0}, 1}) == Frustum::Outside);   // behind
    CHECK(f.test({{0, 200, 0}, 1}) == Frustum::Outside);   // past far
    CHECK(f.test({{0, 100, 0}, 2}) == Frustum::Intersects); // on far
    CHECK(f.test({{30, 10, 0}, 1}) == Frustum::Outside);   // right of 45 degrees
}

static void testScene() {
    auto root = scene::make<scene::Group>();
    auto a = scene::make<scene::Group>();
    auto b = scene::make<scene::Group>();
    root->addChild(a);
    a->addChild(b);
    CHECK(root->bounds().empty());
    // a LOD with ranges
    scene::LOD lod;
    lod.setRanges({0, 10, 100});
    lod.addChild(scene::make<scene::Group>());
    lod.addChild(scene::make<scene::Group>());
    CHECK(lod.select(5) == 0 && lod.select(50) == 1 && lod.select(500) == -1);
    // a switch
    scene::Switch sw;
    sw.setValue(1);
    CHECK(!sw.isActive(0) && sw.isActive(1));
    sw.setValue(scene::Switch::kAll);
    CHECK(sw.isActive(0));
    CHECK(root->findChild("x") == nullptr);
    b->setName("x");
    CHECK(root->findChild("x") == b);
}

static void testLoaders(const std::string &dir) {
    std::string err;
    auto model = assets::loadModel(dir + "/quad.obj", &err);
    CHECK(model != nullptr);
    if (!model) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return;
    }
    CHECK(model->meshes.size() == 2);
    CHECK(model->materials.size() == 1);
    const assets::Material &red = model->materials[0];
    CHECK(near(red.diffuse.x, 1) && near(red.diffuse.y, 0) && near(red.diffuse.w, 0.5f));
    CHECK(red.alpha == assets::AlphaMode::Blend);
    CHECK(near(red.shininess, 32));
    // the cube: 6 quads in 12 triangles, 8 shared corners, smooth normals made
    const assets::Mesh &cube = model->meshes[0];
    CHECK(cube.indices.size() == 36 && cube.vertices.size() == 8);
    CHECK(near(length(cube.vertices[0].normal), 1));
    // the corner (1, 1, 1), vertex 6, points out of the cube
    CHECK(cube.vertices[6].normal.x > 0.3f && cube.vertices[6].normal.y > 0.3f && cube.vertices[6].normal.z > 0.3f);
    CHECK(near(cube.bounds.min, {-1, -1, -1}) && near(cube.bounds.max, {1, 1, 1}));
    // the quad by negative indices, with uvs
    const assets::Mesh &quad = model->meshes[1];
    CHECK(quad.material == 0 && quad.indices.size() == 6);
    CHECK(near(quad.vertices[2].uv.x, 1) && near(quad.vertices[2].uv.y, 1));
    CHECK(near(model->bounds().max, {1, 1, 1}));

    assets::Image img;
    CHECK(assets::loadImage(dir + "/rgb2x2.rgb", img, &err));
    CHECK(img.width == 2 && img.height == 2 && img.channels == 3);
    if (img.valid()) {
        // the bottom row first: red, green; the top row: blue, white
        CHECK(img.pixels[0] == 255 && img.pixels[1] == 0 && img.pixels[2] == 0);
        CHECK(img.pixels[3] == 0 && img.pixels[4] == 255 && img.pixels[5] == 0);
        CHECK(img.pixels[6] == 0 && img.pixels[7] == 0 && img.pixels[8] == 255);
        CHECK(img.pixels[9] == 255 && img.pixels[10] == 0 && img.pixels[11] == 255);
    }
    CHECK(!assets::loadModel(dir + "/missing.obj", &err));
}

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/data";
    testMath();
    testBounds();
    testScene();
    testLoaders(dir);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
