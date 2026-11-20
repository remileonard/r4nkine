// yaodl_loader.cpp - the YAODL objects of powerflip and flip (.ydl, .defs), Y-up.
// A port of src/powerflip/yaodl.c whose IRIS GL calls are recorded into meshes; NURBS are tessellated without their trimming curves, the texture generation is not reproduced.
#include "r4/axes.h"
#include "r4/assets/model.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

namespace r4::assets {
namespace {

struct Yobj;
using YobjP = std::shared_ptr<Yobj>;

enum ContourMode {
    CONTOUR_UNKNOWN = -1,
    CONTOUR_OFF = 0,
    CONTOUR_LINEAR = 1,
    CONTOUR_SPHERE_MAP = 2
};

struct Recorder {
    Model &model;
    std::string dir;
    std::vector<Mat4> stack{Mat4()};
    Vec3 normal{0, 0, 1};
    bool haveNormal = false;
    Vec4 color{1, 1, 1, 1};
    Vec2 uv;
    int texture = -1; // an index in model.images
    int unlit = 0;    // nesting of lmbind(LMODEL, 0)
    // s and t generation: 0 off, 1 an object plane, 2 the sphere map
    struct Contour {
        int mode = CONTOUR_OFF;
        float plane[4] = {0, 0, 0, 0};
    };
    const Contour *contour[2] = {nullptr, nullptr};
    bool sphereMap() const {
        return contour[0] && contour[0]->mode == CONTOUR_SPHERE_MAP;
    }
    std::map<std::string, int> textureByName;

    enum Kind {
        None,
        Polygon,
        Line,
        ClosedLine,
        Strip
    };
    Kind kind = None;
    std::vector<Vertex> prim;
    std::vector<bool> primNormal;

    std::map<std::tuple<int, bool, int, bool>, Mesh> meshes;

    explicit Recorder(Model &m) : model(m) {
    }

    Mat4 &top() {
        return stack.back();
    }
    void push() {
        stack.push_back(stack.back());
    }
    void pop() {
        if (stack.size() > 1) {
            stack.pop_back();
        }
    }

    void begin(Kind k) {
        kind = k;
        prim.clear();
        primNormal.clear();
    }

    void vertex(Vec3 p) {
        if (kind == None) {
            return;
        }
        Vertex v;
        v.position = transformPoint(top(), p);
        v.normal = normalize(transformVector(normalMatrix(top()), normal));
        v.uv = uv;
        for (int c = 0; c < 2; c++) {
            if (contour[c] && contour[c]->mode == CONTOUR_LINEAR) {
                const float *pl = contour[c]->plane;
                (c ? v.uv.y : v.uv.x) = pl[0] * p.x + pl[1] * p.y + pl[2] * p.z + pl[3];
            }
        }
        auto byte = [](float f) {
            return (uint32_t)(std::fmin(std::fmax(f, 0.0f), 1.0f) * 255.0f + 0.5f);
        };
        v.color = byte(color.x) | byte(color.y) << 8 | byte(color.z) << 16 | byte(color.w) << 24;
        prim.push_back(v);
        primNormal.push_back(haveNormal);
    }

    Mesh &mesh(bool lit, Primitive p) {
        auto key = std::make_tuple(texture, lit, (int)p, texture >= 0 && sphereMap());
        Mesh &m = meshes[key];
        m.primitive = p;
        return m;
    }

    void end() {
        Kind k = kind;
        kind = None;
        size_t n = prim.size();
        if (k == Polygon || k == Strip) {
            if (n < 3) {
                return;
            }
            bool any = false;
            for (bool b : primNormal) {
                any = any || b;
            }
            if (!any) {
                Vec3 fn = normalize(cross(prim[1].position - prim[0].position, prim[2].position - prim[0].position));
                for (Vertex &v : prim) {
                    v.normal = fn;
                }
            }
            Mesh &m = mesh(unlit == 0, Primitive::TRIANGLES);
            uint32_t base = (uint32_t)m.vertices.size();
            m.vertices.insert(m.vertices.end(), prim.begin(), prim.end());
            for (uint32_t c = 1; c + 1 < n; c++) {
                if (k == Polygon) {
                    m.indices.insert(m.indices.end(), {base, base + c, base + c + 1});
                } else if (c % 2) {
                    m.indices.insert(m.indices.end(), {base + c - 1, base + c, base + c + 1});
                } else {
                    m.indices.insert(m.indices.end(), {base + c, base + c - 1, base + c + 1});
                }
            }
        } else if (k == Line || k == ClosedLine) {
            if (n < 2) {
                return;
            }
            bool lit = unlit == 0;
            for (bool b : primNormal) {
                lit = lit && b;
            }
            Mesh &m = mesh(lit, Primitive::LINES);
            uint32_t base = (uint32_t)m.vertices.size();
            m.vertices.insert(m.vertices.end(), prim.begin(), prim.end());
            for (uint32_t c = 0; c + 1 < n; c++) {
                m.indices.insert(m.indices.end(), {base + c, base + c + 1});
            }
            if (k == ClosedLine && n > 2) {
                m.indices.insert(m.indices.end(), {base + (uint32_t)n - 1, base});
            }
        }
    }

