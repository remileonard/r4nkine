// gobj_loader.cpp - the .d objects of the SGI flight simulator (libgobj).
//
// A text file of sections (the old files give the counts only, in this
// order):
//   R<radius>
//   B<count>  branch nodes: "statebits,modebits", the transforms
//             "count,t,t...", the children "count,B1,G2,..." (B: branch,
//             G: geometry)
//   T<count>  transforms: "0|1|2,tenths of degrees" (rotation about X, Y,
//             Z), "3,x,y,z" (translation), "4,x,y,z" (scale)
//   G<count>  geometries: a type then its data (see readGeometry)
//   M<count>  materials: ambient; diffuse and alpha; specular; shininess;
//             emission
// A node is drawn when the state given to drawobj meets its statebits (the
// main body 0x1, the landing gear 0x2, the far model 0x4, the shadows 0x8
// and 0x10, the weapons 0xff00...): these become the node masks, and the
// default state of the model is body, gear and weapons. The modebits: 0x1
// back faces culled (two-sided otherwise), 0x2 transparent, 0x4 no depth
// writes. The objects of flight are Y-up.
#include "r4/axes.h"
#include "r4/assets/model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace r4::assets {
namespace {

enum GeomType {
    LPV = 5, LPU = 6, LPS = 7, CPV = 10, CPU = 11, CSECT = 12, LTV = 25,
    CLS = 18, CDV = 22, CDU = 23, CDS = 24, IMV = 31, IMU = 32, IPV = 33, IPU = 34, TLPU = 100,
};

constexpr uint32_t kCull = 0x80000000u;
constexpr uint32_t kEightView = 0x10000000u;
constexpr uint32_t kBackface = 0x1, kTransparent = 0x2, kNoZWrite = 0x4;

// the materials flight defines for the files without M section (light.c)
struct FlightMaterial {
    int id;
    float amb[3], dif[3], spec[3], shininess, alpha;
};
const FlightMaterial kFlightMaterials[] = {
    {1, {0.3f, 0.6f, 0.3f}, {0.3f, 0.6f, 0.3f}, {0, 0, 0}, 0, 1},              // swamp
    {2, {0.7f, 0.7f, 0.7f}, {0.7f, 0.7f, 0.7f}, {1, 1, 1}, 30, 1},             // plane
    {3, {0.44f, 0.37f, 0.19f}, {0.44f, 0.37f, 0.19f}, {0, 0, 0}, 0, 1},        // dirt
    {4, {0.88f, 0.88f, 0.88f}, {0.88f, 0.88f, 0.88f}, {0, 0, 0}, 0, 1},        // gray 0
    {5, {0.82f, 0.82f, 0.82f}, {0.82f, 0.82f, 0.82f}, {0, 0, 0}, 0, 1},
    {6, {0.75f, 0.75f, 0.75f}, {0.75f, 0.75f, 0.75f}, {0, 0, 0}, 0, 1},
    {7, {0.69f, 0.69f, 0.69f}, {0.69f, 0.69f, 0.69f}, {0, 0, 0}, 0, 1},
    {8, {0.63f, 0.63f, 0.63f}, {0.63f, 0.63f, 0.63f}, {0, 0, 0}, 0, 1},
    {9, {0.55f, 0.55f, 0.55f}, {0.55f, 0.55f, 0.55f}, {0, 0, 0}, 0, 1},
    {10, {0.13f, 0.13f, 0.13f}, {0.5f, 0.5f, 0.5f}, {0, 0, 0}, 0, 1},
    {11, {0.44f, 0.44f, 0.44f}, {0.44f, 0.44f, 0.44f}, {0, 0, 0}, 0, 1},
    {12, {0.38f, 0.38f, 0.38f}, {0.38f, 0.38f, 0.38f}, {0, 0, 0}, 0, 1},
    {13, {0.31f, 0.31f, 0.31f}, {0.31f, 0.31f, 0.31f}, {0, 0, 0}, 0, 1},
    {14, {0.25f, 0.25f, 0.25f}, {0.25f, 0.25f, 0.25f}, {0, 0, 0}, 0, 1},
    {15, {0.19f, 0.19f, 0.19f}, {0.19f, 0.19f, 0.19f}, {0, 0, 0}, 0, 1},
    {16, {0.13f, 0.13f, 0.13f}, {0.13f, 0.13f, 0.13f}, {0, 0, 0}, 0, 1},       // gray 12
    {17, {0.2f, 0.2f, 0.2f}, {0.2f, 0.2f, 0.2f}, {0.3f, 0.3f, 0.3f}, 5, 1},    // thruster
    {18, {0.4f, 0.4f, 0.5f}, {0.5f, 0.5f, 0.6f}, {0.9f, 0.9f, 1}, 30, 0.3f},  // glass
    {19, {0.3f, 0.3f, 0.3f}, {0.3f, 0.3f, 0.3f}, {0, 0, 0}, 0, 0.5f},          // propeller
    {20, {0.5f, 0.25f, 0}, {0.5f, 0.25f, 0}, {0, 0, 0}, 0, 1},                 // building orange
    {21, {0.4f, 0.5f, 0.35f}, {0.4f, 0.5f, 0.35f}, {0, 0, 0}, 0, 1},           // lime
    {22, {0.42f, 0.3f, 0.25f}, {0.42f, 0.3f, 0.25f}, {0, 0, 0}, 0, 1},         // tan
    {23, {0.6f, 0.6f, 0.6f}, {0.6f, 0.6f, 0.6f}, {0, 0, 0}, 0, 1},             // gray
    {24, {0.6f, 0, 0.6f}, {0.6f, 0, 0.6f}, {0, 0, 0}, 0, 1},                   // purple
    {25, {0.7f, 0, 0.7f}, {0.7f, 0, 0.7f}, {0, 0, 0}, 0, 1},                   // light purple
    {26, {0.7f, 0.7f, 0.7f}, {0.7f, 0.7f, 0.7f}, {0, 0, 0}, 0, 0.8f},          // missile trail
    {50, {0.1f, 0.1f, 0.1f}, {0.1f, 0.1f, 0.1f}, {0.3f, 0.3f, 0.3f}, 30, 1},   // F-14 black
    {51, {0.9f, 0.7f, 0}, {0.9f, 0.7f, 0}, {0.8f, 0.8f, 0.8f}, 30, 1},         // F-14 yellow
    {52, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, 30, 1},                              // white
};

// the 8 first colors of the IRIS color map; the others a grey ramp
uint32_t indexColor(long c) {
    static const uint32_t base[8] = {0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff,
                                     0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff};
    if (c >= 0 && c < 8)
        return base[c];
    unsigned g = (unsigned)(c & 0xff);
    return 0xff000000u | g << 16 | g << 8 | g;
}

// the packed colors of cpack: 0xAABBGGRR; alpha 0 in the files is opaque
uint32_t packColor(long c) {
    uint32_t v = (uint32_t)c & 0xffffffu;
    return v | 0xff000000u;
}

struct Polygon {
    std::vector<int> v;
    Vec3 normal;
    bool hasNormal = false;
    long color = 0;
    bool hasColor = false;
    int type = 2; // IMU/IMV: 0 points, 1 line, 2 polygon, 3 closed line, 4 polygon
};

struct Geometry {
    int type = 0;
    long material = 0, color = 0;
    Vec3 normal;
    std::vector<Vec3> v, n;
    std::vector<Vec2> uv;
    std::vector<long> vcolor;
    std::vector<Polygon> polys;
};

struct Branch {
    uint32_t state = 0, mode = 0;
    std::vector<int> trans;
    std::vector<std::pair<bool, int>> children; // geometry?, index
};

struct Transform {
    int type = 0, angle = 0;
    Vec3 v;
};

class Reader {
public:
    explicit Reader(FILE *f) : f_(f) {}
    // the next line that is not empty nor a comment
    bool next() {
        char buf[1024];
        while (std::fgets(buf, sizeof buf, f_)) {
            line_++;
            if (buf[0] == '\0' || buf[0] == '#' || buf[0] == '\n' || buf[0] == '\r')
                continue;
            cur_ = buf;
            // the lines ending with a backslash go on
            while (true) {
                size_t e = cur_.find_last_not_of(" \t\r\n");
                if (e == std::string::npos || cur_[e] != '\\')
                    break;
                cur_.erase(e);
                if (!std::fgets(buf, sizeof buf, f_))
                    break;
                line_++;
                cur_ += buf;
            }
            return true;
        }
        return false;
    }
    const char *str() const { return cur_.c_str(); }
    int line() const { return line_; }

