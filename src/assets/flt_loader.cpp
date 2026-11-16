// flt_loader.cpp - OpenFlight (.flt): the 14.2 format and the older ones of
// versions 11 and 12 (integer coordinates, vertices inside the faces). A
// port of the loader of legacy_opengl_demo
// (src/porting/performer/pfd_flt.c).
//
// Read: the header (units), the color, material and texture palettes, the
// vertex palette and the vertex lists (14.2), the old vertices (7, 8, 9),
// the groups, objects, levels of detail, matrices, external references
// (loaded once and shared), the faces with their subfaces (decals) and the
// billboard faces. The faces are gathered into meshes by texture, material
// and mode under each node. A face is lit when it has a material; its color
// is then the face color times the colors of the material. The texture
// attribute files (name.attr) give the wrap of the textures. OpenFlight is
// Z-up, as R4nkine.
#include "r4/assets/model.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace r4::assets {
namespace {

enum {
    OP_HEADER = 1, OP_GROUP = 2, OP_OLD_LOD = 3, OP_OBJECT = 4, OP_FACE = 5, OP_OLD_VERTEX = 7,
    OP_OLD_VERTEX_COLOR = 8, OP_OLD_VERTEX_NORMAL = 9, OP_PUSH = 10, OP_POP = 11, OP_PUSH_SUBFACE = 19,
    OP_POP_SUBFACE = 20, OP_COLOR_PALETTE = 32, OP_MATRIX = 49, OP_EXTREF = 63, OP_TEXTURE = 64,
    OP_MATERIAL_TABLE = 66, OP_VERTEX_PALETTE = 67, OP_VERTEX_C = 68, OP_VERTEX_CN = 69, OP_VERTEX_CNT = 70,
    OP_VERTEX_CT = 71, OP_VERTEX_LIST = 72, OP_LOD = 73, OP_MATERIAL = 113
};

unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
int sbe16(const unsigned char *p) { return (short)be16(p); }
uint32_t be32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
int32_t sbe32(const unsigned char *p) { return (int32_t)be32(p); }
float bef(const unsigned char *p) {
    uint32_t u = be32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
double bed(const unsigned char *p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; i++)
        u = u << 8 | p[i];
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}

struct FVertex {
    double xyz[3] = {0, 0, 0};
    Vec3 nrm;
    Vec2 uv;
    Vec4 color{1, 1, 1, 1};
    bool hasNrm = false, hasUV = false, hasColor = false;
};

struct FMaterial {
    Vec3 amb, dif, spec, emis;
    float shininess = 0, alpha = 1;
};

struct Face {
    Vec4 color{1, 1, 1, 1};
    int texture = -1, material = -1, drawtype = 0, templ = 0, lightmode = 0, subface = 0;
    std::vector<FVertex> v;
};

struct Batch {
    int texture, material, drawtype;
    bool transp, decal, alphatex, vcolors;
    Vec4 color;
    Mesh mesh;
};

struct File;

struct Shared {
    Model &model;
    std::map<std::string, int> refs;   // external file -> node index
    std::map<std::string, int> images; // image path -> index
    int depth = 0;
    explicit Shared(Model &m) : model(m) {}
};

// a file named in a database: as given, then by its base name (the
// absolute paths of the machines where the databases were made are not
// there), next to the file and on the search paths
std::string findFile(const std::string &dir, const std::string &name) { return findAsset(dir, name); }

int loadFile(Shared &sh, const std::string &path, std::string *error);

struct File {
    Shared &sh;
    Model &model;
    std::string dir;
    int version = 0;
    double unit = 1;
    std::vector<Vec4> palette;
    bool oldPalette = false;
    std::vector<FMaterial> materials = std::vector<FMaterial>(256);
    int nmaterials = 0;
    std::map<int, int> textures; // index -> image
    std::map<int, bool> alphatex;
    std::vector<int> vindex;
    long vpalStart = -1, vpalLen = 0;
    std::vector<FVertex> verts;
    // the batches of each node
    std::map<int, std::vector<Batch>> batches;
    std::map<int, std::vector<std::pair<Vec3, Batch>>> billboards;

