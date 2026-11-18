#include "r4/assets/model.h"

#include "r4/axes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <vector>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace r4::assets {

void Mesh::computeBounds() {
    bounds = Box{};
    for (const Vertex &v : vertices)
        bounds.extend(v.position);
}

void Mesh::computeNormals(float creaseDegrees) {
    if (primitive != Primitive::Triangles || indices.size() < 3)
        return;
    size_t triangles = indices.size() / 3;
    // the faces: weighted by their area (the cross product not normalized),
    // compared by their direction
    std::vector<Vec3> area(triangles), dir(triangles);
    for (size_t t = 0; t < triangles; t++) {
        const Vec3 &a = vertices[indices[3 * t]].position, &b = vertices[indices[3 * t + 1]].position,
                   &c = vertices[indices[3 * t + 2]].position;
        area[t] = cross(b - a, c - a);
        float l = length(area[t]);
        dir[t] = l > 0 ? area[t] / l : Vec3(0);
    }
    // the faces around each position (not each vertex: the corners split
    // for their uvs or colors are smoothed together)
    struct Less {
        bool operator()(const Vec3 &a, const Vec3 &b) const {
            return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
        }
    };
    std::map<Vec3, std::vector<uint32_t>, Less> around;
    for (size_t t = 0; t < triangles; t++)
        for (int k = 0; k < 3; k++)
            around[vertices[indices[3 * t + k]].position].push_back((uint32_t)t);
    float minCos = std::cos(radians(std::clamp(creaseDegrees, 0.0f, 180.0f)));
    // each corner: the faces around it that are within the crease angle of
    // its own; a vertex whose corners get different normals is split
    std::vector<Vertex> out;
    out.reserve(vertices.size());
    std::map<std::pair<uint32_t, std::array<int32_t, 3>>, uint32_t> made;
    for (size_t t = 0; t < triangles; t++)
        for (int k = 0; k < 3; k++) {
            uint32_t vi = indices[3 * t + k];
            Vec3 n(0);
            for (uint32_t u : around[vertices[vi].position])
                if (u == t || dot(dir[t], dir[u]) >= minCos)
                    n += area[u];
            float l = length(n);
            n = l > 0 ? n / l : (length(dir[t]) > 0 ? dir[t] : axes::kUp);
            std::array<int32_t, 3> q = {(int32_t)std::lround(n.x * 4096), (int32_t)std::lround(n.y * 4096),
                                        (int32_t)std::lround(n.z * 4096)};
            auto it = made.find({vi, q});
            if (it == made.end()) {
                Vertex v = vertices[vi];
                v.normal = n;
                it = made.emplace(std::make_pair(vi, q), (uint32_t)out.size()).first;
                out.push_back(v);
            }
            indices[3 * t + k] = it->second;
        }
    vertices = std::move(out);
}

Box Model::bounds() const {
    Box box;
    if (nodes.empty())
        return box;
    // the meshes under the root, through the matrices of the nodes
    struct Item {
        int node;
        Mat4 m;
    };
    std::vector<Item> stack{{0, nodes[0].local}};
    while (!stack.empty()) {
        Item it = stack.back();
        stack.pop_back();
        const Node &n = nodes[it.node];
        for (int mi : n.meshes) {
            const Box &b = meshes[mi].bounds;
            if (b.empty())
                continue;
            for (int c = 0; c < 8; c++)
                box.extend(transformPoint(it.m, {c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y,
                                                c & 4 ? b.max.z : b.min.z}));
        }
        for (int c : n.children)
            if (c > 0 && c < (int)nodes.size())
                stack.push_back({c, it.m * nodes[c].local});
    }
    return box;
}

static std::vector<std::string> &paths() {
    static std::vector<std::string> p;
    return p;
}

void addSearchPath(const std::string &folder) {
    if (folder.empty())
        return;
    std::string f = folder;
    if (f.back() != '/' && f.back() != '\\')
        f += '/';
    paths().push_back(f);
}

const std::vector<std::string> &searchPaths() { return paths(); }

static bool fileExists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f)
        return false;
    std::fclose(f);
    return true;
}

std::string findAsset(const std::string &dir, const std::string &name) {
    if (name.empty())
        return {};
    if (name[0] == '/' || name[0] == '\\') {
        if (fileExists(name))
            return name;
    } else {
        if (fileExists(dir + name))
            return dir + name;
        for (const std::string &p : paths())
            if (fileExists(p + name))
                return p + name;
    }
    size_t s = name.find_last_of("/\\");
    std::string base = s == std::string::npos ? name : name.substr(s + 1);
    for (const std::string &p : {dir + base, dir + "../textures/" + base, dir + "textures/" + base, base})
        if (fileExists(p))
            return p;
    for (const std::string &p : paths())
        if (fileExists(p + base))
            return p + base;
    return {};
}

