// pfb_loader.cpp - the binary files of IRIS Performer (.pfb), versions 1
// (0xDB0ACE00) and 2 (0xDB0ACE01, scrambled coordinates). A port of the
// loader of legacy_opengl_demo (src/mekton/pfb.c), whose layout was rebuilt
// from the pfdLoadFile_pfb code of the IRIX executables.
//
// After a header (magic, version, 1, header size), chunks of records: type,
// count, byte size. 0 materials, 1 textures, 2 texture environments, 3
// geostates, 4..8 lists (lengths, coordinates, colors, normals, texture
// coordinates), 10 geosets, 11 user data, 12 nodes, 14 morphs. Records
// reference each other by their position in the chunks of their type; node
// 0 is the root. Performer is Z-up, as R4nkine.
#include "r4/assets/model.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>

namespace r4::assets {
namespace {

constexpr int kInherit = -1000;

enum {
    TRIFANS,
    TRISTRIPS,
    TRIS,
    POINTS,
    LINES,
    LINESTRIPS,
    FLAT_LINESTRIPS,
    QUADS,
    FLAT_TRISTRIPS,
    POLYS
};
enum {
    BIND_NONE,
    BIND_OFF,
    PER_VERTEX,
    PER_PRIM,
    OVERALL
};
enum {
    NODE_TEXT = 1,
    NODE_GEODE,
    NODE_BBOARD,
    NODE_REF23,
    NODE_GROUP,
    NODE_SCS,
    NODE_DCS,
    NODE_PARTITION,
    NODE_SCENE,
    NODE_SWITCH,
    NODE_LOD,
    NODE_SEQUENCE,
    NODE_LAYER,
    NODE_MORPH
};
enum {
    GS_TRANSPARENCY = 1,
    GS_ANTIALIAS,
    GS_DECAL,
    GS_ALPHAFUNC,
    GS_ENLIGHTING,
    GS_ENTEXTURE,
    GS_ENFOG,
    GS_CULLFACE,
    GS_ENLPOINTSTATE = 12,
    GS_FRONTMTL = 15,
    GS_BACKMTL,
    GS_TEXTURE,
    GS_TEXENV,
    GS_LIGHTS = 21,
    GS_LPOINTSTATE = 24
};

struct Reader {
    const unsigned char *p;
    const unsigned char *end;
    bool error = false;
    uint32_t u32() {
        if (p + 4 > end) {
            error = true;
            return 0;
        }
        uint32_t v = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        p += 4;
        return v;
    }
    int i32() {
        return (int)u32();
    }
    float f32() {
        uint32_t u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    void skip(long n) {
        if (n < 0 || p + n > end) {
            error = true;
            p = end;
        } else {
            p += n;
        }
    }
    std::string name() {
        int len = i32();
        if (len == -1 || error) {
            return {};
        }
        if (len < 0 || p + len > end) {
            error = true;
            return {};
        }
        std::string s((const char *)p, (size_t)len);
        p += len;
        while (!s.empty() && s.back() == 0) {
            s.pop_back();
        }
        return s;
    }
    std::vector<int> words() {
        int n = i32();
        std::vector<int> w;
        if (n < 0 || error || p + 4L * n > end) {
            error = true;
            return w;
        }
        w.resize(n);
        for (int i = 0; i < n; i++) {
            w[i] = i32();
        }
        return w;
    }
};

float wordFloat(int w) {
    float f;
    uint32_t u = (uint32_t)w;
    std::memcpy(&f, &u, 4);
    return f;
}

struct PMtl {
    float alpha = 1;
    float shininess = 0;
    float ambient[3] = {};
    float diffuse[3] = {};
    float specular[3] = {};
    float emission[3] = {};
    int cmode = 0;
};
struct PGState {
    int transparency = kInherit;
    int alphafunc = kInherit;
    int lighting = kInherit;
    int texturing = kInherit;
    int cullface = kInherit;
    int frontmtl = -1;
    int texture = -1;
    int decal = 0;
};
struct PList {
    int count = 0;
    int comps = 0;
    std::vector<float> f;
    std::vector<int> i;
};
struct PGSet {
    int prim = 0;
    int nprims = 0;
    int lengths = -1;
    int bind[4] = {};
    int list[4] = {-1, -1, -1, -1};
    int gstate = -1;
};
struct PNode {
    int type = 0;
    std::string name;
    std::vector<int> children;
    std::vector<int> gsets;
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    int ref = -1;
    int switchVal = -1;
    int seqBegin = 0;
    std::vector<float> lodRanges;
    Vec3 lodCenter;
    bool hasLodCenter = false;
    std::vector<Vec3> bbPositions;
    bool bbPoint = false;
    uint32_t travmask = 0xffffffffu;
};

// the key of the coordinates of the version 2 (found on earth.pfb, a sphere)
const uint32_t kCoordKey[3] = {0x21b3c975u, 0xad314ffbu, 0xc7230d61u};

bool plausible(float v) {
    return v == v && v > -1e7f && v < 1e7f && (v == 0 || v > 1e-20f || v < -1e-20f);
}

void unscramble(std::vector<float> &f) {
    int raw = 0;
    int dec = 0;
    for (size_t i = 0; i < f.size(); i++) {
        uint32_t u;
        std::memcpy(&u, &f[i], 4);
        raw += plausible(f[i]);
        u ^= kCoordKey[i % 3];
        float d;
        std::memcpy(&d, &u, 4);
        dec += plausible(d);
    }
    if (dec <= raw) {
        return;
    }
    for (size_t i = 0; i < f.size(); i++) {
        uint32_t u;
        std::memcpy(&u, &f[i], 4);
        u ^= kCoordKey[i % 3];
        std::memcpy(&f[i], &u, 4);
    }
}

struct File {
    int version = 1;
    std::vector<PMtl> mtl;
    std::vector<std::string> tex;
    std::vector<PGState> gstate;
    std::vector<PList> list[9];
    std::vector<PGSet> gset;
    std::vector<PNode> node;
    std::vector<std::vector<int>> morph;
};

void readGState(Reader &r, PGState &g) {
    std::vector<int> w = r.words();
    size_t i = 0;
    while (i + 1 < w.size()) { // the last word is the user data
        int key = w[i++];
        int v = i < w.size() ? w[i] : 0;
        switch (key) {
        case GS_TRANSPARENCY:
            g.transparency = v;
            i++;
            break;
        case GS_ALPHAFUNC:
            g.alphafunc = v;
            i++;
            break;
        case GS_ENLIGHTING:
            g.lighting = v;
            i++;
            break;
        case GS_ENTEXTURE:
            g.texturing = v;
            i++;
            break;
        case GS_CULLFACE:
            g.cullface = v;
            i++;
            break;
        case GS_FRONTMTL:
            g.frontmtl = v;
            i++;
            break;
        case GS_TEXTURE:
            g.texture = v;
            i++;
            break;
        case GS_DECAL:
            g.decal = v;
            i += 3;
            break; // mode, id, offset
        case GS_LIGHTS:
            i += 1 + (v > 0 ? v : 0);
            break;
        default:
            i++;
            break;
        }
    }
}

void readList(Reader &r, int type, PList &l, int version) {
    static const int comps[9] = {0, 0, 0, 0, 1, 3, 4, 3, 2};
    int n = r.i32();
    r.i32(); // a cycle buffer
    r.i32(); // user data
    if (n < 0 || r.error || r.p + 4L * n * comps[type] > r.end) {
        r.error = true;
        return;
    }
    l.count = n;
    l.comps = comps[type];
    if (type == 4) {
        l.i.resize(n);
        for (int i = 0; i < n; i++) {
            l.i[i] = r.i32();
        }
    } else {
        l.f.resize((size_t)n * l.comps);
        for (float &v : l.f) {
            v = r.f32();
        }
        if (type == 5 && version >= 2) {
            unscramble(l.f);
        }
    }
}

void readGSet(Reader &r, PGSet &g) {
    g.prim = r.i32();
    g.nprims = r.i32();
    g.lengths = r.i32();
    for (int i = 0; i < 4; i++) {
        g.bind[i] = r.i32();
        g.list[i] = r.i32();
        r.i32(); // index list: not used by the files of the demos
    }
    r.i32(); // flat shading
    r.i32(); // wireframe
    r.i32(); // compile GL
    g.gstate = r.i32();
    r.i32();
    r.f32(); // line width
    r.f32(); // point size
    r.i32(); // draw bin
    r.u32(); // intersection mask
    r.i32();
    r.i32();
    for (int i = 0; i < 6; i++) {
        r.f32(); // bounding box
    }
    r.i32(); // user data
}

void readNode(Reader &r, PNode &nd) {
    std::vector<int> w = r.words();
    int n = (int)w.size();
    int p = 1;
    int ngsets = 0;
    if (n > 0) {
        nd.type = w[0];
        switch (nd.type) {
        case NODE_TEXT:
        case NODE_GEODE:
            ngsets = n > 1 && w[1] >= 0 && w[1] < n ? w[1] : 0;
            p = 2 + (nd.type == NODE_TEXT ? ngsets : 0);
            break;
        case NODE_BBOARD:
            ngsets = n > 1 && w[1] >= 0 && w[1] < n ? w[1] : 0;
            for (int i = 0; i < ngsets && 2 + 3 * i + 2 < n; i++) {
                nd.bbPositions.push_back({wordFloat(w[2 + 3 * i]), wordFloat(w[3 + 3 * i]), wordFloat(w[4 + 3 * i])});
            }
            p = 2 + 3 * ngsets + 4;
            // the axis, then the mode: 0 axial, else turned to the eye
            if (p - 1 < n) {
                nd.bbPoint = w[p - 1] != 0;
            }
            break;
        case NODE_REF23:
        case NODE_MORPH:
            nd.ref = n > 1 ? w[1] : -1;
            p = 2;
            break;
        case NODE_SCS:
        case NODE_DCS:
            p = nd.type == NODE_DCS ? 2 : 1;
            for (int i = 0; i < 16 && p + i < n; i++) {
                nd.matrix[i] = wordFloat(w[p + i]);
            }
            p += 16;
            break;
        case NODE_SCENE:
            p = 3;
            break;
        case NODE_SWITCH:
            nd.switchVal = n > 1 ? w[1] : -1;
            p = 2;
            break;
        case NODE_LOD: {
            // ranges (k + 1), transitions (k + 1), the center: kept when
            // they look like distances
            int k = n > 1 && w[1] >= 0 && w[1] < n ? w[1] : 0;
            bool ok = 2 + 2 * (k + 1) + 3 <= n;
            for (int i = 0; ok && i <= k; i++) {
                float v = wordFloat(w[2 + i]);
                if (!(v >= 0 && v < 1e9f) || (i && v < nd.lodRanges.back())) {
                    ok = false;
                } else {
                    nd.lodRanges.push_back(v);
                }
            }
            if (!ok) {
                nd.lodRanges.clear();
            } else {
                int c = 2 + 2 * (k + 1);
                nd.lodCenter = {wordFloat(w[c]), wordFloat(w[c + 1]), wordFloat(w[c + 2])};
                nd.hasLodCenter = true;
            }
            p = 2 + 2 * (k + 1) + 3 + 2;
            break;
        }
        case NODE_SEQUENCE: {
            int frames = n > 1 && w[1] >= 0 && 2 + w[1] + 6 <= n ? w[1] : 0;
            if (!frames) {
                p = n;
                break;
            }
            p = 2 + frames;
            nd.seqBegin = w[p + 1];
            p += 6;
            break;
        }
        case NODE_LAYER:
            p = 4;
            break;
        default:
            break;
        }
        if (ngsets > n - p) {
            ngsets = p < n ? n - p : 0;
        }
        if (p < n) {
            if (nd.type == NODE_GEODE || nd.type == NODE_BBOARD) {
                for (int i = 0; i < ngsets && p < n; i++) {
                    nd.gsets.push_back(w[p++]);
                }
            } else if (nd.type != NODE_TEXT) {
                int count = w[p++];
                if (count < 0 || p + count > n) {
                    count = 0;
                }
                for (int i = 0; i < count; i++) {
                    nd.children.push_back(w[p++]);
                }
            }
        }
        if (p + 5 <= n) {
            nd.travmask = (uint32_t)w[p]; // the cull traversal mask
        }
    }
    nd.name = r.name();
}

// pfMorph: attributes (type, floats per element, elements, sources, dest
// list, index flag, sources), then one weight per source
void applyMorph(File &f, const std::vector<int> &w) {
    static const int listType[6] = {0, 6, 7, 8, 5, 0};
    if (w.empty()) {
        return;
    }
    int nattr = w[0];
    int p = 1;
    if (nattr > 8) {
        return;
    }
    struct At {
        int type;
        int fpe;
        int nelts;
        int nsrc;
        int dst;
        int src[8];
    } at[8];
    for (int a = 0; a < nattr; a++) {
        if (p + 6 > (int)w.size()) {
            return;
        }
        at[a].type = w[p] >= 1 && w[p] <= 4 ? listType[w[p]] : 0;
        at[a].fpe = w[p + 1];
        at[a].nelts = w[p + 2];
        at[a].nsrc = w[p + 3];
        at[a].dst = w[p + 4];
        if (w[p + 5] != 0 || at[a].nsrc > 8) {
            return;
        }
        p += 6;
        for (int i = 0; i < at[a].nsrc && p < (int)w.size(); i++) {
            at[a].src[i] = w[p++];
        }
    }
    for (int a = 0; a < nattr; a++) {
        int t = at[a].type;
        if (!t || at[a].dst < 0 || at[a].dst >= (int)f.list[t].size() || p + at[a].nsrc > (int)w.size()) {
            continue;
        }
        PList &dl = f.list[t][at[a].dst];
        int cnt = std::min(at[a].nelts * at[a].fpe, (int)dl.f.size());
        for (int j = 0; j < cnt; j++) {
            float v = 0;
            for (int i = 0; i < at[a].nsrc; i++) {
                int s = at[a].src[i];
                if (s >= 0 && s < (int)f.list[t].size() && j < (int)f.list[t][s].f.size()) {
                    v += wordFloat(w[p + i]) * f.list[t][s].f[j];
                }
            }
            dl.f[j] = v;
        }
    }
}

bool parse(const std::vector<unsigned char> &buf, File &f) {
    Reader r{buf.data(), buf.data() + buf.size()};
    if ((r.u32() & ~1u) != 0xDB0ACE00u) {
        return false;
    }
    f.version = (int)r.u32();
    r.u32();
    uint32_t header = r.u32();
    if (header > buf.size()) {
        return false;
    }
    r.p = buf.data() + header;
    while (!r.error && r.p + 12 <= r.end) {
        int type = r.i32();
        int count = r.i32();
        int bytes = r.i32();
        const unsigned char *next = r.p + bytes;
        if (bytes < 0 || next > r.end || count < 0 || count > bytes / 4) {
            break;
        }
        switch (type) {
        case 0:
            for (int i = 0; i < count && !r.error; i++) {
                PMtl m;
                r.i32(); // side
                m.alpha = r.f32();
                m.shininess = r.f32();
                for (float *v : {m.ambient, m.diffuse, m.specular, m.emission}) {
                    for (int k = 0; k < 3; k++) {
                        v[k] = r.f32();
                    }
                }
                r.i32(); // color mode side
                m.cmode = r.i32();
                r.i32();
                f.mtl.push_back(m);
            }
            break;
        case 1:
            for (int i = 0; i < count && !r.error; i++) {
                std::string name = r.name();
                const unsigned char *body = r.p;
                r.skip(224);
                if (r.error) {
                    break;
                }
                Reader b{body + 0xd0, body + 224};
                int nimages = b.i32();
                b.i32();
                int nlevels = b.i32();
                if (nimages > 0) {
                    r.skip(4L * nimages);
                }
                if (nlevels > 0) {
                    r.skip(8L * nlevels);
                }
                f.tex.push_back(name);
            }
            break;
        case 3:
            for (int i = 0; i < count && !r.error; i++) {
                PGState g;
                readGState(r, g);
                f.gstate.push_back(g);
            }
            break;
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            for (int i = 0; i < count && !r.error; i++) {
                PList l;
                readList(r, type, l, f.version);
                f.list[type].push_back(std::move(l));
            }
            break;
        case 10:
            for (int i = 0; i < count && !r.error; i++) {
                PGSet g;
                readGSet(r, g);
                f.gset.push_back(g);
            }
            break;
        case 12:
            for (int i = 0; i < count && !r.error; i++) {
                PNode nd;
                readNode(r, nd);
                f.node.push_back(std::move(nd));
            }
            break;
        case 14:
            for (int i = 0; i < count && !r.error; i++) {
                f.morph.push_back(r.words());
            }
            break;
        default:
            break; // texture environments, user data, light points...
        }
        r.p = next;
    }
    for (auto &m : f.morph) {
        applyMorph(f, m);
    }
    return !f.node.empty();
}

struct Builder {
    Builder(File &file, Model &m) : f(file), model(m) {
    }
    File &f;
    Model &model;
    std::string dir;
    std::map<std::string, int> images;
    std::map<std::tuple<int, bool, bool, bool>, int> materials; // gstate, vcolors, lit, decal
    std::map<std::pair<int, bool>, int> meshes;                 // gset, decal