    // the numbers of the line until a comment: ints, hex with %i, or floats;
    // B/G letters marked in kinds
    std::vector<double> numbers(std::vector<char> *kinds = nullptr) const {
        std::vector<double> out;
        const char *p = cur_.c_str();
        char kind = 0;
        while (*p && *p != '#' && *p != '\n') {
            if (*p == 'B' || *p == 'G') {
                kind = *p++;
                continue;
            }
            if (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9')) {
                char *end;
                double v;
                if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
                    v = (double)std::strtoul(p, &end, 16);
                else
                    v = std::strtod(p, &end);
                if (end == p) {
                    p++;
                    continue;
                }
                out.push_back(v);
                if (kinds)
                    kinds->push_back(kind);
                kind = 0;
                p = end;
                continue;
            }
            p++;
        }
        return out;
    }

private:
    FILE *f_;
    std::string cur_;
    int line_ = 0;
};

bool readVertices(Reader &r, Geometry &g, int perVertex) {
    if (!r.next())
        return false;
    int count = std::atoi(r.str());
    for (int i = 0; i < count; i++) {
        if (!r.next())
            return false;
        std::vector<double> n = r.numbers();
        n.resize(std::max<size_t>(n.size(), 6), 0);
        switch (perVertex) {
        case 0: // x y z
            g.v.push_back({(float)n[0], (float)n[1], (float)n[2]});
            break;
        case 1: // x y z nx ny nz
            g.v.push_back({(float)n[0], (float)n[1], (float)n[2]});
            g.n.push_back({(float)n[3], (float)n[4], (float)n[5]});
            break;
        case 2: // x y z s t
            g.v.push_back({(float)n[0], (float)n[1], (float)n[2]});
            g.uv.push_back({(float)n[3], (float)n[4]});
            break;
        case 3: // color x y z
            g.vcolor.push_back((long)n[0]);
            g.v.push_back({(float)n[1], (float)n[2], (float)n[3]});
            break;
        case 4: // x y z color
            g.v.push_back({(float)n[0], (float)n[1], (float)n[2]});
            g.vcolor.push_back((long)n[3]);
            break;
        }
    }
    return true;
}

// the polygons: each line has `prefix` numbers before the indices
// (0: none, 1: a color, 2: type and color, 3: a normal, -1: a type)
bool readPolygons(Reader &r, Geometry &g, int prefix) {
    if (!r.next())
        return false;
    int count = std::atoi(r.str());
    for (int i = 0; i < count; i++) {
        if (!r.next())
            return false;
        std::vector<double> n = r.numbers();
        Polygon p;
        size_t k = 0;
        if (prefix == 3 && n.size() >= 3) {
            p.normal = {(float)n[0], (float)n[1], (float)n[2]};
            p.hasNormal = true;
            k = 3;
        } else if (prefix == 1 && !n.empty()) {
            p.color = (long)n[0];
            p.hasColor = true;
            k = 1;
        } else if (prefix == 2 && n.size() >= 2) {
            p.type = (int)n[0];
            p.color = (long)n[1];
            p.hasColor = true;
            k = 2;
        } else if (prefix == -1 && !n.empty()) {
            p.type = (int)n[0];
            k = 1;
        }
        for (; k < n.size(); k++) {
            int idx = (int)n[k];
            if (idx >= 0 && idx < (int)g.v.size())
                p.v.push_back(idx);
        }
        g.polys.push_back(std::move(p));
    }
    return true;
}

bool readGeometry(Reader &r, Geometry &g) {
    auto header = [&](long &value) {
        if (!r.next())
            return false;
        value = (long)std::strtoul(r.str(), nullptr, 0);
        return true;
    };
    switch (g.type) {
    case LPV: // per vertex normals
    case LTV: // the same in triangle strips
        return header(g.material) && readVertices(r, g, 1) && readPolygons(r, g, 0);
    case LPU: // a normal per polygon
        return header(g.material) && readVertices(r, g, 0) && readPolygons(r, g, 3);
    case TLPU: // textured, a normal per polygon
        return header(g.material) && readVertices(r, g, 2) && readPolygons(r, g, 3);
    case LPS: { // one normal for all
        if (!header(g.material) || !r.next())
            return false;
        std::vector<double> n = r.numbers();
        n.resize(3, 0);
        g.normal = {(float)n[0], (float)n[1], (float)n[2]};
        return readVertices(r, g, 0) && readPolygons(r, g, 0);
    }
    case CSECT: // one color: polygons, lines or points
    case CLS:
    case CDS:
        return header(g.color) && readVertices(r, g, 0) && readPolygons(r, g, 0);
    case CPV:
    case CDV:
        return readVertices(r, g, 3) && readPolygons(r, g, 0);
    case CPU:
    case CDU:
        return readVertices(r, g, 0) && readPolygons(r, g, 1);
    case IMU:
        return readVertices(r, g, 0) && readPolygons(r, g, 2);
    case IMV:
        return readVertices(r, g, 4) && readPolygons(r, g, -1);
    case IPU:
        return readVertices(r, g, 0) && readPolygons(r, g, 1);
    case IPV:
        return readVertices(r, g, 4) && readPolygons(r, g, 0);
    }
    return false;
}

struct Loader {
    explicit Loader(Model &m) : model(m) {}
    Model &model;
    std::vector<Branch> branches;
    std::vector<Transform> transforms;
    std::vector<Geometry> geoms;
    std::vector<int> objectMaterials; // the M section: indices in model.materials
    bool hasM = false;
    std::map<std::pair<long, uint32_t>, int> litMaterials;   // (material, mode)
    std::map<uint32_t, int> colorMaterials;                   // mode
    std::map<std::pair<int, uint32_t>, std::vector<int>> geomMeshes; // (geometry, mode)

