// obj_loader.cpp - Wavefront OBJ and its MTL materials: one mesh per material, smooth normals made when the file has none.
#include "r4/axes.h"
#include "r4/assets/model.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>
#include <unordered_map>

namespace r4::assets {
namespace {

std::string directoryOf(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool readLines(const std::string &path, std::vector<std::string> &lines) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    std::string line;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, f)) {
        line += buf;
        if (line.empty() || line.back() != '\n') {
            continue;
        }
        std::string t = trim(line);
        if (!t.empty() && t.back() == '\\') {
            line = t.substr(0, t.size() - 1) + " ";
            continue;
        }
        lines.push_back(t);
        line.clear();
    }
    if (!line.empty()) {
        lines.push_back(trim(line));
    }
    std::fclose(f);
    return true;
}

struct Key {
    int v;
    int t;
    int n;
    bool operator==(const Key &o) const {
        return v == o.v && t == o.t && n == o.n;
    }
};
struct KeyHash {
    size_t operator()(const Key &k) const {
        return (size_t)k.v * 73856093u ^ (size_t)k.t * 19349663u ^ (size_t)k.n * 83492791u;
    }
};

struct Builder {
    Mesh mesh;
    std::unordered_map<Key, uint32_t, KeyHash> map;
    bool missingNormals = false;
};