    File(Shared &s) : sh(s), model(s.model) {}

    Vec4 indexColor(unsigned c) const {
        int base;
        float in;
        if (oldPalette && c >= 4096) {
            base = 32 + (int)(c - 4096);
            in = 1;
        } else {
            base = (int)(c / 128);
            in = (c % 128) / 127.0f;
        }
        if (base >= (int)palette.size())
            return {1, 1, 1, 1};
        const Vec4 &p = palette[base];
        return {p.x * in, p.y * in, p.z * in, 1};
    }
    static Vec4 packed(const unsigned char *p) { return {p[3] / 255.0f, p[2] / 255.0f, p[1] / 255.0f, 1}; }

    void paletteVertex(int op, const unsigned char *r) {
        FVertex v;
        int flags = sbe16(r + 6);
        for (int k = 0; k < 3; k++)
            v.xyz[k] = bed(r + 8 + 8 * k);
        if (op == OP_VERTEX_CN || op == OP_VERTEX_CNT) {
            v.nrm = {bef(r + 32), bef(r + 36), bef(r + 40)};
            v.hasNrm = true;
        }
        if (op == OP_VERTEX_CNT) {
            v.uv = {bef(r + 44), bef(r + 48)};
            v.hasUV = true;
        } else if (op == OP_VERTEX_CT) {
            v.uv = {bef(r + 32), bef(r + 36)};
            v.hasUV = true;
        }
        // bit 2: no color, bit 3: packed color (the bits from the high one)
        if (!(flags & 0x2000)) {
            int pc = op == OP_VERTEX_C ? 32 : op == OP_VERTEX_CN ? 44 : op == OP_VERTEX_CNT ? 52 : 40;
            v.color = (flags & 0x1000) ? packed(r + pc) : indexColor(be16(r + 4));
            v.hasColor = true;
        }
        verts.push_back(v);
    }

    void oldVertex(Face &fc, int op, const unsigned char *r, int len) {
        FVertex v;
        for (int k = 0; k < 3; k++)
            v.xyz[k] = sbe32(r + 4 + 4 * k) * unit;
        if (op == OP_OLD_VERTEX && len >= 24) {
            v.uv = {bef(r + 16), bef(r + 20)};
            v.hasUV = true;
        } else if (op == OP_OLD_VERTEX_NORMAL) {
            v.nrm = Vec3{(float)(sbe32(r + 16) / 1073741824.0), (float)(sbe32(r + 20) / 1073741824.0),
                         (float)(sbe32(r + 24) / 1073741824.0)};
            v.hasNrm = true;
            if (len >= 40) {
                v.uv = {bef(r + 32), bef(r + 36)};
                v.hasUV = true;
            }
        } else if (op == OP_OLD_VERTEX_COLOR && len >= 28) {
            v.uv = {bef(r + 20), bef(r + 24)};
            v.hasUV = true;
        }
        fc.v.push_back(v);
    }

    static uint32_t pack(Vec4 c) {
        auto b = [](float x) { return (uint32_t)(std::fmin(std::fmax(x, 0.0f), 1.0f) * 255 + 0.5f); };
        return b(c.x) | b(c.y) << 8 | b(c.z) << 16 | b(c.w) << 24;
    }