    void applyMode(Material &m, uint32_t mode) {
        m.doubleSided = !(mode & kBackface);
        if (mode & kTransparent) {
            m.alpha = AlphaMode::Blend;
            if (m.diffuse.w >= 1)
                m.diffuse.w = 0.5f;
        }
        if (mode & kNoZWrite)
            m.depthWrite = false;
    }

    int litMaterial(long id, uint32_t mode) {
        auto key = std::make_pair(id, mode);
        auto it = litMaterials.find(key);
        if (it != litMaterials.end())
            return it->second;
        Material m;
        if (hasM) {
            if (id >= 0 && id < (long)objectMaterials.size())
                m = model.materials[objectMaterials[id]];
        } else {
            for (const FlightMaterial &f : kFlightMaterials)
                if (f.id == id) {
                    m.name = "flight " + std::to_string(id);
                    m.ambient = {f.amb[0], f.amb[1], f.amb[2]};
                    m.diffuse = {f.dif[0], f.dif[1], f.dif[2], f.alpha};
                    m.specular = {f.spec[0], f.spec[1], f.spec[2]};
                    m.shininess = f.shininess;
                    if (f.alpha < 1)
                        m.alpha = AlphaMode::Blend;
                }
        }
        // the ambient of IRIS materials is relative to the ambient light
        applyMode(m, mode);
        int index = (int)model.materials.size();
        model.materials.push_back(m);
        litMaterials[key] = index;
        return index;
    }

