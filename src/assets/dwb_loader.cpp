// dwb_loader.cpp - the Designer's Workbench files (.dwb, Coryphaeus) of the
// F-18 demo. A port of the loader of legacy_opengl_demo (src/f18/dwb.c),
// rebuilt from pfdLoad_dwb of the IRIX demo.
//
// Big-endian records: a 16-bit opcode, a 16-bit length, the data.
//   0x1771 header    +0x00 units, +0x04 version, +0x86 up axis (2: Y up)
//   0x1772 group     +0x34 flags: bit 26 layer (decals on the first child),
//                    bit 24 sequence (animation frames, +0x80 seconds each)
//   0x1773 LOD       +0x08 switch out, +0x0c switch in, +0x10 center
//   0x1775 polygon   +0x04 vertices, +0x10 texture, +0x2c color 0xAABBGGRR,
//                    +0x36 material
//   0x177a, 0x177c   vertices, 44 bytes: index, x y z, normal, s t, 2 floats
//   0x1798 name, 0x179c texture file name, 0x17ca push, 0x17cb pop
//   0x17ea material  +0x04 diffuse, +0x10 ambient, +0x1c shininess,
//                    +0x20 specular, +0x2c emission, +0x38 alpha
//   0x17eb texture
// The textures of the demo were encrypted (SGI images with 4 at 0x6c): their
// pixels are xor'ed with a table of 1633 seeds, decrypted here (when the
// image is not already in clear).
#include "r4/axes.h"
#include "r4/assets/model.h"

#include <cstdio>
#include <cstring>
#include <map>

