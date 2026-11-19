//
//  TerrainDiag.cpp
//
#include "TerrainDiag.h"

#include "engine/Texture.h"
#include "realspace/RSArea.h"
#include "realspace/RSEntity.h"
#include "realspace/RSImage.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

bool writePpm(const std::string &path, int w, int h, const std::vector<Rgb> &px) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(px.data(), 3, px.size(), f);
    std::fclose(f);
    return true;
}

constexpr int kSide = BLOCK_PER_MAP_SIDE * 20; // les sommets de LVL0 sur un côté
constexpr int kCell = 3;                       // les pixels d'une cellule dans textures.ppm

} // namespace

bool terrainDiagnostic(RSArea *area, const VGAPalette &palette, const std::vector<PaletteRange> &ranges,
                       const std::string &dir, std::string &error) {
    if (!area) {
        error = "pas de terrain";
        return false;
    }
    // le sommet (x, y) de la carte, x vers l'est, y vers le sud (z de libRealSpace)
    auto vertex = [&](int x, int y) -> MapVertex * {
        AreaBlock *b = area->GetAreaBlockByCoo(0, x / 20, y / 20);
        if (!b || b->sideSize != 20)
            return nullptr;
        return b->GetVertice(x % 20, y % 20);
    };

    // la couleur d'un type : le ton le plus clair de sa rampe (l'eau bleue,
    // le sable, la roche, le vert) ; magenta hors de la palette
    auto rampColor = [&](int t) -> Rgb {
        if (t >= (int)ranges.size())
            return {255, 0, 255};
        const Texel &c = palette.colors[std::min(255, ranges[t].first + ranges[t].shades)];
        return {c.r, c.g, c.b};
    };
    std::vector<Rgb> terrain(kSide * kSide), types(kSide * kSide);
    std::map<int, size_t> typeCount;
    for (int y = 0; y < kSide; y++)
        for (int x = 0; x < kSide; x++) {
            MapVertex *v = vertex(x, y);
            if (!v)
                continue;
            Rgb &c = terrain[y * kSide + x];
            c.r = (uint8_t)std::clamp(v->color[0] * 255.0f, 0.0f, 255.0f);
            c.g = (uint8_t)std::clamp(v->color[1] * 255.0f, 0.0f, 255.0f);
            c.b = (uint8_t)std::clamp(v->color[2] * 255.0f, 0.0f, 255.0f);
            types[y * kSide + x] = rampColor(v->type);
            typeCount[v->type]++;
        }

    // les images : leur couleur moyenne (sans les texels clairs), leur usage
    struct Use {
        size_t triangles = 0;
        std::map<int, size_t> onType; // le type de terrain du sommet de la cellule
        std::vector<std::pair<int, int>> cells; // les premières cellules (sommets de LVL0)
        Rgb mean;
        int width = 0, height = 0;
        std::string name;
        bool loaded = false;
    };
    std::map<int, Use> uses;
    auto useOf = [&](int id) -> Use & {
        Use &u = uses[id];
        if (!u.loaded) {
            u.loaded = true;
            RSImage *img = area->GetImageByID(id);
            if (img && img->data && img->width && img->height) {
                u.width = (int)img->width;
                u.height = (int)img->height;
                for (int i = 0; i < 8 && img->name[i] >= 32 && img->name[i] < 127; i++)
                    u.name += img->name[i];
                double r = 0, g = 0, b = 0;
                size_t n = 0;
                std::vector<Rgb> px((size_t)u.width * u.height);
                for (size_t i = 0; i < px.size(); i++) {
                    uint8_t k = img->data[i];
                    const Texel &t = palette.colors[k];
                    px[i] = {t.r, t.g, t.b};
                    if (k == 255) {
                        px[i] = {255, 0, 255}; // clair : en magenta
                        continue;
                    }
                    r += t.r, g += t.g, b += t.b, n++;
                }
                if (n)
                    u.mean = {(uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(b / n)};
                writePpm(dir + "/tex_" + std::to_string(id) + ".ppm", u.width, u.height, px);
            }
        }
        return u;
    };

    const int side = kSide * kCell;
    std::vector<Rgb> textures((size_t)side * side);
    for (int y = 0; y + 1 < kSide; y++)
        for (int x = 0; x + 1 < kSide; x++) {
            MapVertex *v = vertex(x, y);
            if (!v)
                continue;
            // la cellule de RSArea : le triangle bas (le sommet, en bas à droite,
            // en bas) et le triangle haut (le sommet, à droite, en bas à droite)
            for (int half = 0; half < 2; half++) {
                int id = half == 0 ? v->lowerImageID : v->upperImageID;
                if (id == 0xFF)
                    continue;
                Use &u = useOf(id);
                u.triangles++;
                u.onType[v->type]++;
                if (u.cells.size() < 6)
                    u.cells.push_back({x, y});
                for (int j = 0; j < kCell; j++)
                    for (int i = 0; i < kCell; i++)
                        if ((half == 0) == (j >= i))
                            textures[(size_t)(y * kCell + j) * side + x * kCell + i] = u.mean;
            }
        }

    // all the textures of the game (TXMPACK then ACCPACK, the same on every
    // map): all_<id>.ppm, clear texels in magenta
    for (int id = 0; id < 1024; id++) {
        RSImage *img = area->GetImageByID(id);
        if (!img || !img->data || !img->width || !img->height)
            continue;
        std::vector<Rgb> px(img->width * img->height);
        for (size_t i = 0; i < px.size(); i++) {
            const Texel &t = palette.colors[img->data[i]];
            px[i] = img->data[i] == 255 ? Rgb{255, 0, 255} : Rgb{t.r, t.g, t.b};
        }
        writePpm(dir + "/all_" + std::to_string(id) + ".ppm", (int)img->width, (int)img->height, px);
    }
    if (!writePpm(dir + "/terrain.ppm", kSide, kSide, terrain) || !writePpm(dir + "/types.ppm", kSide, kSide, types) ||
        !writePpm(dir + "/textures.ppm", side, side, textures)) {
        error = "écriture impossible dans " + dir;
        return false;
    }

    FILE *f = std::fopen((dir + "/types.txt").c_str(), "w");
    if (f) {
        std::fprintf(f, "type  sommets  rampe (first shades)  ton le plus clair (types.ppm)\n");
        for (auto &[t, n] : typeCount) {
            int first = t < (int)ranges.size() ? ranges[t].first : -1;
            int shades = t < (int)ranges.size() ? ranges[t].shades : 0;
            Texel top = palette.colors[std::min(255, std::max(0, first + shades))];
            std::fprintf(f, "%4d  %7zu  %3d %3d               #%02x%02x%02x\n", t, n, first, shades, top.r, top.g,
                         top.b);
        }
        std::fclose(f);
    }
    f = std::fopen((dir + "/textures.txt").c_str(), "w");
    if (f) {
        std::fprintf(f, "id   nom           taille   triangles  moyenne   sur les types (sommets)\n");
        for (auto &[id, u] : uses) {
            std::fprintf(f, "%-4d %-12s %3dx%-3d  %9zu  #%02x%02x%02x  ", id, u.name.c_str(), u.width, u.height,
                         u.triangles, u.mean.r, u.mean.g, u.mean.b);
            for (auto &[t, n] : u.onType)
                std::fprintf(f, " %d:%zu", t, n);
            // où : la cellule (x, y) de LVL0, et le point de libRealSpace (x, z)
            std::fprintf(f, "   à");
            for (auto &[x, y] : u.cells) {
                MapVertex *v = vertex(x, y);
                std::fprintf(f, " (%d,%d)=%.0f,%.0f", x, y, v->v.x, v->v.z);
            }
            std::fprintf(f, "\n");
        }
        std::fclose(f);
    }
    return true;
}