    // searched as given, in the folder of the model, then by its name in that folder
    int loadTexture(const std::string &name) {
        auto it = textureByName.find(name);
        if (it != textureByName.end()) {
            return it->second;
        }
        Image img;
        std::string err;
        bool ok = loadImage(name, img, &err);
        if (!ok && !dir.empty() && name[0] != '/' && name[0] != '\\') {
            ok = loadImage(dir + name, img, &err);
        }
        if (!ok) {
            size_t slash = name.find_last_of("/\\");
            if (slash != std::string::npos) {
                ok = loadImage(dir + name.substr(slash + 1), img, &err);
            }
        }
        int index = -1;
        if (ok) {
            index = (int)model.images.size();
            model.images.push_back(std::move(img));
        } else {
            std::fprintf(stderr, "r4: YAODL: cannot load the texture %s, drawn without it\n", name.c_str());
        }
        textureByName[name] = index;
        return index;
    }
};

enum class Cls {
    FLOAT,
    INT,
    STRING,
    GROUP,
    LINES,
    POLYGONS,
    INDEX_POLYGONS,
    INDICES,
    VERTICES,
    NORMALS,
    COLORS,
    COLORS4F,
    TRANSLATES,
    SCALES,
    ROTATES,
    UNTRANSFORM,
    NO_FLIP_LIGHTING,
    Textures,
    TexCoords,
    Contours,
    Subdivides,
    RegularMesh,
    Nurbs,
    TrimCurves,
    Unknown
};

struct Surface {
    std::vector<double> sknots;
    std::vector<double> tknots;
    std::vector<double> points;
    int ns = 0;
    int nt = 0;
    int size = 3;
};

struct Yobj {
    Cls cls = Cls::Unknown;
    std::string className;
    int count = 0;
    std::vector<YobjP> assoc;
    std::vector<float> f;
    std::vector<int> iv;
    int width = 0;
    std::string str;
    std::vector<YobjP> kids;
    std::vector<std::vector<int>> polys; // indices
    std::vector<Surface> surfaces;
    std::vector<Recorder::Contour> contours;
    int flag = 0;
    int saved = 0;
    const Recorder::Contour *savedContour = nullptr;
};

bool isData(const Yobj &o) {
    return o.cls == Cls::FLOAT || o.cls == Cls::INT || o.cls == Cls::STRING;
}
int clampIndex(int i, int n) {
    return i < n ? i : n - 1;
}

struct Exec {
    Recorder &r;
    int currentTexture = 0; // 1 + image index, 0: none (as texbind)

    void listPush(std::vector<YobjP> &l, bool full) {
        for (auto &a : l) {
            push(*a, full);
        }
    }
    void listPop(std::vector<YobjP> &l, bool full) {
        for (size_t i = l.size(); i-- > 0;) {
            pop(*l[i], full);
        }
    }
    void listExecuteN(std::vector<YobjP> &l, int i) {
        for (auto &a : l) {
            listPush(a->assoc, true);
            listExecuteN(a->assoc, i);
            executeN(*a, i);
            listPop(a->assoc, true);
        }
    }
    void executeElements(Yobj &o) {
        listPush(o.assoc, true);
        for (int i = 0; i < o.count; i++) {
            if (i) {
                listPop(o.assoc, false);
                listPush(o.assoc, false);
            }
            listExecuteN(o.assoc, i);
            executeN(o, i);
        }
        listPop(o.assoc, true);
    }

    const float *element(Yobj &o, int i) {
        return o.f.data() + o.width * clampIndex(i, o.count);
    }

