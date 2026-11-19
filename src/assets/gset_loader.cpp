// gset_loader.cpp - the line drawings of the Mekton demo (.gset): the
// texts and the instruments of its head-up display. Text lines:
//   G <type>        a new geoset (LINESTRIPS)
//   C n, c r g b a  its color
//   V n, v x y z    its vertices
//   L n, l len      the lengths of its line strips
// Each geoset becomes a mesh of unlit lines.
#include "r4/assets/model.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace r4::assets {

std::shared_ptr<Model> loadGSet(const std::string &path, std::string *error) {
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) {
        if (error) {
            *error = "cannot read " + path;
        }
        return nullptr;
    }
    struct G {
        Vec4 color{1, 1, 1, 1};
        std::vector<Vec3> v;
        std::vector<int> lengths;
    };
    std::vector<G> gsets;
    char line[256];
    char kw[32];
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "%31s", kw) != 1) {
            continue;
        }
        if (!std::strcmp(kw, "G")) {
            gsets.emplace_back();
        } else if (gsets.empty()) {
            continue;
        } else if (!std::strcmp(kw, "c")) {
            Vec4 &c = gsets.back().color;
            std::sscanf(line, "%*s %f %f %f %f", &c.x, &c.y, &c.z, &c.w);
        } else if (!std::strcmp(kw, "v")) {
            Vec3 p;
            std::sscanf(line, "%*s %f %f %f", &p.x, &p.y, &p.z);
            gsets.back().v.push_back(p);
        } else if (!std::strcmp(kw, "l")) {
            int n = 0;
            std::sscanf(line, "%*s %d", &n);
            gsets.back().lengths.push_back(n);
        }
    }
    std::fclose(f);
    auto model = std::make_shared<Model>();
    model->source = path;
    Material m;
    m.name = "lines";
    m.shading = Shading::UNLIT;
    m.diffuse = {1, 1, 1, 1};
    m.vertexColors = true;
    model->materials.push_back(m);
    Node root;
    root.name = path;
    for (const G &g : gsets) {
        Mesh mesh;
        mesh.primitive = Primitive::LINES;
        mesh.material = 0;
        auto b = [](float x) {
            return (uint32_t)(std::fmin(std::fmax(x, 0.0f), 1.0f) * 255 + 0.5f);
        };
        uint32_t color = b(g.color.x) | b(g.color.y) << 8 | b(g.color.z) << 16 | b(g.color.w) << 24;
        for (const Vec3 &p : g.v) {
            Vertex v;
            v.position = p;
            v.color = color;
            mesh.vertices.push_back(v);
        }
        uint32_t k = 0;
        for (int len : g.lengths) {
            for (int j = 0; j + 1 < len && k + j + 1 < mesh.vertices.size(); j++) {
                mesh.indices.insert(mesh.indices.end(), {k + j, k + j + 1});
            }
            k += (uint32_t)std::max(len, 0);
        }
        if (mesh.indices.empty()) {
            continue;
        }
        if (color >> 24 < 255) {
            model->materials[0].alpha = AlphaMode::BLEND;
        }
        mesh.computeBounds();
        root.meshes.push_back((int)model->meshes.size());
        model->meshes.push_back(std::move(mesh));
    }
    if (model->meshes.empty()) {
        if (error) {
            *error = path + ": no line";
        }
        return nullptr;
    }
    model->nodes.push_back(root);
    return model;
}

} // namespace r4::assets