// A material named by usemtl and not in a .mtl: rgb_RRGGBB is that color, glass a tinted glass, the others grey (the
// convention of the airframes of libRealSpace)
Material namedMaterial(const std::string &name) {
    Material m;
    m.name = name;
    unsigned rgb = 0;
    if (name.size() == 10 && std::sscanf(name.c_str(), "rgb_%6x", &rgb) == 1) {
        Vec3 c{((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f};
        m.diffuse = Vec4(c, 1.0f);
        m.ambient = c;
    } else if (name == "glass") {
        m.diffuse = {0.25f, 0.3f, 0.35f, 0.35f};
        m.ambient = {0.25f, 0.3f, 0.35f};
        m.specular = {0.6f, 0.6f, 0.6f};
        m.shininess = 64;
        m.alpha = AlphaMode::BLEND;
        m.doubleSided = true;
    } else {
        m.diffuse = {0.6f, 0.6f, 0.6f, 1.0f};
        m.ambient = {0.6f, 0.6f, 0.6f};
    }
    return m;
}

// "lod3" gives 3; -1 for another name
int lodOfGroup(const std::string &name) {
    int n = -1;
    char rest = 0;
    if (std::sscanf(name.c_str(), "lod%d%c", &n, &rest) == 1 && n >= 0) {
        return n;
    }
    return -1;
}

void loadMTL(const std::string &path, Model &model, std::map<std::string, int> &byName, std::map<std::string, int> &images) {
    std::vector<std::string> lines;
    if (!readLines(path, lines)) {
        return;
    }
    std::string dir = directoryOf(path);
    Material *m = nullptr;
    for (const std::string &line : lines) {
        char tag[64] = {0};
        if (line.empty() || line[0] == '#' || std::sscanf(line.c_str(), "%63s", tag) != 1) {
            continue;
        }
        const char *rest = line.c_str() + std::strlen(tag);
        if (!std::strcmp(tag, "newmtl")) {
            Material mat;
            mat.name = trim(rest);
            byName[mat.name] = (int)model.materials.size();
            model.materials.push_back(mat);
            m = &model.materials.back();
        } else if (!m) {
            continue;
        } else if (!std::strcmp(tag, "Kd")) {
            std::sscanf(rest, "%f %f %f", &m->diffuse.x, &m->diffuse.y, &m->diffuse.z);
        } else if (!std::strcmp(tag, "Ka")) {
            std::sscanf(rest, "%f %f %f", &m->ambient.x, &m->ambient.y, &m->ambient.z);
        } else if (!std::strcmp(tag, "Ks")) {
            std::sscanf(rest, "%f %f %f", &m->specular.x, &m->specular.y, &m->specular.z);
        } else if (!std::strcmp(tag, "Ke")) {
            std::sscanf(rest, "%f %f %f", &m->emissive.x, &m->emissive.y, &m->emissive.z);
        } else if (!std::strcmp(tag, "Ns")) {
            m->shininess = (float)std::atof(rest);
        } else if (!std::strcmp(tag, "d") || !std::strcmp(tag, "Tr")) {
            float d = (float)std::atof(rest);
            m->diffuse.w = !std::strcmp(tag, "Tr") ? 1 - d : d;
            if (m->diffuse.w < 1) {
                m->alpha = AlphaMode::BLEND;
            }
        } else if (!std::strcmp(tag, "illum")) {
            if (std::atoi(rest) == 0) {
                m->shading = Shading::UNLIT;
            }
        } else if (!std::strcmp(tag, "map_Kd")) {
            std::string file = trim(rest);
            size_t sp = file.find_last_of(" \t");
            if (sp != std::string::npos) {
                file = file.substr(sp + 1);
            }
            auto it = images.find(file);
            if (it == images.end()) {
                Image img;
                std::string err;
                if (!loadImage(dir + file, img, &err) && !loadImage(file, img, &err)) {
                    std::fprintf(stderr, "r4: %s\n", err.c_str());
                    it = images.emplace(file, -1).first;
                } else {
                    it = images.emplace(file, (int)model.images.size()).first;
                    model.images.push_back(std::move(img));
                }
            }
            m->diffuseImage = it->second;
            if (m->diffuseImage >= 0 && model.images[m->diffuseImage].channels == 4 && m->alpha == AlphaMode::NONE) {
                m->alpha = AlphaMode::MASK;
            }
        }
    }
}

// "v/t/n", "v//n", "v/t" or "v" with negative indices from the end
bool parseCorner(const char *s, int nv, int nt, int nn, Key &k) {
    k = {-1, -1, -1};
    char *end;
    long v = std::strtol(s, &end, 10);
    if (end == s) {
        return false;
    }
    k.v = v < 0 ? nv + (int)v : (int)v - 1;
    if (*end == '/') {
        s = end + 1;
        if (*s != '/') {
            long t = std::strtol(s, &end, 10);
            if (end != s) {
                k.t = t < 0 ? nt + (int)t : (int)t - 1;
            }
            s = end;
        }
        if (*s == '/') {
            s++;
            long n = std::strtol(s, &end, 10);
            if (end != s) {
                k.n = n < 0 ? nn + (int)n : (int)n - 1;
            }
        }
    }
    return k.v >= 0 && k.v < nv;
}

} // namespace

std::shared_ptr<Model> loadOBJ(const std::string &path, std::string *error) {
    std::vector<std::string> lines;
    if (!readLines(path, lines)) {
        if (error) {
            *error = "cannot read " + path;
        }
        return nullptr;
    }
    auto model = std::make_shared<Model>();
    model->source = path;
    std::string dir = directoryOf(path);
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::map<std::string, int> materialByName;
    std::map<std::string, int> images;
    // by (level of detail of the group, -1 without, material)
    std::map<std::pair<int, int>, Builder> builders;
    std::vector<std::pair<int, int>> order;
    int material = -1;
    int lod = -1;

    for (const std::string &line : lines) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const char *s = line.c_str();
        if (s[0] == 'v' && s[1] == ' ') {
            Vec3 p;
            std::sscanf(s + 2, "%f %f %f", &p.x, &p.y, &p.z);
            positions.push_back(p);
        } else if (s[0] == 'v' && s[1] == 'n') {
            Vec3 n;
            std::sscanf(s + 3, "%f %f %f", &n.x, &n.y, &n.z);
            normals.push_back(n);
        } else if (s[0] == 'v' && s[1] == 't') {
            Vec2 t;
            std::sscanf(s + 3, "%f %f", &t.x, &t.y);
            uvs.push_back(t);
        } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            const std::pair<int, int> key{lod, material};
            if (!builders.count(key)) {
                order.push_back(key);
            }
            Builder &b = builders[key];
            std::vector<uint32_t> corners;
            const char *p = s + 2;
            while (*p) {
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                if (!*p) {
                    break;
                }
                Key k;
                if (parseCorner(p, (int)positions.size(), (int)uvs.size(), (int)normals.size(), k)) {
                    if (k.t >= (int)uvs.size()) {
                        k.t = -1;
                    }
                    if (k.n >= (int)normals.size()) {
                        k.n = -1;
                    }
                    auto it = b.map.find(k);
                    if (it == b.map.end()) {
                        Vertex v;
                        v.position = positions[k.v];
                        if (k.t >= 0) {
                            v.uv = uvs[k.t];
                        }
                        if (k.n >= 0) {
                            v.normal = normals[k.n];
                        } else {
                            b.missingNormals = true;
                        }
                        it = b.map.emplace(k, (uint32_t)b.mesh.vertices.size()).first;
                        b.mesh.vertices.push_back(v);
                    }
                    corners.push_back(it->second);
                }
                while (*p && *p != ' ' && *p != '\t') {
                    p++;
                }
            }
            for (size_t i = 1; i + 1 < corners.size(); i++) {
                b.mesh.indices.push_back(corners[0]);
                b.mesh.indices.push_back(corners[i]);
                b.mesh.indices.push_back(corners[i + 1]);
            }
        } else if (!std::strncmp(s, "mtllib", 6)) {
            std::string names = trim(s + 6);
            size_t pos = 0;
            while (pos < names.size()) {
                size_t sp = names.find_first_of(" \t", pos);
                std::string name = names.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
                if (!name.empty()) {
                    loadMTL(dir + name, *model, materialByName, images);
                }
                if (sp == std::string::npos) {
                    break;
                }
                pos = sp + 1;
            }
        } else if (!std::strncmp(s, "usemtl", 6)) {
            std::string name = trim(s + 6);
            auto it = materialByName.find(name);
            if (it == materialByName.end()) {
                it = materialByName.emplace(name, (int)model->materials.size()).first;
                model->materials.push_back(namedMaterial(name));
            }
            material = it->second;
        } else if (s[0] == 'g' && (s[1] == ' ' || s[1] == '\t' || s[1] == 0)) {
            lod = lodOfGroup(trim(s + 1));
        }
    }

    Node root;
    root.name = path;
    root.local = axes::fromYUpMatrix();
    // the groups lod0, lod1...: the levels of detail of a LOD node, each a child, in the order of their numbers
    std::map<int, std::vector<int>> levels;
    Box lodBox;
    for (const std::pair<int, int> &key : order) {
        Builder &b = builders[key];
        const int m = key.second;
        if (b.mesh.indices.empty()) {
            continue;
        }
        if (b.missingNormals) {
            b.mesh.computeNormals();
        }
        b.mesh.material = m;
        b.mesh.name = m >= 0 ? model->materials[m].name : "default";
        b.mesh.computeBounds();
        const int index = (int)model->meshes.size();
        if (key.first >= 0) {
            levels[key.first].push_back(index);
            lodBox.extend(b.mesh.bounds);
        } else {
            root.meshes.push_back(index);
        }
        model->meshes.push_back(std::move(b.mesh));
    }
    model->nodes.push_back(root);
    if (!levels.empty()) {
        // without distances in the file: a level for each 30 radii of the object
        const Vec3 center = (lodBox.min + lodBox.max) * 0.5f;
        const float step = 30.0f * std::max(length(lodBox.max - lodBox.min) * 0.5f, 1e-3f);
        Node lodNode;
        lodNode.name = "lod";
        lodNode.kind = NodeKind::LOD;
        lodNode.lodCenter = center;
        lodNode.hasLodCenter = true;
        lodNode.lodRanges.push_back(0);
        const int lodIndex = (int)model->nodes.size();
        model->nodes.push_back(lodNode);
        model->nodes[0].children.push_back(lodIndex);
        int i = 0;
        for (auto &[number, meshes] : levels) {
            Node level;
            level.name = "lod" + std::to_string(number);
            level.meshes = meshes;
            i++;
            model->nodes[lodIndex].lodRanges.push_back(i < (int)levels.size() ? step * (float)i : 1e30f);
            model->nodes[lodIndex].children.push_back((int)model->nodes.size());
            model->nodes.push_back(level);
        }
    }
    if (model->meshes.empty()) {
        if (error) {
            *error = path + ": no faces";
        }
        return nullptr;
    }
    return model;
}

} // namespace r4::assets