    void push(Yobj &o, bool full) {
        switch (o.cls) {
        case Cls::GROUP:
        case Cls::LINES:
        case Cls::POLYGONS:
        case Cls::INDEX_POLYGONS:
        case Cls::RegularMesh:
            for (auto &k : o.kids) {
                push(*k, full);
            }
            break;
        case Cls::TRANSLATES:
        case Cls::SCALES:
        case Cls::ROTATES:
            r.push();
            break;
        case Cls::UNTRANSFORM:
            if (full) {
                r.push();
                r.top() = Mat4();
            }
            break;
        case Cls::NO_FLIP_LIGHTING:
            r.unlit++;
            break;
        case Cls::Textures:
            if (currentTexture) {
                o.flag = 1;
            }
            if (full) {
                o.saved = currentTexture;
            }
            break;
        case Cls::Contours:
            if (full) {
                o.savedContour = r.contour[o.className == "tContours"];
            }
            break;
        default:
            break;
        }
    }

    void pop(Yobj &o, bool full) {
        switch (o.cls) {
        case Cls::GROUP:
        case Cls::LINES:
        case Cls::POLYGONS:
        case Cls::INDEX_POLYGONS:
        case Cls::RegularMesh:
            for (auto &k : o.kids) {
                pop(*k, full);
            }
            break;
        case Cls::TRANSLATES:
        case Cls::SCALES:
        case Cls::ROTATES:
            r.pop();
            break;
        case Cls::UNTRANSFORM:
            if (full) {
                r.pop();
            }
            break;
        case Cls::NO_FLIP_LIGHTING:
            r.unlit--;
            break;
        case Cls::Textures:
            if (o.flag || !full) {
                return;
            }
            if (o.saved != currentTexture) {
                currentTexture = o.saved;
                r.texture = currentTexture - 1;
            }
            break;
        case Cls::Contours:
            if (full) {
                r.contour[o.className == "tContours"] = o.savedContour;
            }
            break;
        default:
            break;
        }
    }

    void executeN(Yobj &o, int i) {
        switch (o.cls) {
        case Cls::VERTICES: {
            const float *v = element(o, i);
            r.vertex({v[0], v[1], v[2]});
            break;
        }
        case Cls::NORMALS: {
            const float *v = element(o, i);
            r.normal = {v[0], v[1], v[2]};
            r.haveNormal = true;
            break;
        }
        case Cls::COLORS: {
            const float *v = element(o, i);
            r.color = {v[0], v[1], v[2], 1};
            break;
        }
        case Cls::COLORS4F: {
            const float *v = element(o, i);
            r.color = {v[0], v[1], v[2], v[3]};
            break;
        }
        case Cls::TRANSLATES: {
            const float *t = element(o, i);
            r.top() = r.top() * translate({t[0], t[1], t[2]});
            break;
        }
        case Cls::SCALES: {
            const float *s = element(o, i);
            r.top() = r.top() * scale({s[0], s[1], s[2]});
            break;
        }
        case Cls::ROTATES: {
            const float *a = element(o, i);
            if (a[1] != 0 || a[2] != 0 || a[3] != 0) {
                r.top() = r.top() * rotate(radians(a[0]), {a[1], a[2], a[3]});
            }
            break;
        }
        case Cls::GROUP:
            execute(*o.kids[i]);
            break;
        case Cls::LINES:
            r.begin(Recorder::Line);
            execute(*o.kids[i]);
            r.end();
            break;
        case Cls::POLYGONS:
            r.begin(Recorder::Polygon);
            execute(*o.kids[i]);
            r.end();
            break;
        case Cls::INDEX_POLYGONS: {
            r.begin(Recorder::Polygon);
            Yobj &verts = *o.kids[0];
            Yobj &ind = *o.kids[1];
            listPush(verts.assoc, true);
            const std::vector<int> &poly = ind.polys[i];
            for (size_t j = 0; j < poly.size(); j++) {
                int v = poly[j];
                if (v < 0 || v >= verts.count) {
                    continue;
                }
                if (j) {
                    listPop(verts.assoc, false);
                    listPush(verts.assoc, false);
                }
                listExecuteN(verts.assoc, v);
                executeN(verts, v);
            }
            listPop(verts.assoc, true);
            r.end();
            break;
        }
        case Cls::Textures:
            if (o.flag || o.count == 0) {
                return;
            }
            currentTexture = o.iv[clampIndex(i, o.count)];
            r.texture = currentTexture - 1;
            break;
        case Cls::TexCoords:
            if (currentTexture) {
                const float *t = element(o, i);
                r.uv = {t[0], t[1]};
            }
            break;
        case Cls::Nurbs:
            nurbs(o.surfaces[clampIndex(i, (int)o.surfaces.size())]);
            break;
        case Cls::Contours:
            if (!o.contours.empty()) {
                const Recorder::Contour *c = &o.contours[clampIndex(i, (int)o.contours.size())];
                r.contour[o.className == "tContours"] = c->mode ? c : nullptr;
            }
            break;
        default:
            break;
        }
    }