    void addFace(int owner, Face &fc) {
        int need = fc.drawtype >= 8 ? 1 : fc.drawtype >= 2 ? 2 : 3;
        int n = (int)fc.v.size();
        if (n < need)
            return;
        const FMaterial *m = fc.material >= 0 && fc.material < nmaterials ? &materials[fc.material] : nullptr;
        bool points = fc.drawtype >= 8;
        bool lines = !points && (fc.drawtype == 2 || fc.drawtype == 3 || n < 3);
        bool alpha = fc.texture >= 0 && alphatex.count(fc.texture) && alphatex[fc.texture];
        bool transp = fc.color.w < 1 || fc.templ == 1 || fc.templ == 2 || fc.templ == 4 || (m && m->alpha < 1);
        bool billboard = fc.templ == 2 || fc.templ == 4;
        bool decal = fc.subface > 0;
        int drawtype = points ? 8 : lines ? 2 : fc.drawtype;
        // the normal of the face (Newell) and its center
        Vec3 fn;
        double center[3] = {0, 0, 0};
        for (int i = 0; i < n; i++) {
            const double *a = fc.v[i].xyz, *c = fc.v[(i + 1) % n].xyz;
            fn.x += (float)((a[1] - c[1]) * (a[2] + c[2]));
            fn.y += (float)((a[2] - c[2]) * (a[0] + c[0]));
            fn.z += (float)((a[0] - c[0]) * (a[1] + c[1]));
            for (int k = 0; k < 3; k++)
                center[k] += a[k] / n;
        }
        fn = normalize(fn);
        // a lit face with vertex colors uses them as its diffuse color
        bool vcolors = m && (fc.lightmode == 1 || fc.lightmode == 3) && fc.v[0].hasColor;
        Batch local{fc.texture, m ? fc.material : -1, drawtype, transp, decal, alpha, vcolors, fc.color, Mesh()};
        Batch *b = nullptr;
        if (billboard) {
            b = &local;
        } else {
            for (Batch &c : batches[owner])
                if (c.texture == fc.texture && c.material == local.material && c.drawtype == drawtype &&
                    c.transp == transp && c.decal == decal && c.alphatex == alpha && c.vcolors == vcolors &&
                    (!m || vcolors ||
                     (c.color.x == fc.color.x && c.color.y == fc.color.y && c.color.z == fc.color.z &&
                      c.color.w == fc.color.w))) {
                    b = &c;
                    break;
                }
            if (!b) {
                batches[owner].push_back(local);
                b = &batches[owner].back();
            }
        }
        Mesh &mesh = b->mesh;
        mesh.primitive = points ? Primitive::Points : lines ? Primitive::Lines : Primitive::Triangles;
        double off[3] = {0, 0, 0};
        if (billboard)
            for (int k = 0; k < 3; k++)
                off[k] = center[k];
        auto vertex = [&](const FVertex &v, Vec3 nrm, Vec4 col) {
            Vertex o;
            o.position = {(float)(v.xyz[0] - off[0]), (float)(v.xyz[1] - off[1]), (float)(v.xyz[2] - off[2])};
            o.normal = nrm;
            o.uv = v.uv;
            o.color = pack(col);
            mesh.indices.push_back((uint32_t)mesh.vertices.size());
            mesh.vertices.push_back(o);
        };
        if (lines) {
            for (int i = 0; i < n; i++) {
                if (fc.drawtype == 3 && i == n - 1)
                    break;
                vertex(fc.v[i], fn, fc.color);
                vertex(fc.v[(i + 1) % n], fn, fc.color);
            }
        } else {
            int nt = points ? n : 3 * (n - 2);
            for (int t = 0; t < nt; t++) {
                int vi = points ? t : t % 3 == 0 ? 0 : t / 3 + t % 3;
                const FVertex &v = fc.v[vi];
                Vec3 nrm = (fc.lightmode >= 2 || version < 13) && v.hasNrm ? v.nrm : fn;
                Vec4 col = (fc.lightmode == 1 || fc.lightmode == 3) && v.hasColor ? v.color : fc.color;
                if (vcolors)
                    col = {col.x * m->dif.x, col.y * m->dif.y, col.z * m->dif.z, col.w};
                vertex(v, nrm, col);
            }
        }
        if (billboard)
            billboards[owner].push_back({Vec3{(float)center[0], (float)center[1], (float)center[2]}, local});
    }