    int image(const std::string &name) {
        if (name.empty()) {
            return -1;
        }
        size_t slash = name.find_last_of("/\\");
        std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
        auto it = images.find(base);
        if (it != images.end()) {
            return it->second;
        }
        Image img;
        std::string err;
        int index = -1;
        std::string found = findAsset(dir, name);
        if (!found.empty() && loadImage(found, img, &err)) {
            index = (int)model.images.size();
            model.images.push_back(std::move(img));
        }
        if (index < 0) {
            std::fprintf(stderr, "r4: pfb: texture %s not found\n", name.c_str());
        }
        images[base] = index;
        return index;
    }

    int material(int gs, bool vcolors, bool hasNormals, bool decal) {
        const PGState *g = gs >= 0 && gs < (int)f.gstate.size() ? &f.gstate[gs] : nullptr;
        bool lit = g && g->lighting != kInherit ? g->lighting == 2 : hasNormals;
        auto key = std::make_tuple(gs, vcolors, lit, decal);
        auto it = materials.find(key);
        if (it != materials.end()) {
            return it->second;
        }
        Material m;
        m.name = "gstate " + std::to_string(gs);
        m.shading = lit ? Shading::Phong : Shading::Unlit;
        m.diffuse = {1, 1, 1, 1};
        m.ambient = {1, 1, 1};
        m.doubleSided = true;
        m.decal = decal;
        if (g) {
            if (g->frontmtl >= 0 && g->frontmtl < (int)f.mtl.size()) {
                const PMtl &p = f.mtl[g->frontmtl];
                m.diffuse = {p.diffuse[0], p.diffuse[1], p.diffuse[2], p.alpha};
                m.ambient = {p.ambient[0], p.ambient[1], p.ambient[2]};
                m.specular = {p.specular[0], p.specular[1], p.specular[2]};
                m.emissive = {p.emission[0], p.emission[1], p.emission[2]};
                m.shininess = p.shininess;
                // the vertex colors drive ambient and diffuse (PFMTL_CMODE_AD)
                if (vcolors && lit && p.cmode == 2) {
                    m.diffuse = {1, 1, 1, p.alpha};
                    m.ambient = {1, 1, 1};
                }
            }
            bool textured = g->texturing != kInherit ? g->texturing == 2 : g->texture >= 0;
            if (textured && g->texture >= 0 && g->texture < (int)f.tex.size()) {
                m.diffuseImage = image(f.tex[g->texture]);
            }
            if (g->transparency != kInherit && g->transparency >= 2) {
                m.alpha = AlphaMode::Blend;
            } else if (m.diffuse.w < 1) {
                m.alpha = AlphaMode::Blend;
            } else if (g->alphafunc != kInherit && g->alphafunc > 1 && m.diffuseImage >= 0) {
                m.alpha = AlphaMode::Mask;
            }
            if (m.diffuseImage >= 0 && m.alpha == AlphaMode::Opaque && model.images[m.diffuseImage].channels == 4) {
                m.alpha = AlphaMode::Mask;
            }
            if (g->cullface == 2) {
                m.doubleSided = false;
            }
        }
        // lit, the colors only count with the color mode; unlit, always
        m.vertexColors = vcolors && (!lit || (g && g->frontmtl >= 0 && g->frontmtl < (int)f.mtl.size() ? f.mtl[g->frontmtl].cmode == 2 : true));
        int index = (int)model.materials.size();
        model.materials.push_back(m);
        materials[key] = index;
        return index;
    }