    void execute(Yobj &o) {
        switch (o.cls) {
        case Cls::LINES: {
            bool lit = false;
            for (auto &a : o.assoc) {
                lit = lit || a->cls == Cls::NORMALS;
            }
            for (auto &k : o.kids) {
                for (auto &a : k->assoc) {
                    lit = lit || a->cls == Cls::NORMALS;
                }
            }
            if (!lit) {
                r.unlit++;
            }
            executeElements(o);
            if (!lit) {
                r.unlit--;
            }
            break;
        }
        case Cls::RegularMesh: {
            listPush(o.assoc, true);
            listExecuteN(o.assoc, 0);
            Yobj &verts = *o.kids[0];
            int w = o.width;
            int h = o.iv.empty() ? 0 : o.iv[0];
            listPush(verts.assoc, true);
            auto meshVertex = [&](int v, bool first) {
                if (!first) {
                    listPop(verts.assoc, false);
                    listPush(verts.assoc, false);
                }
                listExecuteN(verts.assoc, v);
                executeN(verts, v);
            };
            for (int row = 0; row + 1 < h; row++) {
                r.begin(Recorder::Strip);
                for (int c = 0; c < w; c++) {
                    meshVertex(row * w + c, c == 0);
                    meshVertex((row + 1) * w + c, false);
                }
                r.end();
            }
            listPop(verts.assoc, true);
            listPop(o.assoc, true);
            break;
        }
        case Cls::Nurbs: {
            listPush(o.assoc, true);
            for (int i = 0; i < o.count; i++) {
                executeN(o, i);
            }
            listPop(o.assoc, true);
            break;
        }
        default:
            executeElements(o);
        }
    }

    static double basis(const std::vector<double> &knots, int order, int i, double u) {
        std::vector<double> N(order);
        for (int j = 0; j < order; j++) {
            double a = knots[i + j];
            double b = knots[i + j + 1];
            N[j] = (u >= a && u < b) || (u == knots.back() && u > a && u <= b) ? 1 : 0;
        }
        for (int k = 2; k <= order; k++) {
            for (int j = 0; j + k - 1 < order; j++) {
                double left = 0;
                double right = 0;
                double d1 = knots[i + j + k - 1] - knots[i + j];
                double d2 = knots[i + j + k] - knots[i + j + 1];
                if (d1 > 0) {
                    left = (u - knots[i + j]) / d1 * N[j];
                }
                if (d2 > 0) {
                    right = (knots[i + j + k] - u) / d2 * N[j + 1];
                }
                N[j] = left + right;
            }
        }
        return N[0];
    }

    void nurbs(const Surface &s) {
        int os = (int)s.sknots.size() - s.ns;
        int ot = (int)s.tknots.size() - s.nt;
        if (os < 1 || ot < 1) {
            return;
        }
        double s0 = s.sknots[os - 1];
        double s1 = s.sknots[s.ns];
        double t0 = s.tknots[ot - 1];
        double t1 = s.tknots[s.nt];
        const int SN = std::max(8, (s.ns - os + 1) * 6);
        const int TN = std::max(8, (s.nt - ot + 1) * 6);
        std::vector<Vec3> grid((SN + 1) * (TN + 1));
        std::vector<double> bs(s.ns);
        std::vector<double> bt(s.nt);
        for (int b = 0; b <= TN; b++) {
            double t = t0 + (t1 - t0) * b / TN;
            for (int j = 0; j < s.nt; j++) {
                bt[j] = basis(s.tknots, ot, j, t);
            }
            for (int a = 0; a <= SN; a++) {
                double u = s0 + (s1 - s0) * a / SN;
                for (int i = 0; i < s.ns; i++) {
                    bs[i] = basis(s.sknots, os, i, u);
                }
                double x = 0;
                double y = 0;
                double z = 0;
                double w = 0;
                for (int j = 0; j < s.nt; j++) {
                    for (int i = 0; i < s.ns; i++) {
                        double k = bs[i] * bt[j];
                        if (k == 0) {
                            continue;
                        }
                        const double *p = &s.points[(j * s.ns + i) * s.size];
                        double pw = s.size == 4 ? p[3] : 1;
                        x += k * p[0];
                        y += k * p[1];
                        z += k * p[2];
                        w += k * (s.size == 4 ? pw : 1);
                    }
                }
                if (w == 0) {
                    w = 1;
                }
                grid[b * (SN + 1) + a] = {(float)(x / w), (float)(y / w), (float)(z / w)};
            }
        }
        auto at = [&](int a, int b) {
            return grid[b * (SN + 1) + a];
        };
        bool saved = r.haveNormal;
        for (int b = 0; b < TN; b++) {
            r.begin(Recorder::Strip);
            for (int a = 0; a <= SN; a++) {
                for (int k = 0; k < 2; k++) {
                    int bb = b + k;
                    Vec3 du = at(std::min(a + 1, SN), bb) - at(std::max(a - 1, 0), bb);
                    Vec3 dv = at(a, std::min(bb + 1, TN)) - at(a, std::max(bb - 1, 0));
                    r.normal = normalize(cross(du, dv));
                    r.haveNormal = true;
                    r.uv = {(float)a / SN, (float)bb / TN};
                    r.vertex(at(a, bb));
                }
            }
            r.end();
        }
        r.haveNormal = saved;
    }
};

struct Parser {
    const unsigned char *s;
    long len;
    long pos = 0;
    int line = 1;
    std::string error;
    std::vector<std::pair<std::string, YobjP>> defs;
    Recorder &rec;
    explicit Parser(Recorder &r) : s(nullptr), len(0), rec(r) {
    }

