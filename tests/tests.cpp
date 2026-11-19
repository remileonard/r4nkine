// tests.cpp - the tests of R4nkine that need no GPU: math, culling, scene
// graph bounds, loaders. Usage: r4_tests <the tests/data folder>
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
    // the normals of a non-uniform scale
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
    c.extend({{1, 0, 0}, 1}); // inside
    CHECK(near(c.radius, 5));

    Mat4 vp = perspective(radians(90), 1, 1, 100) * lookAt({0, 0, 0}, {0, 1, 0}, {0, 0, 1});
    Frustum f = Frustum::fromMatrix(vp);
    CHECK(f.test({{0, 10, 0}, 1}) == Frustum::Inside);
    CHECK(f.test({{0, -10, 0}, 1}) == Frustum::Outside);    // behind
    CHECK(f.test({{0, 200, 0}, 1}) == Frustum::Outside);    // past far
    CHECK(f.test({{0, 100, 0}, 2}) == Frustum::Intersects); // on far
    CHECK(f.test({{30, 10, 0}, 1}) == Frustum::Outside);    // right of 45 degrees
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
    CHECK(red.alpha == assets::AlphaMode::BLEND);
    CHECK(near(red.shininess, 32));
    // the cube: 6 quads in 12 triangles; no normals in the file: made, sharp
    // at its edges of 90 degrees (beyond the crease angle), so each of the 8
    // corners split in 3, one per face, with the normal of its face
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
        // and out of the cube: the face's normal is along its center
        flat = flat && dot(face, a.position + b.position + c.position) > 0;
    }
    CHECK(flat);
    // a smooth surface stays smooth: a fan of 8 triangles around a pole,
    // tilted 20 degrees, has one normal at the pole, straight up
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
        CHECK(lit.shading == assets::Shading::PHONG && near(lit.diffuse.x, 0.7f) && !lit.doubleSided);
        const assets::Mesh &shadow = d->meshes[1];
        CHECK(d->materials[shadow.material].shading == assets::Shading::UNLIT);
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
    if (b) {
        CHECK(near(b->meshes[0].vertices[2].position, {1, 1, 0}) && near(b->meshes[0].vertices[0].normal, {0, 0, 1}));
    }

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
        CHECK(f->materials[f->meshes[0].material].shading == assets::Shading::UNLIT);
        bool named = false;
        for (const auto &n : f->nodes) {
            named = named || n.name == "g1";
        }
        CHECK(named);
    } else {
        std::fprintf(stderr, "%s\n", err.c_str());
    }

    // gset: one line strip of 3 points: 2 segments
    auto g = assets::loadModel(dir + "/lines.gset", &err);
    CHECK(g && g->meshes.size() == 1 && g->meshes[0].primitive == assets::Primitive::LINES && g->meshes[0].indices.size() == 4);
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

