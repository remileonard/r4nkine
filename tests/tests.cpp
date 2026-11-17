// tests.cpp - the tests of R4nkine that need no GPU: math, culling, scene
// graph bounds, loaders. Usage: r4_tests <the tests/data folder>
#include "r4/assets/model.h"
#include "r4/math.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"

#include <algorithm>
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
    Mat4 m = translate({1, 2, 3}) * rotate(radians(90), {0, 0, 1}) * scale({2, 2, 2});
    CHECK(near(transformPoint(m, {1, 0, 0}), {1, 4, 3}));
    Mat4 i = inverse(m);
    CHECK(near(transformPoint(i, transformPoint(m, {0.3f, -2, 5})), {0.3f, -2, 5}));
    CHECK(near(maxScale(m), 2));
    // the normals of a non-uniform scale
    Mat4 n = normalMatrix(scale({2, 1, 1}));
    CHECK(near(normalize(transformVector(n, {1, 1, 0})), normalize(Vec3{0.5f, 1, 0})));
    Mat4 v = lookAt({0, -10, 0}, {0, 0, 0}, {0, 0, 1});
    CHECK(near(transformPoint(v, {0, 0, 0}), {0, 0, -10}));
}

static void testBounds() {
    Sphere a{{0, 0, 0}, 1}, b{{4, 0, 0}, 1};
    a.extend(b);
    CHECK(near(a.center, {2, 0, 0}) && near(a.radius, 3));
    Sphere c{{0, 0, 0}, 5};
    c.extend({{1, 0, 0}, 1}); // inside
    CHECK(near(c.radius, 5));

    Mat4 vp = perspective(radians(90), 1, 1, 100) * lookAt({0, 0, 0}, {0, 1, 0}, {0, 0, 1});
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

// the loaders of the old formats, on the small files of tests/data
static void testLegacyFormats(const std::string &dir) {
    std::string err;
    auto ext = assets::modelExtensions();
    CHECK(ext.size() == 11);

    // flight (libgobj): two branches with their state bits, a translation,
    // a lit section and a colored one
    auto d = assets::loadModel(dir + "/plane.d", &err);
    CHECK(d != nullptr);
    if (d) {
        CHECK(d->nodes.size() == 4 && d->meshes.size() == 2);
        CHECK(d->nodes[2].mask == 0x1 && d->nodes[3].mask == 0x8);
        CHECK(near(translationOf(d->nodes[2].local), {0, 1, 0}));
        CHECK(d->defaultState == 0xff03);
        const assets::Material &lit = d->materials[d->meshes[0].material];
        CHECK(lit.shading == assets::Shading::Phong && near(lit.diffuse.x, 0.7f) && !lit.doubleSided);
        const assets::Mesh &shadow = d->meshes[1];
        CHECK(d->materials[shadow.material].shading == assets::Shading::Unlit);
        CHECK((shadow.vertices[0].color & 0xffffff) == 0x0000ff); // red, cpack 0xBBGGRR
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    // YAODL: the attributes of the polygons, a translated copy
    auto y = assets::loadModel(dir + "/cube.ydl", &err);
    CHECK(y != nullptr);
    if (y) {
        size_t tris = 0;
        float zmax = -1;
        bool red = false;
        for (const auto &m : y->meshes) {
            tris += m.indices.size() / 3;
            for (const auto &v : m.vertices) {
                zmax = std::max(zmax, v.position.z);
                red = red || (v.color & 0xffffff) == 0x0000ff;
            }
        }
        CHECK(tris == 4 && near(zmax, 2) && red);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    // flip: one quad with its normals
    auto b = assets::loadModel(dir + "/square.bin", &err);
    CHECK(b && b->meshes.size() == 1 && b->meshes[0].indices.size() == 6);
    if (b)
        CHECK(near(b->meshes[0].vertices[2].position, {1, 1, 0}) && near(b->meshes[0].vertices[0].normal, {0, 0, 1}));

    // Performer binary: two triangles, an overall color
    auto p = assets::loadModel(dir + "/quad.pfb", &err);
    CHECK(p != nullptr);
    if (p) {
        CHECK(p->meshes.size() == 1 && p->meshes[0].indices.size() == 6);
        CHECK(near(p->meshes[0].bounds.max, {2, 1, 0}));
        CHECK((p->meshes[0].vertices[0].color & 0xffffff) == 0x0000ff);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    // Designer's Workbench: a polygon of 4 vertices, its color
    auto w = assets::loadModel(dir + "/quad.dwb", &err);
    CHECK(w != nullptr);
    if (w) {
        CHECK(w->meshes.size() == 1 && w->meshes[0].indices.size() == 6);
        CHECK(near(w->meshes[0].bounds.max, {3, 1, 0}) && w->meshes[0].vertices[0].color == 0xff00ff00u);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    // OpenFlight 14.2: a group, a face of the vertex palette, packed color
    auto f = assets::loadModel(dir + "/tri.flt", &err);
    CHECK(f != nullptr);
    if (f) {
        CHECK(f->meshes.size() == 1 && f->meshes[0].indices.size() == 3);
        CHECK((f->meshes[0].vertices[0].color & 0xffffff) == 0xff0000); // blue
        CHECK(f->materials[f->meshes[0].material].shading == assets::Shading::Unlit);
        bool named = false;
        for (const auto &n : f->nodes)
            named = named || n.name == "g1";
        CHECK(named);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    // gset: one line strip of 3 points: 2 segments
    auto g = assets::loadModel(dir + "/lines.gset", &err);
    CHECK(g && g->meshes.size() == 1 && g->meshes[0].primitive == assets::Primitive::Lines &&
          g->meshes[0].indices.size() == 4);
}

// step 2: billboards, instancers, PNG, glTF
static void testStep2(const std::string &dir) {
    // a billboard turns its -Y face towards the eye
    scene::Billboard bb;
    Vec3 face = transformVector(bb.rotationFor({5, 0, 0}), {0, -1, 0});
    CHECK(near(face, {1, 0, 0}));
    face = transformVector(bb.rotationFor({0, 3, 4}), {0, -1, 0});
    CHECK(near(face, {0, 1, 0})); // axis: about Z only
    bb.setMode(scene::Billboard::Mode::Point);
    face = transformVector(bb.rotationFor({0, 3, 4}), {0, -1, 0});
    CHECK(near(face, normalize(Vec3{0, 3, 4})));

    // an instancer: its bounds hold all the instances, its levels by distance
    auto mesh = std::make_shared<render::GpuMesh>();
    mesh->bounds = {{0, 0, 0}, 1};
    scene::Instancer inst;
    inst.addLevel({{{mesh, nullptr}, Mat4()}}, 50);
    inst.addLevel({{{mesh, nullptr}, scale({0.5f, 0.5f, 0.5f})}}, 200);
    inst.addInstance(translate({10, 0, 0}));
    inst.addInstance(translate({-10, 0, 0}));
    const Sphere &s = inst.bounds();
    CHECK(near(s.center, {0, 0, 0}) && s.radius >= 11 && s.radius < 12);
    CHECK(inst.select(10) == 0 && inst.select(100) == 1 && inst.select(500) == -1);

    // the parts of a graph, in a state
    auto root = scene::make<scene::Group>();
    auto body = scene::make<scene::Geode>();
    auto shadow = scene::make<scene::Geode>();
    body->addDrawable({mesh, nullptr});
    shadow->addDrawable({mesh, nullptr});
    body->setMask(0x1);
    shadow->setMask(0x8);
    body->setMatrix(translate({0, 0, 2}));
    root->addChild(body);
    root->addChild(shadow);
    CHECK(scene::collectParts(*root).size() == 2);
    auto parts = scene::collectParts(*root, 0x1);
    CHECK(parts.size() == 1 && near(translationOf(parts[0].local), {0, 0, 2}));

    // PNG through stb_image, rows from the bottom
    std::string err;
    assets::Image img;
    CHECK(assets::loadImage(dir + "/rgba2x2.png", img, &err));
    if (img.valid()) {
        CHECK(img.width == 2 && img.height == 2 && img.channels == 4);
        // the bottom row: blue, white (alpha 128)
        CHECK(img.pixels[0] == 0 && img.pixels[2] == 255);
        CHECK(img.pixels[4] == 255 && img.pixels[7] == 128);
        // the top row: red, green
        CHECK(img.pixels[8] == 255 && img.pixels[9] == 0 && img.pixels[13] == 255);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    // glTF: the node, its translation, the material, the texture
    auto g = assets::loadModel(dir + "/tri.gltf", &err);
    CHECK(g != nullptr);
    if (g) {
        CHECK(g->meshes.size() == 1 && g->meshes[0].indices.size() == 3);
        CHECK(g->nodes.size() == 2 && g->nodes[0].children.size() == 1);
        CHECK(g->nodes[1].name == "tri" && near(translationOf(g->nodes[1].local), {0, 0, 5}));
        const assets::Material &m = g->materials[g->meshes[0].material];
        CHECK(m.name == "half" && near(m.diffuse.x, 0.5f) && m.doubleSided && m.diffuseImage == 0);
        CHECK(g->images.size() == 1 && g->images[0].width == 2);
        // the texture coordinates turned upside down (glTF: the rows go down)
        CHECK(near(g->meshes[0].vertices[2].uv.y, 0) && near(g->meshes[0].vertices[0].uv.y, 1));
        // Y-up turned to Z-up: the translation along glTF Z goes along -Y
        Box b = g->bounds();
        CHECK(near(b.min.y, -5) && near(b.max.z, 1));
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }
}

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/data";
    testMath();
    testBounds();
    testScene();
    testLoaders(dir);
    testLegacyFormats(dir);
    testStep2(dir);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
