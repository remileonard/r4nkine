// obj_loader.cpp - the Wavefront OBJ format and its MTL materials.
//
// Faces of any size (fans), negative indices, v/vt/vn in any combination,
// groups and usemtl. One mesh per material: the vertices with the same
// position/uv/normal triple are shared. The old OBJ of the SGI demos often
// have no normals: smooth ones are made.
#include "r4/assets/model.h"

#include <cstdio>
#include <cstdlib>
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
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool readLines(const std::string &path, std::vector<std::string> &lines) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::string line;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, f)) {
        line += buf;
        if (line.empty() || line.back() != '\n')
            continue;
        // a line ending with a backslash continues on the next one
        std::string t = trim(line);
        if (!t.empty() && t.back() == '\\') {
            line = t.substr(0, t.size() - 1) + " ";
            continue;
        }
        lines.push_back(t);
        line.clear();
    }
    if (!line.empty())
        lines.push_back(trim(line));
    std::fclose(f);
    return true;
}

struct Key {
    int v, t, n;
    bool operator==(const Key &o) const { return v == o.v && t == o.t && n == o.n; }
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

void loadMTL(const std::string &path, Model &model, std::map<std::string, int> &byName,
             std::map<std::string, int> &images) {
    std::vector<std::string> lines;
    if (!readLines(path, lines))
        return;
    std::string dir = directoryOf(path);
    Material *m = nullptr;
    for (const std::string &line : lines) {
        char tag[64] = {0};
        if (line.empty() || line[0] == '#' || std::sscanf(line.c_str(), "%63s", tag) != 1)
            continue;
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
            if (m->diffuse.w < 1)
                m->alpha = AlphaMode::Blend;
        } else if (!std::strcmp(tag, "illum")) {
            if (std::atoi(rest) == 0)
                m->shading = Shading::Unlit;
        } else if (!std::strcmp(tag, "map_Kd")) {
            // the last word is the file; the options before it are skipped
            std::string file = trim(rest);
            size_t sp = file.find_last_of(" \t");
            if (sp != std::string::npos)
                file = file.substr(sp + 1);
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
            if (m->diffuseImage >= 0 && model.images[m->diffuseImage].channels == 4 && m->alpha == AlphaMode::Opaque)
                m->alpha = AlphaMode::Mask;
        }
    }
}

// "v/t/n", "v//n", "v/t" or "v" with negative indices from the end
bool parseCorner(const char *s, int nv, int nt, int nn, Key &k) {
    k = {-1, -1, -1};
    char *end;
    long v = std::strtol(s, &end, 10);
    if (end == s)
        return false;
    k.v = v < 0 ? nv + (int)v : (int)v - 1;
    if (*end == '/') {
        s = end + 1;
        if (*s != '/') {
            long t = std::strtol(s, &end, 10);
            if (end != s)
                k.t = t < 0 ? nt + (int)t : (int)t - 1;
            s = end;
        }
        if (*s == '/') {
            s++;
            long n = std::strtol(s, &end, 10);
            if (end != s)
                k.n = n < 0 ? nn + (int)n : (int)n - 1;
        }
    }
    return k.v >= 0 && k.v < nv;
}

} // namespace

std::shared_ptr<Model> loadOBJ(const std::string &path, std::string *error) {
    std::vector<std::string> lines;
    if (!readLines(path, lines)) {
        if (error)
            *error = "cannot read " + path;
        return nullptr;
    }
    auto model = std::make_shared<Model>();
    model->source = path;
    std::string dir = directoryOf(path);
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    std::map<std::string, int> materialByName, images;
    std::map<int, Builder> builders; // by material, in the order met
    std::vector<int> order;
    int material = -1;

    for (const std::string &line : lines) {
        if (line.empty() || line[0] == '#')
            continue;
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
            if (!builders.count(material))
                order.push_back(material);
            Builder &b = builders[material];
            std::vector<uint32_t> corners;
            const char *p = s + 2;
            while (*p) {
                while (*p == ' ' || *p == '\t')
                    p++;
                if (!*p)
                    break;
                Key k;
                if (parseCorner(p, (int)positions.size(), (int)uvs.size(), (int)normals.size(), k)) {
                    if (k.t >= (int)uvs.size())
                        k.t = -1;
                    if (k.n >= (int)normals.size())
                        k.n = -1;
                    auto it = b.map.find(k);
                    if (it == b.map.end()) {
                        Vertex v;
                        v.position = positions[k.v];
                        if (k.t >= 0)
                            v.uv = uvs[k.t];
                        if (k.n >= 0)
                            v.normal = normals[k.n];
                        else
                            b.missingNormals = true;
                        it = b.map.emplace(k, (uint32_t)b.mesh.vertices.size()).first;
                        b.mesh.vertices.push_back(v);
                    }
                    corners.push_back(it->second);
                }
                while (*p && *p != ' ' && *p != '\t')
                    p++;
            }
            for (size_t i = 1; i + 1 < corners.size(); i++) {
                b.mesh.indices.push_back(corners[0]);
                b.mesh.indices.push_back(corners[i]);
                b.mesh.indices.push_back(corners[i + 1]);
            }
        } else if (!std::strncmp(s, "mtllib", 6)) {
            // several files may follow
            std::string names = trim(s + 6);
            size_t pos = 0;
            while (pos < names.size()) {
                size_t sp = names.find_first_of(" \t", pos);
                std::string name = names.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
                if (!name.empty())
                    loadMTL(dir + name, *model, materialByName, images);
                if (sp == std::string::npos)
                    break;
                pos = sp + 1;
            }
        } else if (!std::strncmp(s, "usemtl", 6)) {
            std::string name = trim(s + 6);
            auto it = materialByName.find(name);
            if (it == materialByName.end()) {
                // a material of a missing library: a grey one with its name
                Material m;
                m.name = name;
                it = materialByName.emplace(name, (int)model->materials.size()).first;
                model->materials.push_back(m);
            }
            material = it->second;
        }
    }

    Node root;
    root.name = path;
    // OBJ is Y-up, R4nkine is Z-up: the root turns +Y into +Z
    root.local = rotate(radians(90), {1, 0, 0});
    for (int m : order) {
        Builder &b = builders[m];
        if (b.mesh.indices.empty())
            continue;
        if (b.missingNormals)
            b.mesh.computeNormals();
        b.mesh.material = m;
        b.mesh.name = m >= 0 ? model->materials[m].name : "default";
        b.mesh.computeBounds();
        root.meshes.push_back((int)model->meshes.size());
        model->meshes.push_back(std::move(b.mesh));
    }
    if (model->meshes.empty()) {
        if (error)
            *error = path + ": no faces";
        return nullptr;
    }
    model->nodes.push_back(std::move(root));
    return model;
}

} // namespace r4::assets
