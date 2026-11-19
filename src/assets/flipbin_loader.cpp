// flipbin_loader.cpp - the .bin objects of flip (the "fast" format of the
// SGI demos): a header of big-endian ints (magic 0x5423, the number of
// points, a colors field), then for each point its normal and its position
// (3 big-endian floats each). Every 4 points make a quadrilateral. Y-up.
#include "r4/axes.h"
#include "r4/assets/model.h"

#include <cstdio>
#include <cstring>

namespace r4::assets {

std::shared_ptr<Model> loadFlipBin(const std::string &path, std::string *error) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error) {
            *error = "cannot read " + path;
        }
        return nullptr;
    }
    std::vector<unsigned char> d;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        d.insert(d.end(), buf, buf + n);
    }
    std::fclose(f);
    auto be32 = [&](size_t at) {
        return (uint32_t)d[at] << 24 | (uint32_t)d[at + 1] << 16 | (uint32_t)d[at + 2] << 8 | d[at + 3];
    };
    auto fl = [&](size_t at) {
        uint32_t u = be32(at);
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    };
    if (d.size() < 12 || be32(0) != 0x5423) {
        if (error) {
            *error = path + ": not a flip object (bad magic)";
        }
        return nullptr;
    }
    uint32_t points = be32(4);
    if (points == 0 || 12 + (size_t)points * 24 > d.size()) {
        if (error) {
            *error = path + ": truncated flip object";
        }
        return nullptr;
    }
    auto model = std::make_shared<Model>();
    model->source = path;
    Mesh mesh;
    mesh.name = "flip";
    mesh.material = 0;
    for (uint32_t i = 0; i < points; i++) {
        size_t at = 12 + (size_t)i * 24;
        Vertex v;
        v.normal = {fl(at), fl(at + 4), fl(at + 8)};
        v.position = {fl(at + 12), fl(at + 16), fl(at + 20)};
        mesh.vertices.push_back(v);
    }
    for (uint32_t q = 0; q + 3 < points; q += 4) {
        mesh.indices.insert(mesh.indices.end(), {q, q + 1, q + 2, q, q + 2, q + 3});
    }
    mesh.computeBounds();
    Material m;
    m.name = "flip";
    m.diffuse = {0.75f, 0.75f, 0.75f, 1};
    m.specular = {0.6f, 0.6f, 0.6f};
    m.shininess = 30;
    m.doubleSided = true;
    model->materials.push_back(m);
    model->meshes.push_back(std::move(mesh));
    Node root;
    root.name = path;
    root.local = axes::fromYUpMatrix();
    root.meshes.push_back(0);
    model->nodes.push_back(root);
    return model;
}

} // namespace r4::assets