    void fail(const std::string &msg) {
        if (error.empty()) {
            error = "line " + std::to_string(line) + ": " + msg;
        }
    }

    void skip(bool commas) {
        while (pos < len) {
            int c = s[pos];
            if (c == '\n') {
                line++;
                pos++;
            } else if (c == '#') {
                while (pos < len && s[pos] != '\n') {
                    pos++;
                }
            } else if (c == '/' && pos + 1 < len && s[pos + 1] == '*') {
                pos += 2;
                while (pos + 1 < len && !(s[pos] == '*' && s[pos + 1] == '/')) {
                    if (s[pos] == '\n') {
                        line++;
                    }
                    pos++;
                }
                pos += 2;
            } else if (std::isspace(c) || (commas && c == ',')) {
                pos++;
            } else {
                break;
            }
        }
    }
    int peek(bool commas) {
        skip(commas);
        return pos < len ? s[pos] : -1;
    }
    static bool wordStart(int c) {
        return std::isalpha(c) || c == '_' || c == '/';
    }
    static bool wordChar(int c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '/' || c == '-';
    }
    bool isNumber() {
        long q = pos;
        if (q < len && (s[q] == '-' || s[q] == '+')) {
            q++;
        }
        if (q < len && s[q] == '.') {
            q++;
        }
        return q < len && std::isdigit(s[q]);
    }
    std::string word() {
        std::string w;
        while (pos < len && wordChar(s[pos])) {
            w += (char)s[pos++];
        }
        return w;
    }
    YobjP findDef(const std::string &name) {
        for (size_t i = defs.size(); i-- > 0;) {
            if (defs[i].first == name) {
                return defs[i].second;
            }
        }
        return nullptr;
    }