    // a geoset as one mesh (triangles, lines or points)
    int mesh(int gi, bool decal) {
        auto key = std::make_pair(gi, decal);
        auto it = meshes.find(key);
        if (it != meshes.end()) {
            return it->second;
        }
        meshes[key] = -1;
        if (gi < 0 || gi >= (int)f.gset.size()) {
            return -1;
        }
        const PGSet &g = f.gset[gi];
        static const int listType[4] = {5, 6, 7, 8};
        const PList *L[4] = {};
        for (int a = 0; a < 4; a++) {
            if (g.list[a] >= 0 && g.list[a] < (int)f.list[listType[a]].size() && g.bind[a] >= PER_VERTEX) {
                L[a] = &f.list[listType[a]][g.list[a]];
            }
        }
        if (!L[0]) {
            return -1;
        }
        const std::vector<int> *lengths = g.lengths >= 0 && g.lengths < (int)f.list[4].size() ? &f.list[4][g.lengths].i : nullptr;
        Mesh m;
        int prim = g.prim;
        m.primitive = prim == POINTS ? Primitive::Points : prim == LINES || prim == LINESTRIPS || prim == FLAT_LINESTRIPS ? Primitive::Lines : Primitive::Triangles;
        int vert = 0; // the next vertex of the lists
        auto attrib = [&](int a, int primIndex, int vertex, int comps) -> const float * {
            const PList *l = L[a];
            if (!l) {
                return nullptr;
            }
            int i = g.bind[a] == PER_VERTEX ? vertex : g.bind[a] == PER_PRIM ? primIndex : 0;
            if (i < 0 || i >= l->count || l->comps != comps) {
                return nullptr;
            }
            return &l->f[(size_t)i * comps];
        };
        // flat strips: the colors and normals are per triangle, not per vertex
        auto vertexAt = [&](int primIndex, int v, int flatIndex) {
            Vertex out;
            const float *p = attrib(0, primIndex, v, 3);
            if (p) {
                out.position = {p[0], p[1], p[2]};
            }
            int ai = flatIndex >= 0 ? flatIndex : v;
            if (const float *n = attrib(2, primIndex, ai, 3)) {
                out.normal = {n[0], n[1], n[2]};
            }
            if (const float *c = attrib(1, primIndex, ai, 4)) {
                auto b = [](float x) {
                    return (uint32_t)(std::fmin(std::fmax(x, 0.0f), 1.0f) * 255 + 0.5f);
                };
                out.color = b(c[0]) | b(c[1]) << 8 | b(c[2]) << 16 | b(c[3]) << 24;
            }
            if (const float *t = attrib(3, primIndex, v, 2)) {
                out.uv = {t[0], t[1]};
            }
            return out;
        };
        int flatCounter = 0; // the per-vertex attributes of the flat primitives
        for (int pi = 0; pi < g.nprims; pi++) {
            int len;
            switch (prim) {
            case TRIS:
                len = 3;
                break;
            case QUADS:
                len = 4;
                break;
            case LINES:
                len = 2;
                break;
            case POINTS:
                len = 1;
                break;
            default:
                len = lengths && pi < (int)lengths->size() ? (*lengths)[pi] : 0;
                break;
            }
            if (len <= 0 || vert + len > L[0]->count) {
                break;
            }
            uint32_t base = (uint32_t)m.vertices.size();
            for (int k = 0; k < len; k++) {
                int flat = -1;
                if (prim == FLAT_TRISTRIPS) {
                    flat = flatCounter + std::max(0, k - 2);
                } else if (prim == FLAT_LINESTRIPS) {
                    flat = flatCounter + std::max(0, k - 1);
                }
                m.vertices.push_back(vertexAt(pi, vert + k, flat));
            }
            if (prim == FLAT_TRISTRIPS) {
                flatCounter += std::max(0, len - 2);
            } else if (prim == FLAT_LINESTRIPS) {
                flatCounter += std::max(0, len - 1);
            }
            vert += len;
            switch (prim) {
            case TRIS:
                m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
                break;
            case QUADS:
                m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
                break;
            case TRIFANS:
            case POLYS:
                for (int k = 1; k + 1 < len; k++) {
                    m.indices.insert(m.indices.end(), {base, base + k, base + k + 1});
                }
                break;
            case TRISTRIPS:
            case FLAT_TRISTRIPS:
                for (int k = 0; k + 2 < len; k++) {
                    if (k % 2 == 0) {
                        m.indices.insert(m.indices.end(), {base + k, base + k + 1, base + k + 2});
                    } else {
                        m.indices.insert(m.indices.end(), {base + k + 1, base + k, base + k + 2});
                    }
                }
                break;
            case LINES:
                m.indices.insert(m.indices.end(), {base, base + 1});
                break;
            case LINESTRIPS:
            case FLAT_LINESTRIPS:
                for (int k = 0; k + 1 < len; k++) {
                    m.indices.insert(m.indices.end(), {base + k, base + k + 1});
                }
                break;
            case POINTS:
                m.indices.push_back(base);
                break;
            }
        }
        if (m.indices.empty()) {
            return -1;
        }
        bool hasNormals = L[2] != nullptr;
        if (!hasNormals && m.primitive == Primitive::Triangles) {
            // no normals: lit only when the geostate says so; flat ones then
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                Vertex &a = m.vertices[m.indices[i]];
                Vertex &b = m.vertices[m.indices[i + 1]];
                Vertex &c = m.vertices[m.indices[i + 2]];
                Vec3 n = normalize(cross(b.position - a.position, c.position - a.position));
                a.normal = b.normal = c.normal = n;
            }
        }
        m.material = material(g.gstate, L[1] != nullptr, hasNormals, decal);
        m.name = "gset " + std::to_string(gi);
        m.computeBounds();
        int index = (int)model.meshes.size();
        model.meshes.push_back(std::move(m));
        meshes[key] = index;
        return index;
    }