static std::string extensionOf(const std::string &path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return e;
}

std::vector<std::string> modelExtensions() { return {"obj", "d", "ydl", "defs", "bin", "pfb", "dwb", "gset", "flt", "gltf", "glb"}; }

std::shared_ptr<Model> loadModel(const std::string &path, std::string *error) {
    std::string ext = extensionOf(path);
    if (ext == "obj")
        return loadOBJ(path, error);
    if (ext == "d")
        return loadGObj(path, error);
    if (ext == "ydl" || ext == "defs")
        return loadYAODL(path, error);
    if (ext == "bin")
        return loadFlipBin(path, error);
    if (ext == "pfb")
        return loadPFB(path, error);
    if (ext == "dwb")
        return loadDWB(path, error);
    if (ext == "gset")
        return loadGSet(path, error);
    if (ext == "flt")
        return loadFLT(path, error);
    if (ext == "gltf" || ext == "glb")
        return loadGLTF(path, error);
    if (error)
        *error = "unknown model format: " + path;
    return nullptr;
}

// --- images ----------------------------------------------------------------

static bool readFile(const std::string &path, std::vector<uint8_t> &data) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    data.resize(size > 0 ? (size_t)size : 0);
    bool ok = size >= 0 && std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// the SGI image format of IRIX: verbatim or RLE, 1 or 2 bytes per channel,
// rows from the bottom like OpenGL
static bool loadSGI(const std::vector<uint8_t> &d, Image &img, std::string *error) {
    if (d.size() < 512 || be16(&d[0]) != 474) {
        if (error)
            *error = "not an SGI image";
        return false;
    }
    int rle = d[2], bpc = d[3];
    int w = (int)be16(&d[6]), h = (int)be16(&d[8]), zs = (int)be16(&d[10]);
    if (be16(&d[4]) < 3)
        h = 1;
    if (be16(&d[4]) < 2)
        zs = 1;
    if (w <= 0 || h <= 0 || zs <= 0 || zs > 4 || (bpc != 1 && bpc != 2)) {
        if (error)
            *error = "unsupported SGI image";
        return false;
    }
    uint32_t pixmax = be32(&d[16]);
    if (bpc == 1 || pixmax == 0)
        pixmax = bpc == 1 ? 255 : 65535;
    img.width = w;
    img.height = h;
    img.channels = zs;
    img.pixels.assign((size_t)w * h * zs, 0);
    std::vector<unsigned> row(w);
    auto store = [&](int y, int z) {
        for (int x = 0; x < w; x++) {
            unsigned v = row[x] * 255u / pixmax;
            img.pixels[((size_t)y * w + x) * zs + z] = (uint8_t)std::min(v, 255u);
        }
    };
    if (!rle) {
        size_t need = 512 + (size_t)w * h * zs * bpc;
        if (d.size() < need) {
            if (error)
                *error = "truncated SGI image";
            return false;
        }
        for (int z = 0; z < zs; z++)
            for (int y = 0; y < h; y++) {
                const uint8_t *p = &d[512 + (((size_t)z * h + y) * w) * bpc];
                for (int x = 0; x < w; x++)
                    row[x] = bpc == 1 ? p[x] : be16(p + 2 * x);
                store(y, z);
            }
        return true;
    }
    size_t tables = 512 + (size_t)h * zs * 8;
    if (d.size() < tables) {
        if (error)
            *error = "truncated SGI image";
        return false;
    }
    for (int z = 0; z < zs; z++)
        for (int y = 0; y < h; y++) {
            size_t k = (size_t)z * h + y;
            size_t off = be32(&d[512 + k * 4]);
            size_t len = be32(&d[512 + (size_t)h * zs * 4 + k * 4]);
            if (off + len > d.size()) {
                if (error)
                    *error = "bad SGI RLE table";
                return false;
            }
            const uint8_t *p = &d[off], *end = p + len;
            int x = 0;
            auto next = [&]() -> int {
                if (p + bpc > end)
                    return -1;
                int v = bpc == 1 ? *p : (int)be16(p);
                p += bpc;
                return v;
            };
            while (x < w) {
                int c = next();
                if (c < 0)
                    break;
                int count = c & 0x7f;
                if (!count)
                    break;
                if (c & 0x80) {
                    for (; count > 0 && x < w; count--) {
                        int v = next();
                        if (v < 0)
                            break;
                        row[x++] = (unsigned)v;
                    }
                } else {
                    int v = next();
                    if (v < 0)
                        break;
                    for (; count > 0 && x < w; count--)
                        row[x++] = (unsigned)v;
                }
            }
            for (; x < w; x++)
                row[x] = 0;
            store(y, z);
        }
    return true;
}

