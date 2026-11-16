#include "r4/assets/model.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace r4::assets {

void Mesh::computeBounds() {
    bounds = Box{};
    for (const Vertex &v : vertices)
        bounds.extend(v.position);
}

void Mesh::computeNormals() {
    for (Vertex &v : vertices)
        v.normal = {0, 0, 0};
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Vertex &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
        // weighted by the area: the cross product is not normalized
        Vec3 n = cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (Vertex &v : vertices)
        v.normal = normalize(v.normal);
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
                box.extend(it.m.transformPoint({c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y,
                                                c & 4 ? b.max.z : b.min.z}));
        }
        for (int c : n.children)
            if (c > 0 && c < (int)nodes.size())
                stack.push_back({c, it.m * nodes[c].local});
    }
    return box;
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

std::vector<std::string> modelExtensions() { return {"obj"}; }

std::shared_ptr<Model> loadModel(const std::string &path, std::string *error) {
    std::string ext = extensionOf(path);
    if (ext == "obj")
        return loadOBJ(path, error);
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

bool loadImage(const std::string &path, Image &img, std::string *error) {
    std::vector<uint8_t> data;
    if (!readFile(path, data)) {
        if (error)
            *error = "cannot read " + path;
        return false;
    }
    img = Image{};
    img.name = path;
    bool ok;
    if (data.size() >= 2 && be16(&data[0]) == 474)
        ok = loadSGI(data, img, error);
    else if (data.size() >= 2 && data[0] == 'P' && (data[1] == '6' || data[1] == '5'))
        ok = loadPNM(data, img, error);
    else {
        if (error)
            *error = "unknown image format: " + path;
        ok = false;
    }
    if (!ok && error)
        *error = path + ": " + *error;
    return ok;
}

} // namespace r4::assets