    // the nodes; shared subgraphs are shared, except under the decal layers
    std::map<std::pair<int, bool>, int> made;
    int node(int idx, bool decal, int depth) {
        if (idx < 0 || idx >= (int)f.node.size() || depth > 64) {
            return -1;
        }
        auto key = std::make_pair(idx, decal);
        auto it = made.find(key);
        if (it != made.end()) {
            return it->second;
        }
        const PNode &pn = f.node[idx];
        int me = (int)model.nodes.size();
        made[key] = me;
        model.nodes.emplace_back();
        Node n;
        n.name = pn.name;
        if (pn.type != NODE_SCENE) {
            n.mask = pn.travmask ? pn.travmask : 0xffffffffu;
        }
        if (pn.type == NODE_DCS || pn.type == NODE_SCS) {
            std::memcpy(&n.local[0][0], pn.matrix, sizeof pn.matrix);
        }
        std::vector<int> kids;
        if (pn.type == NODE_BBOARD) {
            // the geosets at their positions, turned to the eye
            for (size_t i = 0; i < pn.gsets.size(); i++) {
                int mi = mesh(pn.gsets[i], decal);
                if (mi < 0) {
                    continue;
                }
                Node b;
                b.name = "billboard";
                b.kind = NodeKind::Billboard;
                b.billboardPoint = pn.bbPoint;
                if (i < pn.bbPositions.size()) {
                    b.local = translate(pn.bbPositions[i]);
                }
                b.meshes.push_back(mi);
                kids.push_back((int)model.nodes.size());
                model.nodes.push_back(b);
            }
        } else {
            for (int gi : pn.gsets) {
                int mi = mesh(gi, decal);
                if (mi >= 0) {
                    n.meshes.push_back(mi);
                }
            }
        }
        for (size_t i = 0; i < pn.children.size(); i++) {
            // the children of a layer after the first are decals on it
            bool d = decal || (pn.type == NODE_LAYER && i > 0);
            int c = node(pn.children[i], d, depth + 1);
            if (c >= 0) {
                kids.push_back(c);
            }
        }
        if (pn.type == NODE_SWITCH || pn.type == NODE_SEQUENCE || (pn.type == NODE_LOD && pn.lodRanges.empty())) {
            n.kind = NodeKind::Switch;
            // Performer: PFSWITCH_ON -1, OFF -2 (a sequence shows its first frame,
            // a LOD without ranges its most detailed child)
            n.switchValue = pn.type == NODE_SWITCH ? (pn.switchVal < -1 ? -2 : pn.switchVal) : pn.type == NODE_SEQUENCE ? std::max(0, pn.seqBegin) : 0;
        } else if (pn.type == NODE_LOD) {
            n.kind = NodeKind::LOD;
            n.lodRanges = pn.lodRanges;
            n.lodCenter = pn.lodCenter;
            n.hasLodCenter = pn.hasLodCenter;
        }
        if (n.kind != NodeKind::Group && !n.meshes.empty()) {
            Node g;
            g.meshes = std::move(n.meshes);
            n.meshes.clear();
            kids.insert(kids.begin(), (int)model.nodes.size());
            model.nodes.push_back(g);
            if (n.kind == NodeKind::Switch && n.switchValue >= 0) {
                n.switchValue++;
            }
        }
        n.children = kids;
        model.nodes[me] = std::move(n);
        return me;
    }
};

} // namespace

std::shared_ptr<Model> loadPFB(const std::string &path, std::string *error) {
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        if (error) {
            *error = "cannot read " + path;
        }
        return nullptr;
    }
    std::vector<unsigned char> buf;
    unsigned char chunk[65536];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof chunk, fp)) > 0) {
        buf.insert(buf.end(), chunk, chunk + n);
    }
    std::fclose(fp);
    File f;
    if (!parse(buf, f)) {
        if (error) {
            *error = path + ": not a Performer binary file";
        }
        return nullptr;
    }
    auto model = std::make_shared<Model>();
    model->source = path;
    Builder b(f, *model);
    size_t slash = path.find_last_of("/\\");
    b.dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    int root = b.node(0, false, 0);
    if (root != 0 || model->meshes.empty()) {
        if (error) {
            *error = path + ": nothing to draw";
        }
        return nullptr;
    }
    model->nodes[0].name = path;
    return model;
}

} // namespace r4::assets