namespace r4::assets {
namespace {

enum : unsigned {
    OP_HEADER = 0x1771,
    OP_GROUP = 0x1772,
    OP_LOD = 0x1773,
    OP_POLYGON = 0x1775,
    OP_VERTICES = 0x177a,
    OP_VERTICES2 = 0x177c,
    OP_NAME = 0x1798,
    OP_TEXNAME = 0x179c,
    OP_PUSH = 0x17ca,
    OP_POP = 0x17cb,
    OP_MATERIAL = 0x17ea,
    OP_TEXTURE = 0x17eb
};
constexpr uint32_t kLayer = 0x04000000u;
constexpr uint32_t kSequence = 0x01000000u;
constexpr int kVertexSize = 44;

unsigned be16(const unsigned char *p) {
    return (unsigned)p[0] << 8 | p[1];
}
int sbe16(const unsigned char *p) {
    return (short)be16(p);
}
uint32_t be32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
float bef(const unsigned char *p) {
    uint32_t u = be32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

bool readHeader(const std::string &path, unsigned char h[112]) {
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    bool ok = std::fread(h, 1, 112, fp) == 112;
    std::fclose(fp);
    return ok;
}

bool isEncrypted(const std::string &path) {
    unsigned char h[112];
    return readHeader(path, h) && be16(h) == 474 && h[2] == 0 && be32(h + 0x6c) == 4;
}

// ScrambleImageToImage of the demo: xor with the seeds, the pixel i of a row
// for the channels 0 and 2, the pixel width - 1 - i for the channels 1 and 3
void unscramble(Image &img) {
    static unsigned char seed[0x661];
    seed[0] = 0x67;
    for (int i = 1; i < 0x661; i++) {
        seed[i] = (unsigned char)(seed[i - 1] * 103);
    }
    int w = img.width;
    int zs = img.channels;
    int n = 0;
    for (int y = 0; y < img.height; y++) {
        unsigned char *row = &img.pixels[(size_t)y * w * zs];
        for (int x = 0; x < w; x++) {
            unsigned char s = seed[n];
            unsigned char *a = row + (size_t)x * zs;
            unsigned char *b = row + (size_t)(w - 1 - x) * zs;
            a[0] ^= s;
            if (zs == 2) {
                b[1] ^= s;
            } else if (zs >= 3) {
                b[1] ^= s;
                a[2] ^= s;
                if (zs == 4) {
                    b[3] ^= s;
                }
            }
            n = (n + 1) % 0x661;
        }
    }
}

// the sum of the differences between neighbor pixels: a decrypted image is
// smooth, a still encrypted one is noise
double roughness(const Image &img) {
    double r = 0;
    int zs = img.channels;
    for (int y = 0; y < img.height; y += 4) {
        for (int x = 1; x < img.width; x++) {
            for (int c = 0; c < zs; c++) {
                int a = img.pixels[((size_t)y * img.width + x) * zs + c];
                int b = img.pixels[((size_t)y * img.width + x - 1) * zs + c];
                r += a > b ? a - b : b - a;
            }
        }
    }
    return r;
}

// the encrypted images are decrypted; some copies of the data carry the
// mark of encryption but were already decrypted: the smoother one is kept
void decryptIfNeeded(Image &img) {
    Image dec = img;
    unscramble(dec);
    if (roughness(dec) < roughness(img)) {
        img = std::move(dec);
    }
}

enum Units {
    UNITS_METERS = 0,
    UNITS_FEET = 1,
    UNITS_INCHES = 2,
    UNITS_CENTIMETERS = 3,
    UNITS_NAUTICAL_MILES = 4,
    UNITS_KILOMETERS = 5,
    UNITS_MILES = 6
};

float unitsScale(int units) {
    switch (units) {
    case UNITS_FEET:
        return 0.3048f;
    case UNITS_INCHES:
        return 0.0254f;
    case UNITS_CENTIMETERS:
        return 0.01f;
    case UNITS_NAUTICAL_MILES:
        return 1852.0f;
    case UNITS_KILOMETERS:
        return 1000.0f;
    case UNITS_MILES:
        return 1609.344f;
    default:
        return 1.0f;
    }
}

struct Level {
    int node = -1;                                           // where the children go
    int geode = -1;                                          // the node of the polygons
    std::map<std::tuple<int, int, bool, bool>, int> batches; // texture, material, transparent, decal
    bool layer = false;
    int layerPolys = 0;
    bool polygon = false;
};

struct Loader {
    Model &model;
    std::string dir;
    float scale = 1;
    std::vector<int> tex; // image indices, -1: none
    struct Mtl {
        Vec3 diffuse;
        Vec3 ambient;
        Vec3 specular;
        Vec3 emission;
        float shininess = 0;
        float alpha = 1;
    };
    std::vector<Mtl> mtl;
    std::map<std::tuple<int, int, bool, bool>, int> materials;
    std::map<std::string, int> imageCache;
    int polyMesh = -1;
    uint32_t polyColor = 0;
    int polyVerts = 0;

    explicit Loader(Model &m) : model(m) {
    }

    int newNode(const std::string &name, int parent) {
        int i = (int)model.nodes.size();
        Node n;
        n.name = name;
        model.nodes.push_back(n);
        if (parent >= 0) {
            model.nodes[parent].children.push_back(i);
        }
        return i;
    }

    int loadTexture(const std::string &name) {
        size_t slash = name.find_last_of("/\\");
        std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
        std::string lastDir;
        if (slash != std::string::npos) {
            size_t s2 = name.find_last_of("/\\", slash - 1);
            lastDir = name.substr(s2 == std::string::npos ? 0 : s2 + 1, slash - (s2 == std::string::npos ? 0 : s2 + 1));
        }
        static const char *exts[] = {"", ".rgb", ".rgba", ".int", ".inta", ".bw"};
        std::vector<std::string> dirs = {dir, dir + "../textures/", dir + "textures/"};
        if (!lastDir.empty() && lastDir != ".") {
            dirs.push_back(dir + "../textures/" + lastDir + "/");
        }
        for (const std::string &d : dirs) {
            for (const char *e : exts) {
                std::string path = d + base + e;
                FILE *fp = std::fopen(path.c_str(), "rb");
                if (!fp) {
                    continue;
                }
                std::fclose(fp);
                auto it = imageCache.find(path);
                if (it != imageCache.end()) {
                    return it->second;
                }
                Image img;
                std::string err;
                int index = -1;
                if (loadImage(path, img, &err)) {
                    if (isEncrypted(path)) {
                        decryptIfNeeded(img);
                    }
                    index = (int)model.images.size();
                    model.images.push_back(std::move(img));
                }
                imageCache[path] = index;
                return index;
            }
        }
        std::fprintf(stderr, "r4: dwb: texture %s not found\n", name.c_str());
        return -1;
    }

    bool hasAlpha(int t) const {
        return t >= 0 && t < (int)tex.size() && tex[t] >= 0 && (model.images[tex[t]].channels == 2 || model.images[tex[t]].channels == 4);
    }

    int material(int t, int m, bool transp, bool decal) {
        auto key = std::make_tuple(t, m, transp, decal);
        auto it = materials.find(key);
        if (it != materials.end()) {
            return it->second;
        }
        Material mat;
        // the colors of the polygons drive the ambient and diffuse
        mat.vertexColors = true;
        mat.diffuse = {1, 1, 1, 1};
        mat.ambient = {1, 1, 1};
        if (m >= 0) {
            const Mtl &p = mtl[m];
            mat.name = "material " + std::to_string(m);
            mat.specular = p.specular;
            mat.emissive = p.emission;
            mat.shininess = p.shininess;
            mat.diffuse.w = p.alpha;
        } else {
            mat.name = "default";
        }
        mat.doubleSided = true;
        mat.decal = decal;
        if (t >= 0 && t < (int)tex.size()) {
            mat.diffuseImage = tex[t];
        }
        if (transp) {
            mat.alpha = AlphaMode::BLEND;
        }
        int index = (int)model.materials.size();
        model.materials.push_back(mat);
        materials[key] = index;
        return index;
    }

    int batch(Level &lv, int t, int m, bool transp) {
        bool decal = false;
        if (lv.layer) {
            // each polygon in its own geode, the ones after the first are decals
            decal = lv.layerPolys++ > 0;
            lv.geode = newNode("layer polygon", lv.node);
        } else {
            auto key = std::make_tuple(t, m, transp, false);
            auto it = lv.batches.find(key);
            if (it != lv.batches.end()) {
                return it->second;
            }
            if (lv.geode < 0) {
                lv.geode = newNode("polygons", lv.node);
            }
        }
        Mesh mesh;
        mesh.material = material(t, m, transp, decal);
        int index = (int)model.meshes.size();
        model.meshes.push_back(std::move(mesh));
        model.nodes[lv.geode].meshes.push_back(index);
        if (!lv.layer) {
            lv.batches[std::make_tuple(t, m, transp, false)] = index;
        }
        return index;
    }

    void addVertices(const unsigned char *b, int len) {
        int n = len / kVertexSize;
        if (polyMesh < 0 || n <= 0) {
            return;
        }
        Mesh &mesh = model.meshes[polyMesh];
        polyMesh = -1;
        if (polyVerts > 0 && polyVerts < n) {
            n = polyVerts;
        }
        if (n < 3) {
            return;
        }
        uint32_t base = (uint32_t)mesh.vertices.size();
        for (int i = 0; i < n; i++) {
            const unsigned char *v = b + i * kVertexSize;
            Vertex out;
            out.position = Vec3{bef(v + 4), bef(v + 8), bef(v + 12)} * scale;
            out.normal = {bef(v + 16), bef(v + 20), bef(v + 24)};
            out.uv = {bef(v + 28), bef(v + 32)};
            out.color = polyColor; // 0xAABBGGRR: R in the low byte
            mesh.vertices.push_back(out);
        }
        for (int i = 1; i + 1 < n; i++) {
            mesh.indices.insert(mesh.indices.end(), {base, base + i, base + i + 1});
        }
    }
};

} // namespace

std::shared_ptr<Model> loadDWB(const std::string &path, std::string *error) {
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        if (error) {
            *error = "cannot read " + path;
        }
        return nullptr;
    }
    std::vector<unsigned char> data;
    unsigned char chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, fp)) > 0) {
        data.insert(data.end(), chunk, chunk + got);
    }
    std::fclose(fp);
    if (data.size() < 8 || be16(data.data()) != OP_HEADER) {
        if (error) {
            *error = path + ": not a Designer's Workbench file";
        }
        return nullptr;
    }
    auto model = std::make_shared<Model>();
    model->source = path;
    Loader L(*model);
    size_t slash = path.find_last_of("/\\");
    L.dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    // node 0: the root (turned when the file is Y-up), node 1: the content
    L.newNode(path, -1);
    int content = L.newNode("dwb", 0);
    std::vector<Level> stack(1);
    stack[0].node = content;
    Level last;
    bool lastIsPoly = false;
    int lastNode = -1;
    int lastTex = -1;
    bool yUp = false;
    long size = (long)data.size();
    for (long p = 0; p + 4 <= size;) {
        unsigned op = be16(&data[p]);
        int len = (int)be16(&data[p + 2]);
        const unsigned char *b = &data[p + 4];
        if (p + 4 + len > size) {
            break;
        }
        p += 4 + len;
        Level &lv = stack.back();
        switch (op) {
        case OP_HEADER:
            if (len >= 2) {
                L.scale = unitsScale(sbe16(b));
            }
            if (len >= 0x88) {
                yUp = sbe16(b + 0x86) == 2;
            }
            break;
        case OP_GROUP: {
            uint32_t flags = len >= 0x38 ? be32(b + 0x34) : 0;
            int n = L.newNode("group", lv.node);
            if (flags & kSequence) {
                // an animation: its first frame
                model->nodes[n].kind = NodeKind::SWITCH;
                model->nodes[n].switchValue = 0;
            }
            lastNode = n;
            last = Level();
            last.node = n;
            last.layer = (flags & kLayer) != 0;
            lastIsPoly = false;
            break;
        }
        case OP_LOD: {
            int lod = L.newNode("lod", lv.node);
            Node &ln = model->nodes[lod];
            ln.kind = NodeKind::LOD;
            if (len >= 0x1c) {
                ln.lodRanges = {bef(b + 0x0c) * L.scale, bef(b + 0x08) * L.scale};
                ln.lodCenter = Vec3{bef(b + 0x10), bef(b + 0x14), bef(b + 0x18)} * L.scale;
                ln.hasLodCenter = true;
            } else {
                ln.lodRanges = {0, 1e30f};
            }
            int g = L.newNode("lod child", lod);
            lastNode = lod;
            last = Level();
            last.node = g;
            lastIsPoly = false;
            break;
        }
        case OP_POLYGON: {
            if (len < 0x38) {
                break;
            }
            int t = sbe16(b + 0x10);
            int m = sbe16(b + 0x36);
            uint32_t color = be32(b + 0x2c);
            if (t < 0 || t >= (int)L.tex.size() || L.tex[t] < 0) {
                t = -1;
            }
            if (m < 0 || m >= (int)L.mtl.size()) {
                m = -1;
            }
            bool transp = (color >> 24) < 255 || (m >= 0 && L.mtl[m].alpha < 0.95f) || L.hasAlpha(t);
            // a polygon inside a polygon (a subface) goes with its parent
            size_t d = stack.size() - 1;
            while (d > 0 && stack[d].polygon) {
                d--;
            }
            L.polyMesh = L.batch(stack[d], t, m, transp);
            L.polyColor = color;
            L.polyVerts = sbe16(b + 4);
            lastNode = -1;
            lastIsPoly = true;
            break;
        }
        case OP_VERTICES:
        case OP_VERTICES2:
            L.addVertices(b, len);
            break;
        case OP_NAME:
            if (lastNode >= 0) {
                int k = 0;
                while (k < len && b[k]) {
                    k++;
                }
                if (k) {
                    model->nodes[lastNode].name.assign((const char *)b, k);
                }
            }
            break;
        case OP_MATERIAL:
            if (len >= 0x3c) {
                Loader::Mtl m;
                m.diffuse = {bef(b + 0x04), bef(b + 0x08), bef(b + 0x0c)};
                m.ambient = {bef(b + 0x10), bef(b + 0x14), bef(b + 0x18)};
                m.shininess = bef(b + 0x1c);
                m.specular = {bef(b + 0x20), bef(b + 0x24), bef(b + 0x28)};
                m.emission = {bef(b + 0x2c), bef(b + 0x30), bef(b + 0x34)};
                m.alpha = bef(b + 0x38);
                if (m.alpha <= 0 || m.alpha > 1) {
                    m.alpha = 1;
                }
                L.mtl.push_back(m);
            }
            break;
        case OP_TEXTURE:
            L.tex.push_back(-1);
            lastTex = (int)L.tex.size() - 1;
            break;
        case OP_TEXNAME:
            if (lastTex >= 0 && L.tex[lastTex] < 0) {
                int k = 0;
                while (k < len && b[k]) {
                    k++;
                }
                L.tex[lastTex] = L.loadTexture(std::string((const char *)b, k));
            }
            lastTex = -1;
            break;
        case OP_PUSH: {
            Level next;
            if (lastIsPoly) {
                next = stack.back();
                next.polygon = true;
            } else if (last.node >= 0) {
                next = last;
            } else {
                next = stack.back();
                next.polygon = false;
            }
            stack.push_back(next);
            last = Level();
            lastNode = -1;
            lastIsPoly = false;
            break;
        }
        case OP_POP:
            if (stack.size() > 1) {
                Level top = stack.back();
                stack.pop_back();
                // the batches of a level stay with its geode
                if (!top.polygon && top.node == stack.back().node) {
                    stack.back().batches = top.batches;
                    stack.back().geode = top.geode;
                }
            }
            last = Level();
            lastNode = -1;
            lastIsPoly = false;
            break;
        default:
            break;
        }
    }
    // the empty meshes go
    for (Node &n : model->nodes) {
        std::vector<int> keep;
        for (int mi : n.meshes) {
            if (!model->meshes[mi].indices.empty()) {
                keep.push_back(mi);
            }
        }
        n.meshes = keep;
    }
    bool any = false;
    for (Mesh &m : model->meshes) {
        m.computeBounds();
        any = any || !m.indices.empty();
    }
    if (!any) {
        if (error) {
            *error = path + ": nothing to draw";
        }
        return nullptr;
    }
    model->nodes[0].children = {content};
    if (yUp) {
        model->nodes[0].local = axes::fromYUpMatrix();
    }
    return model;
}

} // namespace r4::assets