    int material(const Batch &b) {
        Material mat;
        const FMaterial *m = b.material >= 0 ? &materials[b.material] : nullptr;
        if (m) {
            mat.name = "material " + std::to_string(b.material);
            mat.shading = Shading::Phong;
            if (b.vcolors) {
                // the vertex colors are the ambient and diffuse colors
                mat.ambient = {1, 1, 1};
                mat.diffuse = {1, 1, 1, m->alpha * b.color.w};
                mat.vertexColors = true;
            } else {
                mat.ambient = {m->amb.x * b.color.x, m->amb.y * b.color.y, m->amb.z * b.color.z};
                mat.diffuse = {m->dif.x * b.color.x, m->dif.y * b.color.y, m->dif.z * b.color.z, m->alpha * b.color.w};
            }
            mat.specular = m->spec;
            mat.emissive = m->emis;
            mat.shininess = m->shininess;
        } else {
            mat.name = "unlit";
            mat.shading = Shading::Unlit;
            mat.diffuse = {1, 1, 1, 1};
            mat.vertexColors = true;
        }
        auto t = textures.find(b.texture);
        if (t != textures.end())
            mat.diffuseImage = t->second;
        mat.doubleSided = !(b.drawtype == 0 && !b.decal);
        mat.decal = b.decal;
        if (b.alphatex) {
            mat.alpha = AlphaMode::Mask;
            mat.alphaCutoff = 0.5f;
        }
        if (b.transp)
            mat.alpha = AlphaMode::Blend;
        int index = (int)model.materials.size();
        model.materials.push_back(mat);
        return index;
    }

    // the meshes of the faces read under each node
    void build() {
        std::map<std::tuple<int, int, int, bool, bool, bool, bool, float, float, float, float>, int> mats;
        auto materialOf = [&](const Batch &b) {
            auto key = std::make_tuple(b.texture, b.material, b.drawtype == 0 ? 0 : b.drawtype, b.transp, b.decal,
                                       b.alphatex, b.vcolors, b.color.x, b.color.y, b.color.z, b.color.w);
            auto it = mats.find(key);
            if (it != mats.end())
                return it->second;
            return mats[key] = material(b);
        };
        for (auto &[owner, list] : batches)
            for (Batch &b : list) {
                if (b.mesh.indices.empty())
                    continue;
                b.mesh.material = materialOf(b);
                b.mesh.computeBounds();
                model.nodes[owner].meshes.push_back((int)model.meshes.size());
                model.meshes.push_back(std::move(b.mesh));
            }
        // the billboards at their centers (they do not turn yet)
        for (auto &[owner, list] : billboards)
            for (auto &[center, b] : list) {
                b.mesh.material = materialOf(b);
                b.mesh.computeBounds();
                Node n;
                n.name = "billboard";
                n.local = Mat4::translate(center);
                n.meshes.push_back((int)model.meshes.size());
                model.meshes.push_back(std::move(b.mesh));
                int ni = (int)model.nodes.size();
                model.nodes.push_back(n);
                model.nodes[owner].children.push_back(ni);
            }
    }

    // the texture attribute file: the wrap of u at 40 and of v at 44
    // (0 repeat, 1 clamp, 3 the method at 36)
    static void readAttr(Image &img, const std::string &path) {
        FILE *fp = std::fopen((path + ".attr").c_str(), "rb");
        if (!fp)
            return;
        unsigned char a[48];
        if (std::fread(a, 1, sizeof a, fp) == sizeof a) {
            int method = sbe32(a + 36);
            for (int k = 0; k < 2; k++) {
                int w = sbe32(a + 40 + 4 * k);
                if (w == 3)
                    w = method;
                (k ? img.clampT : img.clampS) = w == 1;
            }
        }
        std::fclose(fp);
    }

