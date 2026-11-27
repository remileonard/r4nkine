// tests.cpp - the tests of R4nkine that need no GPU. Usage: r4_tests <the tests/data folder>
#include "r4/assets/model.h"
#include "r4/assets/upscale.h"
#include "r4/axes.h"
#include "r4/render/orbit.h"
#include "r4/math.h"
#include "r4/raster/raster.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace r4;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        checks++;                                                                         \
        if (!(cond)) {                                                                    \
            failures++;                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                                 \
    } while (0)

static bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}
static bool near(Vec3 a, Vec3 b, float eps = 1e-4f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

static void testMath() {
    Mat4 m = translate({1, 2, 3}) * rotate(radians(90), {0, 0, 1}) * scale({2, 2, 2});
    CHECK(near(transformPoint(m, {1, 0, 0}), {1, 4, 3}));
    Mat4 i = inverse(m);
    CHECK(near(transformPoint(i, transformPoint(m, {0.3f, -2, 5})), {0.3f, -2, 5}));
    CHECK(near(maxScale(m), 2));
    Mat4 n = normalMatrix(scale({2, 1, 1}));
    CHECK(near(normalize(transformVector(n, {1, 1, 0})), normalize(Vec3{0.5f, 1, 0})));
    Mat4 v = lookAt({0, -10, 0}, {0, 0, 0}, {0, 0, 1});
    CHECK(near(transformPoint(v, {0, 0, 0}), {0, 0, -10}));
}

static void testBounds() {
    Sphere a{{0, 0, 0}, 1};
    Sphere b{{4, 0, 0}, 1};
    a.extend(b);
    CHECK(near(a.center, {2, 0, 0}) && near(a.radius, 3));
    Sphere c{{0, 0, 0}, 5};
    c.extend({{1, 0, 0}, 1});
    CHECK(near(c.radius, 5));

    Mat4 vp = perspective(radians(90), 1, 1, 100) * lookAt({0, 0, 0}, {0, 1, 0}, {0, 0, 1});
    Frustum f = Frustum::fromMatrix(vp);
    CHECK(f.test({{0, 10, 0}, 1}) == Frustum::Inside);
    CHECK(f.test({{0, -10, 0}, 1}) == Frustum::Outside);
    CHECK(f.test({{0, 200, 0}, 1}) == Frustum::Outside);
    CHECK(f.test({{0, 100, 0}, 2}) == Frustum::Intersects);
    CHECK(f.test({{30, 10, 0}, 1}) == Frustum::Outside);
}

static void testScene() {
    auto root = scene::make<scene::Group>();
    auto a = scene::make<scene::Group>();
    auto b = scene::make<scene::Group>();
    root->addChild(a);
    a->addChild(b);
    CHECK(root->bounds().empty());
    scene::LOD lod;
    lod.setRanges({0, 10, 100});
    lod.addChild(scene::make<scene::Group>());
    lod.addChild(scene::make<scene::Group>());
    CHECK(lod.select(5) == 0 && lod.select(50) == 1 && lod.select(500) == -1);
    scene::Switch sw;
    sw.setValue(1);
    CHECK(!sw.isActive(0) && sw.isActive(1));
    sw.setValue(scene::Switch::kAll);
    CHECK(sw.isActive(0));
    auto area = scene::make<scene::Trigger>();
    area->setCylinder(10, 5);
    area->setMatrix(translate({100, 0, 0}));
    root->addChild(area);
    std::vector<uint32_t> entered;
    std::vector<uint32_t> left;
    area->onEnter = [&](uint32_t id) {
        entered.push_back(id);
    };
    area->onLeave = [&](uint32_t id) {
        left.push_back(id);
    };
    scene::checkTriggers(*root, {{1, {105, 0, 2}}, {2, {100, 0, 6}}});
    CHECK(entered.size() == 1 && entered[0] == 1 && left.empty());
    scene::checkTriggers(*root, {{1, {120, 0, 2}}, {2, {100, 0, 4}}});
    CHECK(entered.size() == 2 && entered[1] == 2 && left.size() == 1 && left[0] == 1);
    scene::checkTriggers(*root, {});
    CHECK(left.size() == 2 && area->inside().empty());
    scene::Trigger sphere;
    sphere.setSphere(2);
    CHECK(sphere.contains({1, 1, 1}) && !sphere.contains({2, 2, 0}));
    scene::Trigger box;
    box.setBox({{-1, -2, 0}, {3, 4, 5}});
    CHECK(box.contains({2, -1, 4}) && !box.contains({2, -3, 4}));
    root->removeChild(area.get());
    scene::Instancer trees;
    std::vector<scene::Placement> placed;
    for (int i = 0; i < 6; i++) {
        scene::Placement p;
        p.position = {(float)(i % 2) * 1000 + 10, (float)(i / 2) * 1000 + 20, 5};
        p.scale = 1 + (float)i;
        placed.push_back(p);
    }
    trees.setPlacements(placed);
    const auto &cells = trees.cells();
    CHECK(cells.size() == 6 && trees.placements().size() == 6);
    uint32_t inCells = 0;
    for (const auto &c : cells) {
        inCells += c.end - c.begin;
        CHECK(c.end - c.begin == 1 && near(c.maxScale, trees.placements()[c.begin].scale));
    }
    CHECK(inCells == 6);
    std::vector<std::string> order;
    a->setAppCallbacks(
        [&](scene::Node &n) {
            order.push_back("before " + n.name());
        },
        [&](scene::Node &n) {
            order.push_back("after " + n.name());
        });
    a->setName("a");
    b->setAppCallbacks([&](scene::Node &n) {
        order.push_back("b");
        n.setMatrix(translate({1, 0, 0}));
    });
    scene::app(*root);
    CHECK(order.size() == 3 && order[0] == "before a" && order[1] == "b" && order[2] == "after a");
    CHECK(near(b->matrix()[3].x, 1));
    b->setAppMask(2);
    order.clear();
    scene::app(*root, 1);
    CHECK(order.size() == 2);
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
    CHECK(red.alpha == assets::AlphaMode::BLEND);
    CHECK(near(red.shininess, 32));
    const assets::Mesh &cube = model->meshes[0];
    CHECK(cube.indices.size() == 36 && cube.vertices.size() == 24);
    bool flat = true;
    for (size_t i = 0; i < cube.indices.size(); i += 3) {
        const assets::Vertex &a = cube.vertices[cube.indices[i]];
        const assets::Vertex &b = cube.vertices[cube.indices[i + 1]];
        const assets::Vertex &c = cube.vertices[cube.indices[i + 2]];
        Vec3 face = normalize(cross(b.position - a.position, c.position - a.position));
        for (const assets::Vertex *v : {&a, &b, &c}) {
            flat = flat && near(v->normal, face);
        }
        flat = flat && dot(face, a.position + b.position + c.position) > 0;
    }
    CHECK(flat);
    {
        assets::Mesh cone;
        cone.vertices.push_back({});
        cone.vertices[0].position = {0, 0, 0};
        for (int i = 0; i < 8; i++) {
            assets::Vertex v;
            float a = radians(45.0f * i);
            v.position = {std::cos(a), std::sin(a), -std::tan(radians(20.0f))};
            cone.vertices.push_back(v);
        }
        for (uint32_t i = 0; i < 8; i++) {
            cone.indices.insert(cone.indices.end(), {0, 1 + i, 1 + (i + 1) % 8});
        }
        cone.computeNormals();
        int poles = 0;
        for (const assets::Vertex &v : cone.vertices) {
            if (near(v.position, {0, 0, 0})) {
                poles++;
                CHECK(near(v.normal, {0, 0, 1}));
            }
        }
        CHECK(poles == 1);
    }
    CHECK(near(cube.bounds.min, {-1, -1, -1}) && near(cube.bounds.max, {1, 1, 1}));
    const assets::Mesh &quad = model->meshes[1];
    CHECK(quad.material == 0 && quad.indices.size() == 6);
    CHECK(near(quad.vertices[2].uv.x, 1) && near(quad.vertices[2].uv.y, 1));
    CHECK(near(model->bounds().max, {1, 1, 1}));

    assets::Image img;
    CHECK(assets::loadImage(dir + "/rgb2x2.rgb", img, &err));
    CHECK(img.width == 2 && img.height == 2 && img.channels == 3);
    if (img.valid()) {
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
    // the 11 formats of R4nkine first, then those of Assimp when it is there
    CHECK(ext.size() >= 11 && ext[0] == "obj" && ext[10] == "glb");

    auto d = assets::loadModel(dir + "/plane.d", &err);
    CHECK(d != nullptr);
    if (d) {
        CHECK(d->nodes.size() == 4 && d->meshes.size() == 2);
        CHECK(d->nodes[2].mask == 0x1 && d->nodes[3].mask == 0x8);
        CHECK(near(translationOf(d->nodes[2].local), {0, 1, 0}));
        CHECK(d->defaultState == 0xff03);
        const assets::Material &lit = d->materials[d->meshes[0].material];
        CHECK(lit.shading == assets::Shading::PHONG && near(lit.diffuse.x, 0.7f) && !lit.doubleSided);
        const assets::Mesh &shadow = d->meshes[1];
        CHECK(d->materials[shadow.material].shading == assets::Shading::UNLIT);
        CHECK((shadow.vertices[0].color & 0xffffff) == 0x0000ff);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

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

    auto b = assets::loadModel(dir + "/square.bin", &err);
    CHECK(b && b->meshes.size() == 1 && b->meshes[0].indices.size() == 6);
    if (b) {
        CHECK(near(b->meshes[0].vertices[2].position, {1, 1, 0}) && near(b->meshes[0].vertices[0].normal, {0, 0, 1}));
    }

    auto p = assets::loadModel(dir + "/quad.pfb", &err);
    CHECK(p != nullptr);
    if (p) {
        CHECK(p->meshes.size() == 1 && p->meshes[0].indices.size() == 6);
        CHECK(near(p->meshes[0].bounds.max, {2, 1, 0}));
        CHECK((p->meshes[0].vertices[0].color & 0xffffff) == 0x0000ff);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    auto w = assets::loadModel(dir + "/quad.dwb", &err);
    CHECK(w != nullptr);
    if (w) {
        CHECK(w->meshes.size() == 1 && w->meshes[0].indices.size() == 6);
        CHECK(near(w->meshes[0].bounds.max, {3, 1, 0}) && w->meshes[0].vertices[0].color == 0xff00ff00u);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    auto f = assets::loadModel(dir + "/tri.flt", &err);
    CHECK(f != nullptr);
    if (f) {
        CHECK(f->meshes.size() == 1 && f->meshes[0].indices.size() == 3);
        CHECK((f->meshes[0].vertices[0].color & 0xffffff) == 0xff0000);
        CHECK(f->materials[f->meshes[0].material].shading == assets::Shading::UNLIT);
        bool named = false;
        for (const auto &n : f->nodes) {
            named = named || n.name == "g1";
        }
        CHECK(named);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    auto g = assets::loadModel(dir + "/lines.gset", &err);
    CHECK(g && g->meshes.size() == 1 && g->meshes[0].primitive == assets::Primitive::LINES && g->meshes[0].indices.size() == 4);
}

// billboards, instancers, PNG, glTF
static void testStep2(const std::string &dir) {
    scene::Billboard bb;
    Vec3 face = transformVector(bb.rotationFor({5, 0, 0}), {0, -1, 0});
    CHECK(near(face, {1, 0, 0}));
    face = transformVector(bb.rotationFor({0, 3, 4}), {0, -1, 0});
    CHECK(near(face, {0, 1, 0}));
    bb.setMode(scene::Billboard::Mode::Point);
    face = transformVector(bb.rotationFor({0, 3, 4}), {0, -1, 0});
    CHECK(near(face, normalize(Vec3{0, 3, 4})));

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

    std::string err;
    assets::Image img;
    CHECK(assets::loadImage(dir + "/rgba2x2.png", img, &err));
    if (img.valid()) {
        CHECK(img.width == 2 && img.height == 2 && img.channels == 4);
        CHECK(img.pixels[0] == 0 && img.pixels[2] == 255);
        CHECK(img.pixels[4] == 255 && img.pixels[7] == 128);
        CHECK(img.pixels[8] == 255 && img.pixels[9] == 0 && img.pixels[13] == 255);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    auto g = assets::loadModel(dir + "/tri.gltf", &err);
    CHECK(g != nullptr);
    if (g) {
        CHECK(g->meshes.size() == 1 && g->meshes[0].indices.size() == 3);
        CHECK(g->nodes.size() == 2 && g->nodes[0].children.size() == 1);
        CHECK(g->nodes[1].name == "tri" && near(translationOf(g->nodes[1].local), {0, 0, 5}));
        const assets::Material &m = g->materials[g->meshes[0].material];
        CHECK(m.name == "half" && near(m.diffuse.x, 0.5f) && m.doubleSided && m.diffuseImage == 0);
        CHECK(near(m.metallic, 0) && near(m.roughness, 1));
        CHECK(g->images.size() == 1 && g->images[0].width == 2);
        CHECK(near(g->meshes[0].vertices[2].uv.y, 0) && near(g->meshes[0].vertices[0].uv.y, 1));
        Box b = g->bounds();
        CHECK(near(b.min.y, -5) && near(b.max.z, 1));
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }
    // glass: a blend that lets 85 % of the light through; the emissive strength multiplies the emissive color
    auto glass = assets::loadModel(dir + "/glass.gltf", &err);
    CHECK(glass != nullptr);
    if (glass) {
        const assets::Material &m = glass->materials[glass->meshes[0].material];
        CHECK(m.alpha == assets::AlphaMode::BLEND && near(m.diffuse.w, 0.15f));
        CHECK(near(m.emissive.x, 0.2f) && near(m.emissive.z, 0.6f));
        CHECK(near(m.metallic, 0) && near(m.roughness, 0) && m.emissiveImage < 0);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }
    // a normal map and its scale; the quad beside it has none
    auto bump = assets::loadModel(dir + "/bump.gltf", &err);
    CHECK(bump != nullptr);
    if (bump) {
        const assets::Material &mapped = bump->materials[bump->meshes[0].material];
        const assets::Material &real = bump->materials[bump->meshes[1].material];
        CHECK(mapped.normalImage == 0 && near(mapped.normalScale, 0.5f) && real.normalImage < 0);
        CHECK(bump->images.size() == 1 && bump->images[0].width == 128 && bump->images[0].channels == 3);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }
}

static void testRaster() {
    using namespace r4::raster;
    Surface s(32, 20);
    s.clear(5);
    CHECK(s.get(0, 0) == 5 && s.get(31, 19) == 5);
    s.pixel(3, 4, 9);
    s.pixel(40, 4, 9);
    CHECK(s.get(3, 4) == 9);
    s.line(0, 0, 9, 9, 7);
    CHECK(s.get(5, 5) == 7 && s.get(9, 9) == 7);
    s.fillRect(10, 2, 4, 3, 1);
    CHECK(s.get(10, 2) == 1 && s.get(13, 4) == 1 && s.get(14, 4) == 5);
    s.rect(20, 2, 5, 5, 2);
    CHECK(s.get(20, 2) == 2 && s.get(24, 6) == 2 && s.get(22, 4) == 5);
    s.fillCircle(16, 10, 3, 3);
    CHECK(s.get(16, 10) == 3 && s.get(19, 10) == 3 && s.get(16, 14) == 5);
    s.setClip(0, 0, 8, 8);
    s.fillRect(0, 0, 32, 20, 4);
    CHECK(s.get(7, 7) == 4 && s.get(8, 8) != 4);
    s.resetClip();
    s.fillTriangle(0, 19, 6, 19, 0, 13, 6);
    CHECK(s.get(1, 18) == 6);
    Surface sprite(2, 2);
    sprite.clear(255);
    sprite.pixel(0, 0, 10);
    Surface t(4, 4);
    t.clear(0);
    t.blit(sprite, 1, 1, 0, 0, 0, 0, 3);
    CHECK(t.get(1, 1) == 13 && t.get(2, 2) == 0);
    t.scroll(1, 0, 8);
    CHECK(t.get(2, 1) == 13 && t.get(0, 1) == 8);
    Surface txt(60, 20);
    txt.clear(0);
    int w = txt.text(Font::builtin(13), 0, 0, "R4", 15);
    int lit = 0;
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 60; x++) {
            lit += txt.get(x, y) == 15;
        }
    }
    CHECK(w > 10 && lit > 20 && Font::builtin(40).height > Font::builtin(13).height);
    Surface c(8, 8, Format::RGBA8);
    c.clear(rgba(0, 0, 255));
    c.blend = true;
    c.fillRect(0, 0, 4, 4, rgba(255, 0, 0, 128));
    uint32_t p = c.get(1, 1);
    CHECK(red(p) > 100 && blue(p) > 100 && alpha(p) == 255);
    Palette vga = Palette::vga();
    CHECK(vga.colors[15] == rgba(255, 255, 255) && vga.nearest(250, 250, 250) == 15);
    CHECK(vga.fadedTo(rgba(0, 0, 0), 1).colors[15] == rgba(0, 0, 0));
    CHECK(vga.rotated(1, 3, 1).colors[2] == vga.colors[1]);
    Surface i(2, 1);
    i.pixel(0, 0, 15);
    i.pixel(1, 0, 255);
    uint8_t out[8];
    i.toRGBA(&vga, out);
    CHECK(out[0] == 255 && out[3] == 255 && out[7] == 0);
}

// the axes of R4nkine and their conversions (r4/axes.h)
static void testAxes() {
    using namespace r4::axes;
    CHECK(near(fromYUp({0, 1, 0}), kUp));
    CHECK(near(fromYUp({0, 0, -1}), kForward));
    CHECK(near(fromYUp({1, 0, 0}), kRight));
    Vec3 p{1.5f, -2.0f, 3.25f};
    CHECK(near(toYUp(fromYUp(p)), p));
    CHECK(near(transformPoint(fromYUpMatrix(), p), fromYUp(p)));
    CHECK(near(transformPoint(toYUpMatrix(), fromYUp(p)), p));
    CHECK(near(dot(cross(kRight, kForward), kUp), 1));
    Mat4 yUpTurn = rotate(radians(30.0f), {0, 1, 0});
    CHECK(near(transformPoint(transformFromYUp(yUpTurn), kForward), transformPoint(rotate(radians(30.0f), kUp), kForward)));
    CHECK(near(headingOf(kForward), 0) && near(headingOf(-kRight), 90) && near(headingOf(kRight), -90));
    CHECK(near(pitchOf(kUp), 90) && near(pitchOf(kForward), 0) && near(heightOf({4, 5, 6}), 6));
    render::Orbit orbit;
    orbit.setAngles(-140, 20, 10);
    float h;
    float pi;
    float r;
    orbit.angles(h, pi, r);
    CHECK(near(h, -140, 1e-3f) && near(pi, 20, 1e-3f) && near(r, 10, 1e-3f));
}

// the scalers keep the sizes, flat images, hard edges and clear texels, and smooth oblique lines
static void testUpscale() {
    using namespace r4::assets;
    auto make = [](int w, int h, auto color) {
        Image img;
        img.width = w;
        img.height = h;
        img.channels = 4;
        img.pixels.resize((size_t)w * h * 4);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint32_t c = color(x, y);
                std::memcpy(&img.pixels[((size_t)y * w + x) * 4], &c, 4);
            }
        }
        return img;
    };
    auto at = [](const Image &img, int x, int y) {
        uint32_t c;
        std::memcpy(&c, &img.pixels[((size_t)y * img.width + x) * 4], 4);
        return c;
    };
    Image flat = make(8, 6, [](int, int) {
        return 0xff3366ccu;
    });
    for (Upscale u : {Upscale::SAI_2X, Upscale::XBR_2X, Upscale::XBR_4X}) {
        Image big = upscale(flat, u);
        int k = upscaleFactor(u);
        CHECK(big.width == 8 * k && big.height == 6 * k && big.channels == 4);
        bool same = true;
        for (int y = 0; y < big.height; y++) {
            for (int x = 0; x < big.width; x++) {
                same = same && at(big, x, y) == 0xff3366ccu;
            }
        }
        CHECK(same);
    }
    Image edge = make(8, 8, [](int x, int) {
        return x < 4 ? 0xffffffffu : 0x00000000u;
    });
    edge.clampS = edge.clampT = true;
    Image sharp = upscale(edge, Upscale::XBR_2X);
    CHECK(at(sharp, 7, 5) == 0xffffffffu && at(sharp, 8, 5) == 0x00000000u);
    Image stairs = make(8, 8, [](int x, int y) {
        return x > y ? 0xff000000u : 0xffffffffu;
    });
    stairs.clampS = stairs.clampT = true;
    Image big = upscale(stairs, Upscale::XBR_2X);
    int between = 0;
    for (int y = 0; y < big.height; y++) {
        for (int x = 0; x < big.width; x++) {
            uint32_t c = at(big, x, y);
            between += c != 0xff000000u && c != 0xffffffffu;
        }
    }
    CHECK(between > 0);
    CHECK(std::string(upscaleName(Upscale::XBR_4X)) == "xBR 4x");
}

// the compact instances: their matrix about the up axis, their bounds
static void testPlacements() {
    using namespace r4::scene;
    Instancer inst;
    Placement p;
    p.position = {10, 20, 30};
    p.heading = r4::radians(90.0f);
    p.scale = 2;
    p.scaleUp = 3;
    Mat4 m = inst.matrixOf(p);
    CHECK(near(transformPoint(m, {1, 0, 0}), {10, 22, 30}));
    CHECK(near(transformPoint(m, {0, 0, 1}), {10, 20, 33}));
    inst.setUp({0, 1, 0});
    m = inst.matrixOf(p);
    CHECK(near(transformPoint(m, {0, 1, 0}), {10, 23, 30}));
    CHECK(near(transformPoint(m, {1, 0, 0}), {10, 20, 28}));
    CHECK(inst.count() == 0);
    inst.setPlacements({p, p});
    CHECK(inst.count() == 2);
}

// the other formats, when R4nkine is built with Assimp
static void testAssimp(const std::string &dir) {
    std::vector<std::string> ext = assets::assimpExtensions();
    if (ext.empty()) {
        std::string err;
        CHECK(!assets::loadModel(dir + "/quad.ply", &err) && !err.empty());
        return;
    }
    CHECK(std::find(ext.begin(), ext.end(), "fbx") != ext.end());
    std::vector<std::string> all = assets::modelExtensions();
    CHECK(std::count(all.begin(), all.end(), "obj") == 1);
    std::string err;
    auto m = assets::loadModel(dir + "/quad.ply", &err);
    CHECK(m != nullptr);
    if (!m) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(m->meshes.size() == 1 && m->meshes[0].vertices.size() == 4 && m->meshes[0].indices.size() == 6);
    CHECK(m->meshes[0].material >= 0 && m->materials[m->meshes[0].material].vertexColors);
    CHECK(m->meshes[0].vertices[0].color == 0xff0000ffu);
    // Y up turned to Z up at the root: the quad in the XY plane of the file stands in the XZ plane
    Box b = m->bounds();
    CHECK(near(b.min, {0, 0, 0}) && near(b.max, {1, 0, 1}));
}

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/data";
    testMath();
    testAxes();
    testUpscale();
    testPlacements();
    testBounds();
    testScene();
    testLoaders(dir);
    testLegacyFormats(dir);
    testStep2(dir);
    testAssimp(dir);
    testRaster();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