    int colorMaterial(uint32_t mode) {
        auto it = colorMaterials.find(mode);
        if (it != colorMaterials.end())
            return it->second;
        Material m;
        m.name = "colors";
        m.shading = Shading::Unlit;
        m.diffuse = {1, 1, 1, 1};
        m.vertexColors = true;
        applyMode(m, mode);
        int index = (int)model.materials.size();
        model.materials.push_back(m);
        colorMaterials[mode] = index;
        return index;
    }

    // the meshes of a geometry drawn in a mode: triangles, lines, points
    std::vector<int> meshesOf(int gi, uint32_t mode) {
        auto key = std::make_pair(gi, mode);
        auto it = geomMeshes.find(key);
        if (it != geomMeshes.end())
            return it->second;
        const Geometry &g = geoms[gi];
        bool lit = g.type == LPV || g.type == LPU || g.type == LPS || g.type == LTV || g.type == TLPU;
        bool indexed = g.type >= IMV && g.type <= IPU;
        Mesh tris, lines, points;
        tris.primitive = Primitive::Triangles;
        lines.primitive = Primitive::Lines;
        points.primitive = Primitive::Points;
        auto colorOf = [&](const Polygon &p, int corner) -> uint32_t {
            long c;
            if (!g.vcolor.empty())
                c = g.vcolor[p.v[corner]];
            else if (p.hasColor)
                c = p.color;
            else
                c = g.color;
            return indexed ? indexColor(c) : packColor(c);
        };
        auto vertexOf = [&](const Polygon &p, int corner, Vec3 flatNormal) {
            Vertex v;
            int i = p.v[corner];
            v.position = g.v[i];
            if (!g.n.empty())
                v.normal = g.n[i];
            else
                v.normal = flatNormal;
            if (!g.uv.empty())
                v.uv = g.uv[i];
            if (!lit)
                v.color = colorOf(p, corner);
            return v;
        };
        for (const Polygon &p : g.polys) {
            int n = (int)p.v.size();
            if (!n)
                continue;
            // what the polygon draws
            int kind; // 0 points, 1 line, 2 polygon, 3 closed line, 5 strip
            if (g.type == CLS)
                kind = 1;
            else if (g.type == CDS || g.type == CDV || g.type == CDU)
                kind = 0;
            else if (g.type == IMU || g.type == IMV)
                kind = p.type == 4 ? 2 : p.type;
            else if (g.type == LTV)
                kind = 5;
            else
                kind = 2;
            Vec3 normal = p.hasNormal ? p.normal : g.normal;
            if (lit && !p.hasNormal && g.type != LPS && g.n.empty() && n >= 3)
                normal = normalize(cross(g.v[p.v[1]] - g.v[p.v[0]], g.v[p.v[2]] - g.v[p.v[0]]));
            if (kind == 2 || kind == 5) {
                if (n < 3)
                    continue;
                uint32_t base = (uint32_t)tris.vertices.size();
                for (int c = 0; c < n; c++)
                    tris.vertices.push_back(vertexOf(p, c, normal));
                for (int c = 1; c + 1 < n; c++) {
                    if (kind == 2) { // a fan
                        tris.indices.insert(tris.indices.end(), {base, base + c, base + c + 1});
                    } else if (c % 2) { // a strip, the odd triangles turned
                        tris.indices.insert(tris.indices.end(), {base + c - 1, base + c, base + c + 1});
                    } else {
                        tris.indices.insert(tris.indices.end(), {base + c, base + c - 1, base + c + 1});
                    }
                }
            } else if (kind == 1 || kind == 3) {
                uint32_t base = (uint32_t)lines.vertices.size();
                for (int c = 0; c < n; c++)
                    lines.vertices.push_back(vertexOf(p, c, normal));
                for (int c = 0; c + 1 < n; c++)
                    lines.indices.insert(lines.indices.end(), {base + c, base + c + 1});
                if (kind == 3 && n > 2)
                    lines.indices.insert(lines.indices.end(), {base + n - 1, base});
            } else {
                for (int c = 0; c < n; c++) {
                    points.indices.push_back((uint32_t)points.vertices.size());
                    points.vertices.push_back(vertexOf(p, c, normal));
                }
            }
        }
        std::vector<int> out;
        int material = lit ? litMaterial(g.material, mode) : colorMaterial(mode);
        for (Mesh *m : {&tris, &lines, &points}) {
            if (m->indices.empty())
                continue;
            m->material = m == &tris ? material : (lit ? colorMaterial(mode) : material);
            if (lit && m != &tris)
                for (Vertex &v : m->vertices)
                    v.color = 0xffffffffu;
            m->name = "G" + std::to_string(gi);
            m->computeBounds();
            out.push_back((int)model.meshes.size());
            model.meshes.push_back(std::move(*m));
        }
        geomMeshes[key] = out;
        return out;
    }
};

} // namespace