// binary PPM (P6) and PGM (P5), 8 bits; their rows are from the top
static bool loadPNM(const std::vector<uint8_t> &d, Image &img, std::string *error) {
    size_t pos = 2;
    int values[3], n = 0;
    while (n < 3 && pos < d.size()) {
        if (d[pos] == '#') {
            while (pos < d.size() && d[pos] != '\n')
                pos++;
        } else if (std::isdigit(d[pos])) {
            int v = 0;
            while (pos < d.size() && std::isdigit(d[pos]))
                v = v * 10 + (d[pos++] - '0');
            values[n++] = v;
            continue;
        }
        pos++;
    }
    int channels = d[1] == '6' ? 3 : 1;
    if (n < 3 || values[2] != 255 || values[0] <= 0 || values[1] <= 0 ||
        pos + (size_t)values[0] * values[1] * channels > d.size()) {
        if (error)
            *error = "unsupported PNM image";
        return false;
    }
    img.width = values[0];
    img.height = values[1];
    img.channels = channels;
    img.pixels.resize((size_t)img.width * img.height * channels);
    size_t rowBytes = (size_t)img.width * channels;
    for (int y = 0; y < img.height; y++)
        std::memcpy(&img.pixels[(size_t)(img.height - 1 - y) * rowBytes], &d[pos + (size_t)y * rowBytes], rowBytes);
    return true;
}

// the scrambled 16-bit images of perfly.face (.5551E): "SEn", the count of
// inserted bytes, the width, the height (big-endian ints), then the bytes
// xor'ed with 13 plus their position, runs of junk bytes inserted between
// runs of image bytes, the image written from its last byte back to its
// first; the pixels are RGBA 5551
static void numbers5551E(unsigned &s, unsigned &count, unsigned &skip) {
    const unsigned X = 10733, Y = 951;
    count = ((X >> s) + (Y << s)) % 0x7fffffffu;
    skip = ((X << s) + (Y >> s)) % 37;
    s = (s + 5) % 7;
}

static bool load5551E(const std::vector<uint8_t> &in, Image &img, std::string *error) {
    if (in.size() < 15 || std::memcmp(in.data(), "SEn", 3) != 0) {
        if (error)
            *error = "not a 5551E image";
        return false;
    }
    int w = (int)be32(&in[7]), h = (int)be32(&in[11]);
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        if (error)
            *error = "bad 5551E size";
        return false;
    }
    std::vector<uint8_t> out((size_t)w * h * 2, 0);
    long pos = (long)out.size() - 1;
    unsigned s = 2, count, skip;
    numbers5551E(s, count, skip);
    for (size_t i = 0; i + 15 < in.size() && pos >= 0; i++) {
        uint8_t b = (uint8_t)(in[15 + i] ^ ((13 + i) & 0xff));
        if (count) {
            count--;
            out[pos--] = b;
        } else if (skip) {
            skip--;
        } else {
            numbers5551E(s, count, skip);
            out[pos--] = b;
        }
    }
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.resize((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        unsigned v = (unsigned)out[2 * i] << 8 | out[2 * i + 1];
        img.pixels[4 * i] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
        img.pixels[4 * i + 1] = (uint8_t)(((v >> 6) & 31) * 255 / 31);
        img.pixels[4 * i + 2] = (uint8_t)(((v >> 1) & 31) * 255 / 31);
        img.pixels[4 * i + 3] = v & 1 ? 255 : 0;
    }
    return true;
}

bool decodeImageStb(const uint8_t *data, size_t size, Image &img, std::string *error);

bool decodeImage(const uint8_t *data, size_t size, Image &img, std::string *error) {
    std::vector<uint8_t> d(data, data + size);
    bool ok;
    if (size >= 2 && be16(data) == 474)
        ok = loadSGI(d, img, error);
    else if (size >= 2 && data[0] == 'P' && (data[1] == '6' || data[1] == '5'))
        ok = loadPNM(d, img, error);
    else if (size >= 3 && std::memcmp(data, "SEn", 3) == 0)
        ok = load5551E(d, img, error);
    else
        ok = decodeImageStb(data, size, img, error);
    return ok;
}

bool loadImage(const std::string &path, Image &img, std::string *error) {
    std::vector<uint8_t> data;
    if (!readFile(path, data)) {
        if (error)
            *error = "cannot read " + path;
        return false;
    }
    img = Image{};
    img.name = path;
    bool ok = decodeImage(data.data(), data.size(), img, error);
    if (!ok && error)
        *error = path + ": " + *error;
    return ok;
}

} // namespace r4::assets
