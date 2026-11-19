// shapes.h - meshes made in code, so that the examples run without files:
// boxes, cylinders, cones and a ground. Z is up, as everywhere in R4nkine.
#pragma once

#include "r4/assets/model.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"

#include <cmath>

namespace example {

using r4::Vec3;
using r4::assets::Mesh;

inline void addQuad(Mesh &m, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    Vec3 n = r4::normalize(r4::cross(b - a, c - a));
    uint32_t base = (uint32_t)m.vertices.size();
    for (Vec3 p : {a, b, c, d}) {
        r4::assets::Vertex v;
        v.position = p;
        v.normal = n;
        m.vertices.push_back(v);
    }
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

// a box of the size given, centered on the origin
inline Mesh box(Vec3 size) {
    Mesh m;
    m.name = "box";
    Vec3 h = size * 0.5f;
    Vec3 p[8];
    for (int i = 0; i < 8; i++) {
        p[i] = {i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z};
    }
    addQuad(m, p[0], p[2], p[3], p[1]); // bottom
    addQuad(m, p[4], p[5], p[7], p[6]); // top
    addQuad(m, p[0], p[1], p[5], p[4]); // front (-Y)
    addQuad(m, p[2], p[6], p[7], p[3]); // back
    addQuad(m, p[0], p[4], p[6], p[2]); // left
    addQuad(m, p[1], p[3], p[7], p[5]); // right
    m.computeBounds();
    return m;
}

// a cone (top radius 0) or a cylinder, along +Z from z0 to z1
inline Mesh cylinder(float r0, float r1, float z0, float z1, int segments = 16) {
    Mesh m;
    m.name = r1 > 0 ? "cylinder" : "cone";
    float slope = (r0 - r1) / (z1 - z0);
    for (int i = 0; i <= segments; i++) {
        float a = 2 * r4::kPi * i / segments;
        float c = std::cos(a);
        float s = std::sin(a);
        Vec3 n = r4::normalize(Vec3{c, s, slope});
        r4::assets::Vertex lo;
        r4::assets::Vertex hi;
        lo.position = {r0 * c, r0 * s, z0};
        hi.position = {r1 * c, r1 * s, z1};
        lo.normal = hi.normal = n;
        m.vertices.push_back(lo);
        m.vertices.push_back(hi);
    }
    for (uint32_t i = 0; i < (uint32_t)segments; i++) {
        uint32_t b = 2 * i;
        m.indices.insert(m.indices.end(), {b, b + 2, b + 3, b, b + 3, b + 1});
    }
    m.computeBounds();
    return m;
}

// a square on the ground, z = 0
inline Mesh ground(float size) {
    Mesh m;
    m.name = "ground";
    float h = size * 0.5f;
    addQuad(m, {-h, -h, 0}, {h, -h, 0}, {h, h, 0}, {-h, h, 0});
    m.computeBounds();
    return m;
}

// a mesh with a plain material, ready for a Geode or an Instancer
inline r4::scene::Drawable drawable(r4::render::Renderer &renderer, const Mesh &mesh, r4::Vec4 color) {
    r4::assets::Material mat;
    mat.diffuse = color;
    mat.specular = {0.2f, 0.2f, 0.2f};
    return {renderer.uploadMesh(mesh), renderer.createMaterial(mat)};
}

} // namespace example