std::shared_ptr<Model> loadGObj(const std::string &path, std::string *error) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error)
            *error = "cannot read " + path;
        return nullptr;
    }
    int c = std::fgetc(f);
    if (c == 0) {
        std::fclose(f);
        if (error)
            *error = path + ": binary gobj files (memory dumps of IRIX structures) are not supported";
        return nullptr;
    }
    std::rewind(f);
    auto model = std::make_shared<Model>();
    model->source = path;
    Loader L(*model);
    Reader r(f);
    int oldOrder = 0;
    std::string err;
    auto readBranches = [&](int count) {
        for (int i = 0; i < count && err.empty(); i++) {
            Branch b;
            if (!r.next()) {
                err = "truncated branch";
                break;
            }
            std::vector<double> sm = r.numbers();
            sm.resize(2, 0);
            b.state = (uint32_t)(unsigned long)sm[0];
            b.mode = (uint32_t)(unsigned long)sm[1];
            if (!r.next()) {
                err = "truncated branch";
                break;
            }
            std::vector<double> t = r.numbers();
            for (size_t k = 1; k < t.size(); k++)
                b.trans.push_back((int)t[k]);
            if (!r.next()) {
                err = "truncated branch";
                break;
            }
            std::vector<char> kinds;
            std::vector<double> s = r.numbers(&kinds);
            for (size_t k = 1; k < s.size(); k++)
                b.children.push_back({kinds[k] == 'G', (int)s[k]});
            L.branches.push_back(b);
        }
    };
    auto readTransforms = [&](int count) {
        for (int i = 0; i < count && err.empty(); i++) {
            if (!r.next()) {
                err = "truncated transform";
                break;
            }
            std::vector<double> n = r.numbers();
            n.resize(4, 0);
            Transform t;
            t.type = (int)n[0];
            if (t.type <= 2)
                t.angle = (int)n[1];
            else
                t.v = {(float)n[1], (float)n[2], (float)n[3]};
            L.transforms.push_back(t);
        }
    };
    auto readGeometries = [&](int count) {
        for (int i = 0; i < count && err.empty(); i++) {
            Geometry g;
            if (!r.next()) {
                err = "truncated geometry";
                break;
            }
            g.type = std::atoi(r.str());
            if (!readGeometry(r, g)) {
                err = "bad geometry of type " + std::to_string(g.type);
                break;
            }
            L.geoms.push_back(std::move(g));
        }
    };
    auto readMaterials = [&](int count) {
        L.hasM = true;
        for (int i = 0; i < count && err.empty(); i++) {
            std::vector<double> v[5];
            for (int k = 0; k < 5; k++) {
                if (!r.next()) {
                    err = "truncated material";
                    return;
                }
                v[k] = r.numbers();
                v[k].resize(4, 0);
            }
            Material m;
            m.name = "M" + std::to_string(i);
            m.ambient = {(float)v[0][0], (float)v[0][1], (float)v[0][2]};
            m.diffuse = {(float)v[1][0], (float)v[1][1], (float)v[1][2], (float)v[1][3]};
            if (m.diffuse.w <= 0)
                m.diffuse.w = 1;
            if (m.diffuse.w < 1)
                m.alpha = AlphaMode::Blend;
            m.specular = {(float)v[2][0], (float)v[2][1], (float)v[2][2]};
            m.shininess = (float)v[3][0];
            m.emissive = {(float)v[4][0], (float)v[4][1], (float)v[4][2]};
            L.objectMaterials.push_back((int)model->materials.size());
            model->materials.push_back(m);
        }
    };
    while (err.empty() && r.next()) {
        const char *s = r.str();
        switch (s[0]) {
        case 'B': readBranches(std::atoi(s + 1)); break;
        case 'T': readTransforms(std::atoi(s + 1)); break;
        case 'G': readGeometries(std::atoi(s + 1)); break;
        case 'M': readMaterials(std::atoi(s + 1)); break;
        case 'R': break;
        default:
            if (s[0] >= '0' && s[0] <= '9') {
                // the old files: the counts in the order B, T, G
                int count = std::atoi(s);
                switch (oldOrder++) {
                case 0: readBranches(count); break;
                case 1: readTransforms(count); break;
                case 2: readGeometries(count); break;
                default: err = "syntax error"; break;
                }
            } else {
                err = "unknown section";
            }
        }
    }
    std::fclose(f);
    if (!err.empty() || L.branches.empty()) {
        if (error)
            *error = path + ": " + (err.empty() ? std::string("no branch node") : err) + " (line " +
                     std::to_string(r.line()) + ")";
        return nullptr;
    }

    // node 0: the root turning Y-up into Z-up; branch i: node 1 + i
    model->nodes.resize(1 + L.branches.size());
    model->nodes[0].name = path;
    model->nodes[0].local = axes::fromYUpMatrix();
    model->nodes[0].children.push_back(1);
    for (size_t i = 0; i < L.branches.size(); i++) {
        const Branch &b = L.branches[i];
        Mat4 m;
        for (int ti : b.trans) {
            if (ti < 0 || ti >= (int)L.transforms.size())
                continue;
            const Transform &t = L.transforms[ti];
            switch (t.type) {
            case 0: m = m * rotate(radians(t.angle / 10.0f), {1, 0, 0}); break;
            case 1: m = m * rotate(radians(t.angle / 10.0f), {0, 1, 0}); break;
            case 2: m = m * rotate(radians(t.angle / 10.0f), {0, 0, 1}); break;
            case 3: m = m * translate(t.v); break;
            case 4: m = m * scale(t.v); break;
            }
        }
        // filled after: model->nodes may grow (the views of an eight view node)
        model->nodes[1 + i].name = "B" + std::to_string(i);
        model->nodes[1 + i].local = m;
        model->nodes[1 + i].mask = b.state & ~kCull;
    }
    for (size_t i = 0; i < L.branches.size(); i++) {
        const Branch &b = L.branches[i];
        size_t first = (b.state & kCull) ? 1 : 0; // a culling volume, not drawn
        bool eight = (b.mode & kEightView) != 0;
        if (eight)
            model->nodes[1 + i].kind = NodeKind::Switch, model->nodes[1 + i].switchValue = 0;
        for (size_t k = first; k < b.children.size(); k++) {
            auto [isGeom, index] = b.children[k];
            if (!isGeom) {
                if (index >= 0 && index < (int)L.branches.size())
                    model->nodes[1 + i].children.push_back(1 + index);
                continue;
            }
            if (index < 0 || index >= (int)L.geoms.size())
                continue;
            std::vector<int> ms = L.meshesOf(index, b.mode & 0xff);
            if (eight) {
                Node view;
                view.name = "G" + std::to_string(index);
                view.meshes = ms;
                model->nodes[1 + i].children.push_back((int)model->nodes.size());
                model->nodes.push_back(view);
            } else {
                auto &meshes = model->nodes[1 + i].meshes;
                meshes.insert(meshes.end(), ms.begin(), ms.end());
            }
        }
    }
    // the plane of flight: body, landing gear, weapons; not far, not shadows
    model->defaultState = 0xff03;
    return model;
}

} // namespace r4::assets