    void loadTexture(const unsigned char *r, int len) {
        int namelen = version >= 13 ? 200 : 80;
        if (len < namelen + 8)
            return;
        std::string name((const char *)r + 4, (size_t)std::min(namelen, 255));
        name = name.c_str();
        int idx = sbe32(r + 4 + namelen);
        if (idx < 0 || idx >= 1024)
            return;
        std::string path = findFile(dir, name);
        if (path.empty()) {
            std::fprintf(stderr, "r4: flt: texture %s not found\n", name.c_str());
            return;
        }
        auto it = sh.images.find(path);
        int image;
        if (it != sh.images.end()) {
            image = it->second;
        } else {
            Image img;
            std::string err;
            image = -1;
            if (loadImage(path, img, &err)) {
                readAttr(img, path);
                image = (int)model.images.size();
                model.images.push_back(std::move(img));
            } else {
                std::fprintf(stderr, "r4: flt: %s\n", err.c_str());
            }
            sh.images[path] = image;
        }
        if (image < 0)
            return;
        textures[idx] = image;
        size_t dot = name.find_last_of('.');
        std::string ext = dot == std::string::npos ? "" : name.substr(dot);
        int comps = model.images[image].channels;
        alphatex[idx] = comps == 2 || comps == 4 || ext == ".rgba" || ext == ".inta";
    }

    int newNode(const std::string &name, int parent) {
        Node n;
        n.name = name;
        int i = (int)model.nodes.size();
        model.nodes.push_back(n);
        if (parent >= 0)
            model.nodes[parent].children.push_back(i);
        return i;
    }