    YobjP numbers() {
        std::vector<double> v;
        bool isFloat = false;
        for (;;) {
            skip(false);
            if (pos >= len || !isNumber()) {
                break;
            }
            std::string b;
            while (pos < len) {
                int c = s[pos];
                if (!(std::isdigit(c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) {
                    break;
                }
                if (c == '.' || c == 'e' || c == 'E') {
                    isFloat = true;
                }
                b += (char)c;
                pos++;
            }
            v.push_back(std::atof(b.c_str()));
        }
        auto o = std::make_shared<Yobj>();
        if (isFloat) {
            o->cls = Cls::FLOAT;
            for (double d : v) {
                o->f.push_back((float)d);
            }
        } else {
            o->cls = Cls::INT;
            for (double d : v) {
                o->iv.push_back((int)d);
            }
        }
        o->count = (int)v.size();
        return o;
    }

    static uint32_t be32(const unsigned char *b) {
        return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    }

    YobjP binary() {
        std::string type;
        pos++;
        while (pos < len && s[pos] != 0) {
            if (!std::isspace(s[pos])) {
                type += (char)s[pos];
            }
            pos++;
        }
        pos++;
        if (pos + 4 > len) {
            fail("truncated binary block");
            return nullptr;
        }
        uint32_t size = be32(s + pos);
        pos += 4;
        if (size > (uint32_t)(len - pos)) {
            fail("truncated binary block");
            return nullptr;
        }
        const unsigned char *d = s + pos;
        pos += size;
        int n = (int)(size / 4);
        auto o = std::make_shared<Yobj>();
        if (type == "float") {
            o->cls = Cls::FLOAT;
            o->f.resize(n);
            for (int i = 0; i < n; i++) {
                uint32_t u = be32(d + 4 * i);
                std::memcpy(&o->f[i], &u, 4);
            }
            o->count = n;
            return o;
        }
        std::vector<int> iv(n);
        for (int i = 0; i < n; i++) {
            iv[i] = (int)be32(d + 4 * i);
        }
        if (type == "int") {
            o->cls = Cls::INT;
            o->iv = iv;
            o->count = n;
            return o;
        }
        if (type == "indices") {
            o->cls = Cls::INDICES;
            o->className = "indices";
            if (n < 1 || iv[0] < 0 || 1 + iv[0] > n) {
                fail("bad binary indices");
                return nullptr;
            }
            int at = 1 + iv[0];
            for (int k = 0; k < iv[0]; k++) {
                int sz = iv[1 + k];
                if (sz < 0 || at + sz > n) {
                    fail("bad binary indices");
                    return nullptr;
                }
                o->polys.emplace_back(iv.begin() + at, iv.begin() + at + sz);
                at += sz;
            }
            o->count = iv[0];
            return o;
        }
        std::fprintf(stderr, "r4: YAODL: no binary form for %s\n", type.c_str());
        return nullptr;
    }

    YobjP create(const std::string &name, std::vector<YobjP> &args);

    YobjP object(const std::string &name, bool bare) {
        std::vector<YobjP> args;
        std::vector<YobjP> assoc;
        bool attributes = false;
        for (;;) {
            int c = peek(!bare);
            if (c < 0) {
                if (!bare) {
                    fail("unexpected end of file");
                }
                break;
            }
            if (c == ')') {
                if (!bare) {
                    pos++;
                }
                break;
            }
            if (bare && c == ',') {
                break;
            }
            if (c == ':') {
                pos++;
                attributes = true;
                continue;
            }
            YobjP a = item(attributes);
            if (a) {
                (attributes ? assoc : args).push_back(a);
            }
            if (!error.empty()) {
                return nullptr;
            }
        }
        if (!error.empty()) {
            return nullptr;
        }
        YobjP o = create(name, args);
        if (!o) {
            fail("bad arguments of " + name);
            return nullptr;
        }
        o->assoc.insert(o->assoc.end(), assoc.begin(), assoc.end());
        return o;
    }

    YobjP item(bool bareOk) {
        int c = peek(true);
        if (c < 0) {
            return nullptr;
        }
        if (c == '(') {
            pos++;
            skip(true);
            std::string w = word();
            if (w.empty()) {
                fail("syntax error");
                return nullptr;
            }
            return object(w, false);
        }
        if (c == '@') {
            return binary();
        }
        if (isNumber()) {
            return numbers();
        }
        if (c == '"') {
            auto o = std::make_shared<Yobj>();
            o->cls = Cls::STRING;
            long start = ++pos;
            while (pos < len && s[pos] != '"') {
                pos++;
            }
            o->str.assign((const char *)s + start, pos - start);
            o->count = 1;
            pos++;
            return o;
        }
        if (wordStart(c)) {
            std::string w = word();
            if (peek(false) == '=') {
                pos++;
                YobjP d = item(true);
                if (d) {
                    defs.emplace_back(w, d);
                }
                return nullptr;
            }
            if (YobjP d = findDef(w)) {
                return d;
            }
            if (bareOk) {
                return object(w, true);
            }
            auto o = std::make_shared<Yobj>();
            o->cls = Cls::STRING;
            o->str = w;
            o->count = 1;
            return o;
        }
        fail("syntax error");
        return nullptr;
    }

    void file(std::vector<YobjP> &objs) {
        for (;;) {
            int c = peek(true);
            if (c < 0 || !error.empty()) {
                return;
            }
            if (wordStart(c)) {
                long save = pos;
                int saveLine = line;
                std::string name = word();
                if (peek(false) == '=') {
                    pos++;
                    YobjP d = item(true);
                    if (d) {
                        defs.emplace_back(name, d);
                    }
                    continue;
                }
                pos = save;
                line = saveLine;
            }
            if (c == ':') {
                pos++;
                YobjP o = item(true);
                if (o && !objs.empty()) {
                    objs.back()->assoc.push_back(o);
                }
                continue;
            }
            YobjP o = item(true);
            if (o && !isData(*o)) {
                objs.push_back(o);
            }
        }
    }
};

YobjP Parser::create(const std::string &name, std::vector<YobjP> &args) {
    auto o = std::make_shared<Yobj>();
    o->className = name;
    auto floats = [&](Cls cls, int width) -> YobjP {
        o->cls = cls;
        o->width = width;
        for (auto &a : args) {
            if (a->cls == Cls::FLOAT) {
                o->f.insert(o->f.end(), a->f.begin(), a->f.end());
            } else if (a->cls == Cls::INT) {
                for (int v : a->iv) {
                    o->f.push_back((float)v);
                }
            } else {
                return nullptr;
            }
        }
        if (o->f.empty() || o->f.size() % width) {
            return nullptr;
        }
        o->count = (int)o->f.size() / width;
        return o;
    };
    auto group = [&](Cls cls, const char *kidClass) -> YobjP {
        o->cls = cls;
        for (auto &a : args) {
            if (kidClass && a->className != kidClass) {
                return nullptr;
            }
            if (!isData(*a)) {
                o->kids.push_back(a);
            }
        }
        o->count = (int)o->kids.size();
        return o;
    };
    if (name == "group") {
        return group(Cls::GROUP, nullptr);
    }
    if (name == "lines") {
        return group(Cls::LINES, "vertices");
    }
    if (name == "polygons") {
        return group(Cls::POLYGONS, "vertices");
    }
    if (name == "vertices") {
        return floats(Cls::VERTICES, 3);
    }
    if (name == "normals") {
        return floats(Cls::NORMALS, 3);
    }
    if (name == "colors") {
        return floats(Cls::COLORS, 3);
    }
    if (name == "colors4f") {
        return floats(Cls::COLORS4F, 4);
    }
    if (name == "translates") {
        return floats(Cls::TRANSLATES, 3);
    }
    if (name == "scales") {
        return floats(Cls::SCALES, 3);
    }
    if (name == "rotates") {
        return floats(Cls::ROTATES, 4);
    }
    if (name == "texcoords") {
        return floats(Cls::TexCoords, 2);
    }
    if (name == "indexpolygons") {
        if (args.size() != 2 || args[0]->className != "vertices" || args[1]->className != "indices") {
            return nullptr;
        }
        o->cls = Cls::INDEX_POLYGONS;
        o->kids = args;
        o->count = args[1]->count;
        return o;
    }
    if (name == "indices") {
        o->cls = Cls::INDICES;
        for (auto &a : args) {
            if (a->cls != Cls::INT) {
                return nullptr;
            }
            o->polys.push_back(a->iv);
        }
        o->count = (int)o->polys.size();
        return o;
    }
    if (name == "untransform" || name == "nofliplighting" || name == "subdivides") {
        o->cls = name == "untransform" ? Cls::UNTRANSFORM : name == "nofliplighting" ? Cls::NO_FLIP_LIGHTING : Cls::Subdivides;
        o->count = 1;
        return o;
    }
    if (name == "textures") {
        o->cls = Cls::Textures;
        for (auto &a : args) {
            if (a->cls != Cls::STRING) {
                return nullptr;
            }
            o->iv.push_back(rec.loadTexture(a->str) + 1);
        }
        o->count = (int)o->iv.size();
        return o;
    }
    if (name == "sContours" || name == "tContours") {
        o->cls = Cls::Contours;
        size_t k = 0;
        while (k < args.size()) {
            Recorder::Contour c;
            if (args[k]->cls != Cls::STRING) {
                return nullptr;
            }
            const std::string &m = args[k++]->str;
            c.mode = m == "contour" || m == "linear" ? CONTOUR_LINEAR : m == "spheremap" ? CONTOUR_SPHERE_MAP : m == "off" ? CONTOUR_OFF : CONTOUR_UNKNOWN;
            if (c.mode == CONTOUR_UNKNOWN || k >= args.size() || args[k]->cls != Cls::STRING) {
                return nullptr;
            }
            k++;
            if (c.mode == CONTOUR_LINEAR) {
                if (k >= args.size()) {
                    return nullptr;
                }
                const Yobj &pl = *args[k++];
                size_t n = pl.cls == Cls::FLOAT ? pl.f.size() : pl.iv.size();
                if (n != 4) {
                    return nullptr;
                }
                for (int j = 0; j < 4; j++) {
                    c.plane[j] = pl.cls == Cls::FLOAT ? pl.f[j] : (float)pl.iv[j];
                }
            }
            o->contours.push_back(c);
        }
        o->count = (int)o->contours.size();
        return o;
    }
    if (name == "regularMesh") {
        if (args.size() != 2 || args[0]->cls != Cls::INT || args[0]->iv.size() != 2 || args[1]->className != "vertices") {
            return nullptr;
        }
        if (args[0]->iv[0] * args[0]->iv[1] != args[1]->count) {
            return nullptr;
        }
        o->cls = Cls::RegularMesh;
        o->kids = {args[1]};
        o->width = args[0]->iv[0];
        o->iv = {args[0]->iv[1]};
        o->count = 1;
        return o;
    }
    if (name == "nurbs") {
        if (args.empty() || args.size() % 4) {
            return nullptr;
        }
        o->cls = Cls::Nurbs;
        auto doubles = [](const Yobj &l) {
            std::vector<double> d;
            if (l.cls == Cls::FLOAT) {
                d.assign(l.f.begin(), l.f.end());
            } else {
                d.assign(l.iv.begin(), l.iv.end());
            }
            return d;
        };
        for (size_t k = 0; k < args.size(); k += 4) {
            Surface sf;
            if (args[k + 2]->cls != Cls::INT || args[k + 2]->iv.size() != 2) {
                return nullptr;
            }
            sf.sknots = doubles(*args[k]);
            sf.tknots = doubles(*args[k + 1]);
            sf.ns = args[k + 2]->iv[0];
            sf.nt = args[k + 2]->iv[1];
            sf.points = doubles(*args[k + 3]);
            if (sf.ns <= 0 || sf.nt <= 0 || sf.points.size() % (size_t)(sf.ns * sf.nt)) {
                return nullptr;
            }
            sf.size = (int)(sf.points.size() / (size_t)(sf.ns * sf.nt));
            if ((sf.size != 3 && sf.size != 4) || (int)sf.sknots.size() <= sf.ns || (int)sf.tknots.size() <= sf.nt) {
                return nullptr;
            }
            o->surfaces.push_back(std::move(sf));
        }
        o->count = (int)o->surfaces.size();
        return o;
    }
    if (name == "trimcurves") {
        o->cls = Cls::TrimCurves;
        return o;
    }
    o->cls = Cls::Unknown;
    o->kids = args;
    return o;
}

} // namespace

std::shared_ptr<Model> loadYAODL(const std::string &path, std::string *error) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error) {
            *error = "cannot read " + path;
        }
        return nullptr;
    }
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> buf(len > 0 ? (size_t)len : 0);
    bool ok = len >= 0 && std::fread(buf.data(), 1, buf.size(), f) == buf.size();
    std::fclose(f);
    if (!ok) {
        if (error) {
            *error = "cannot read " + path;
        }
        return nullptr;
    }
    auto model = std::make_shared<Model>();
    model->source = path;
    Recorder rec(*model);
    size_t slash = path.find_last_of("/\\");
    rec.dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    Parser p(rec);
    p.s = buf.data();
    p.len = (long)buf.size();
    std::vector<YobjP> objs;
    p.file(objs);
    if (!p.error.empty() || objs.empty()) {
        if (error) {
            *error = path + ": " + (p.error.empty() ? std::string("nothing to draw") : p.error);
        }
        return nullptr;
    }
    Exec ex{rec};
    for (auto &o : objs) {
        ex.execute(*o);
    }