static void testRaster() {
    using namespace r4::raster;
    // indexed: the drawings write indices, clipped
    Surface s(32, 20);
    s.clear(5);
    CHECK(s.get(0, 0) == 5 && s.get(31, 19) == 5);
    s.pixel(3, 4, 9);
    s.pixel(40, 4, 9); // outside: nothing
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
    // blits: the transparent index skipped, the offset added
    Surface sprite(2, 2);
    sprite.clear(255);
    sprite.pixel(0, 0, 10);
    Surface t(4, 4);
    t.clear(0);
    t.blit(sprite, 1, 1, 0, 0, 0, 0, 3);
    CHECK(t.get(1, 1) == 13 && t.get(2, 2) == 0);
    // scroll: the content moves, the rest takes the fill
    t.scroll(1, 0, 8);
    CHECK(t.get(2, 1) == 13 && t.get(0, 1) == 8);
    // text: some pixels of the color
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
    // true colors, blended
    Surface c(8, 8, Format::RGBA8);
    c.clear(rgba(0, 0, 255));
    c.blend = true;
    c.fillRect(0, 0, 4, 4, rgba(255, 0, 0, 128));
    uint32_t p = c.get(1, 1);
    CHECK(red(p) > 100 && blue(p) > 100 && alpha(p) == 255);
    // the palettes
    Palette vga = Palette::vga();
    CHECK(vga.colors[15] == rgba(255, 255, 255) && vga.nearest(250, 250, 250) == 15);
    CHECK(vga.fadedTo(rgba(0, 0, 0), 1).colors[15] == rgba(0, 0, 0));
    CHECK(vga.rotated(1, 3, 1).colors[2] == vga.colors[1]);
    // indexed to RGBA through the palette, the transparent index clear
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
    // Y-up: the up becomes kUp, the forward (-Z) kForward, the right stays
    CHECK(near(fromYUp({0, 1, 0}), kUp));
    CHECK(near(fromYUp({0, 0, -1}), kForward));
    CHECK(near(fromYUp({1, 0, 0}), kRight));
    Vec3 p{1.5f, -2.0f, 3.25f};
    CHECK(near(toYUp(fromYUp(p)), p));
    // the matrix and the function agree, and keep the handedness
    CHECK(near(transformPoint(fromYUpMatrix(), p), fromYUp(p)));
    CHECK(near(transformPoint(toYUpMatrix(), fromYUp(p)), p));
    CHECK(near(dot(cross(kRight, kForward), kUp), 1));
    // a Y-up transform, in R4nkine's axes: a turn about the Y-up up is a
    // turn about kUp
    Mat4 yUpTurn = rotate(radians(30.0f), {0, 1, 0});
    CHECK(near(transformPoint(transformFromYUp(yUpTurn), kForward), transformPoint(rotate(radians(30.0f), kUp), kForward)));
    // the angles
    CHECK(near(headingOf(kForward), 0) && near(headingOf(-kRight), 90) && near(headingOf(kRight), -90));
    CHECK(near(pitchOf(kUp), 90) && near(pitchOf(kForward), 0) && near(heightOf({4, 5, 6}), 6));
    // the orbit gives back its angles
    render::Orbit orbit;
    orbit.setAngles(-140, 20, 10);
    float h;
    float pi;
    float r;
    orbit.angles(h, pi, r);
    CHECK(near(h, -140, 1e-3f) && near(pi, 20, 1e-3f) && near(r, 10, 1e-3f));
}

// the scalers: the sizes, a flat image stays flat, a hard edge and a clear
// texel stay as they are, an oblique line gets smoothed
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
    // a vertical edge, left white, right clear: xBR keeps it sharp (2xSaI
    // blends the column next to it)
    Image edge = make(8, 8, [](int x, int) {
        return x < 4 ? 0xffffffffu : 0x00000000u;
    });
    edge.clampS = edge.clampT = true;
    Image sharp = upscale(edge, Upscale::XBR_2X);
    CHECK(at(sharp, 7, 5) == 0xffffffffu && at(sharp, 8, 5) == 0x00000000u);
    // a diagonal staircase: xBR blends the steps (some texels between the 2 colors)
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
    // Z up: x turns to y, scaled by 2; z scaled by 3
    Mat4 m = inst.matrixOf(p);
    CHECK(near(transformPoint(m, {1, 0, 0}), {10, 22, 30}));
    CHECK(near(transformPoint(m, {0, 0, 1}), {10, 20, 33}));
    // Y up (a subgraph of Y-up data): y scaled by 3
    inst.setUp({0, 1, 0});
    m = inst.matrixOf(p);
    CHECK(near(transformPoint(m, {0, 1, 0}), {10, 23, 30}));
    CHECK(near(transformPoint(m, {1, 0, 0}), {10, 20, 28}));
    CHECK(inst.count() == 0);
    inst.setPlacements({p, p});
    CHECK(inst.count() == 2);
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
    testRaster();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