    // returns the root node, -1 on error
    int read(const std::vector<unsigned char> &data, const std::string &path) {
        size_t sl = path.find_last_of("/\\");
        dir = sl == std::string::npos ? std::string() : path.substr(0, sl + 1);
        int root = newNode(path, -1);
        struct Level {
            int container; // -1: the level of a face
            Face face;
        };
        std::vector<Level> stack{{root, Face()}};
        int last = root, lastParent = -1, lastChild = -1, subface = 0;
        bool lastFace = false;
        Face face;
        long size = (long)data.size();
        for (long pos = 0; pos + 4 <= size;) {
            const unsigned char *r = &data[pos];
            int op = (int)be16(r), len = (int)be16(r + 2);
            if (len < 4 || pos + len > size)
                break;
            int parent = stack.back().container;
            switch (op) {
            case OP_HEADER:
                if (len >= 64) {
                    int mult = sbe16(r + 60);
                    version = sbe32(r + 12);
                    unit = mult > 0 ? mult : mult < 0 ? 1.0 / -mult : 1;
                }
                break;
            case OP_COLOR_PALETTE:
                palette.clear();
                if (version >= 13) {
                    for (int k = 0; k < std::min((len - 132) / 4, 1024); k++)
                        palette.push_back(packed(r + 132 + 4 * k));
                } else {
                    // the old palette: unsigned shorts r, g, b from 0 to 255
                    oldPalette = true;
                    for (int k = 0; k < std::min((len - 4) / 6, 1024); k++)
                        palette.push_back({be16(r + 4 + 6 * k) / 255.0f, be16(r + 6 + 6 * k) / 255.0f,
                                           be16(r + 8 + 6 * k) / 255.0f, 1});
                }
                break;
            case OP_MATERIAL_TABLE:
                nmaterials = std::min((len - 4) / 184, 256);
                for (int k = 0; k < nmaterials; k++) {
                    const unsigned char *e = r + 4 + 184 * k;
                    FMaterial &m = materials[k];
                    m.amb = {bef(e), bef(e + 4), bef(e + 8)};
                    m.dif = {bef(e + 12), bef(e + 16), bef(e + 20)};
                    m.spec = {bef(e + 24), bef(e + 28), bef(e + 32)};
                    m.emis = {bef(e + 36), bef(e + 40), bef(e + 44)};
                    m.shininess = bef(e + 48);
                    m.alpha = bef(e + 52);
                    if (m.alpha <= 0)
                        m.alpha = 1;
                }
                break;
            case OP_MATERIAL:
                if (len >= 84) {
                    int idx = sbe32(r + 4);
                    if (idx >= 0 && idx < 256) {
                        FMaterial &m = materials[idx];
                        m.amb = {bef(r + 24), bef(r + 28), bef(r + 32)};
                        m.dif = {bef(r + 36), bef(r + 40), bef(r + 44)};
                        m.spec = {bef(r + 48), bef(r + 52), bef(r + 56)};
                        m.emis = {bef(r + 60), bef(r + 64), bef(r + 68)};
                        m.shininess = bef(r + 72);
                        m.alpha = bef(r + 76);
                        if (m.alpha <= 0)
                            m.alpha = 1;
                        nmaterials = std::max(nmaterials, idx + 1);
                    }
                }
                break;
            case OP_TEXTURE:
                loadTexture(r, len);
                break;
            case OP_VERTEX_PALETTE:
                vpalStart = pos;
                vpalLen = len >= 8 ? (long)be32(r + 4) : 0;
                vindex.assign(vpalLen > 0 ? (size_t)(vpalLen / 4 + 1) : 0, -1);
                break;
            case OP_VERTEX_C:
            case OP_VERTEX_CN:
            case OP_VERTEX_CNT:
            case OP_VERTEX_CT:
                if (vpalStart >= 0 && !vindex.empty() && pos - vpalStart < vpalLen) {
                    vindex[(pos - vpalStart) / 4] = (int)verts.size();
                    paletteVertex(op, r);
                }
                break;
            case OP_GROUP:
            case OP_OBJECT:
            case OP_LOD:
            case OP_OLD_LOD:
            case OP_EXTREF: {
                if (parent < 0)
                    break;
                std::string name;
                if (len >= 12) {
                    name.assign((const char *)r + 4, 8);
                    name = name.c_str();
                }
                int n = newNode(name, parent), child = n;
                if (op == OP_LOD || op == OP_OLD_LOD) {
                    double in, out;
                    Vec3 c;
                    if (op == OP_LOD && len >= 64) {
                        in = bed(r + 16);
                        out = bed(r + 24);
                        c = {(float)bed(r + 40), (float)bed(r + 48), (float)bed(r + 56)};
                    } else {
                        in = sbe32(r + 12) * unit;
                        out = sbe32(r + 16) * unit;
                        c = {(float)(sbe32(r + 28) * unit), (float)(sbe32(r + 32) * unit),
                             (float)(sbe32(r + 36) * unit)};
                    }
                    Node &ln = model.nodes[n];
                    ln.kind = NodeKind::LOD;
                    ln.lodRanges = {(float)out, (float)in};
                    ln.lodCenter = c;
                    ln.hasLodCenter = true;
                    child = newNode("lod child", n);
                } else if (op == OP_EXTREF) {
                    // "file.flt<node>" names a node: the whole file is used
                    std::string ref((const char *)r + 4, (size_t)std::min(len - 4, 200));
                    ref = ref.c_str();
                    size_t lt = ref.find('<');
                    if (lt != std::string::npos)
                        ref.erase(lt);
                    std::string found = findFile(dir, ref);
                    if (found.empty()) {
                        std::fprintf(stderr, "r4: flt: %s not found\n", ref.c_str());
                    } else {
                        int sub = loadFile(sh, found, nullptr);
                        if (sub >= 0)
                            model.nodes[n].children.push_back(sub);
                    }
                }
                last = n;
                lastParent = parent;
                lastChild = child;
                lastFace = false;
                break;
            }
            case OP_MATRIX:
                if (last >= 0 && lastParent >= 0 && len >= 68) {
                    Mat4 m;
                    float *o = &m.m[0][0];
                    for (int k = 0; k < 16; k++)
                        o[k] = bef(r + 4 + 4 * k);
                    if (version < 13)
                        for (int k = 0; k < 3; k++)
                            m.m[3][k] *= (float)unit;
                    // the matrix applies to the node: it is put above its own
                    model.nodes[last].local = m * model.nodes[last].local;
                }
                break;
            case OP_FACE: {
                face = Face();
                face.subface = subface;
                face.drawtype = len > 18 ? r[18] : 0;
                face.templ = len > 25 ? r[25] : 0;
                face.texture = len >= 30 ? sbe16(r + 28) : -1;
                face.material = len >= 32 ? sbe16(r + 30) : -1;
                face.lightmode = len > 48 && version >= 13 ? r[48] : 0;
                uint32_t flags = len >= 48 && version >= 13 ? be32(r + 44) : 0;
                if (flags & 0x40000000u)
                    face.color = {1, 1, 1, 1};
                else if ((flags & 0x10000000u) && len >= 60)
                    face.color = packed(r + 56);
                else
                    face.color = indexColor(be16(r + 20));
                if (face.texture >= 0 && len > 19 && r[19])
                    face.color = {1, 1, 1, 1};
                face.color.w = 1;
                if (version >= 13 && len >= 42)
                    face.color.w = 1 - be16(r + 40) / 65535.0f;
                lastFace = true;
                break;
            }
            case OP_VERTEX_LIST:
                if (stack.size() > 1 && stack.back().container < 0 && !vindex.empty())
                    for (int k = 4; k + 4 <= len; k += 4) {
                        long off = (long)be32(r + k);
                        int vi = off >= 0 && off < vpalLen ? vindex[off / 4] : -1;
                        if (vi >= 0)
                            stack.back().face.v.push_back(verts[vi]);
                    }
                break;
            case OP_OLD_VERTEX:
            case OP_OLD_VERTEX_COLOR:
            case OP_OLD_VERTEX_NORMAL:
                if (stack.size() > 1 && stack.back().container < 0)
                    oldVertex(stack.back().face, op, r, len);
                break;
            case OP_PUSH:
                if (stack.size() < 256) {
                    if (lastFace) {
                        // the face takes its vertices
                        stack.push_back({-1, face});
                        face = Face();
                    } else {
                        stack.push_back({last >= 0 && lastChild >= 0 ? lastChild : parent, Face()});
                    }
                }
                lastFace = false;
                break;
            case OP_POP:
                if (stack.size() > 1) {
                    if (stack.back().container < 0) {
                        int owner = -1;
                        for (size_t d = stack.size() - 1; d-- > 0 && owner < 0;)
                            owner = stack[d].container;
                        if (owner >= 0)
                            addFace(owner, stack.back().face);
                    }
                    stack.pop_back();
                }
                lastFace = false;
                break;
            case OP_PUSH_SUBFACE:
                subface++;
                break;
            case OP_POP_SUBFACE:
                if (subface > 0)
                    subface--;
                break;
            }
            pos += len;
        }
        build();
        return root;
    }
};

int loadFile(Shared &sh, const std::string &path, std::string *error) {
    auto it = sh.refs.find(path);
    if (it != sh.refs.end())
        return it->second;
    if (sh.depth > 16)
        return -1;
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        if (error)
            *error = "cannot read " + path;
        return -1;
    }
    std::vector<unsigned char> data;
    unsigned char chunk[65536];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof chunk, fp)) > 0)
        data.insert(data.end(), chunk, chunk + n);
    std::fclose(fp);
    if (data.size() < 4 || be16(data.data()) != OP_HEADER) {
        if (error)
            *error = path + ": not an OpenFlight file";
        return -1;
    }
    sh.depth++;
    File f(sh);
    int root = f.read(data, path);
    sh.depth--;
    sh.refs[path] = root;
    return root;
}

} // namespace

std::shared_ptr<Model> loadFLT(const std::string &path, std::string *error) {
    auto model = std::make_shared<Model>();
    model->source = path;
    // node 0: the root of the model, above the root of the file
    Node top;
    top.name = path;
    model->nodes.push_back(top);
    Shared sh(*model);
    int root = loadFile(sh, path, error);
    if (root < 0)
        return nullptr;
    model->nodes[0].children.push_back(root);
    if (model->meshes.empty()) {
        if (error)
            *error = path + ": nothing to draw";
        return nullptr;
    }
    return model;
}

} // namespace r4::assets