    Node root;
    root.name = path;
    root.local = axes::fromYUpMatrix();
    std::map<std::tuple<int, bool, bool>, int> materials;
    for (auto &[key, mesh] : rec.meshes) {
        auto [texture, lit, prim, sphere] = key;
        (void)prim;
        if (mesh.indices.empty()) {
            continue;
        }
        auto mk = std::make_tuple(texture, lit, sphere);
        auto it = materials.find(mk);
        if (it == materials.end()) {
            Material m;
            m.name = std::string(lit ? "lit" : "unlit") + (texture >= 0 ? " textured" : "");
            m.shading = lit ? Shading::PHONG : Shading::UNLIT;
            m.diffuse = {1, 1, 1, 1};
            m.ambient = {1, 1, 1};
            m.vertexColors = true;
            m.doubleSided = true;
            m.diffuseImage = texture;
            m.sphereMap = sphere;
            it = materials.emplace(mk, (int)model->materials.size()).first;
            model->materials.push_back(m);
        }
        mesh.material = it->second;
        for (const Vertex &v : mesh.vertices) {
            if ((v.color >> 24) < 255) {
                model->materials[mesh.material].alpha = AlphaMode::BLEND;
                break;
            }
        }
        mesh.computeBounds();
        root.meshes.push_back((int)model->meshes.size());
        model->meshes.push_back(std::move(mesh));
    }
    if (model->meshes.empty()) {
        if (error) {
            *error = path + ": nothing to draw";
        }
        return nullptr;
    }
    model->nodes.push_back(std::move(root));
    return model;
}

} // namespace r4::assets
