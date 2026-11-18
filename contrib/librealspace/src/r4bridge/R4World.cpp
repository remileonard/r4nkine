//
//  R4World.cpp
//  libRealSpace
//
//  The terrain and the places of the objects of a world. See R4World.h.
//
#include "R4World.h"

#include "../realspace/RSArea.h"
#include "../realspace/RSImage.h"
#include "../realspace/RSMission.h"
#include "../engine/Texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace r4bridge {

namespace {

uint32_t packColor(const float c[4]) {
    auto b = [](float v) { return (uint32_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f); };
    return b(c[0]) | b(c[1]) << 8 | b(c[2]) << 16 | b(c[3]) << 24;
}

} // namespace

std::shared_ptr<r4::assets::Model> terrainModel(RSArea *area, const VGAPalette &palette,
                                               const std::vector<float> &lodRanges, const TerrainLight &light) {
    using namespace r4::assets;
    if (!area || !area->GetAreaBlockByID(0, 0))
        return nullptr;
    const bool dynamic = light.dynamic && light.ranges && !light.ranges->empty();
    auto model = std::make_shared<Model>();
    model->source = "terrain";

    // the colors of the triangles, both faces (the game finds the winding of
    // its terrain at run time):
    // - baked: as RSArea made them, with the light of the data (unlit);
    // - dynamic: the ramp of the terrain type of each triangle (RSArea's
    //   terrainTone), lit by the sun through the normals (PaletteRamp)
    Material colors;
    colors.name = "terrain colors";
    colors.doubleSided = true;
    if (dynamic) {
        model->images.push_back(rampImage(palette, *light.ranges));
        colors.shading = Shading::PaletteRamp;
        colors.diffuseImage = 0;
        colors.rampAmbient = light.lighting.ambient;
        colors.rampLight = light.lighting.light;
    } else {
        colors.shading = Shading::Unlit;
        colors.vertexColors = true;
        colors.diffuse = {1, 1, 1, 1};
    }
    model->materials.push_back(colors);
    // dynamic: the brightest tone of the ramp of a terrain type, as
    // RSArea::terrainTone(type, 255); the ramp of the PaletteRamp gives the
    // tones under it by the intensity
    auto topTone = [&](uint8_t type) -> uint32_t {
        const std::vector<PaletteRange> &r = *light.ranges;
        const PaletteRange &range = type < r.size() ? r[type] : r[0];
        return (uint32_t)std::min(255, range.first + range.shades) | 0xff000000u;
    };
    // the terrain type of a triangle: the one of its highest vertex (RSArea::highestVertex)
    auto typeOf = [](const MapVertex *a, const MapVertex *b, const MapVertex *c) {
        const MapVertex *h = a;
        if ((uint16_t)b->height >= (uint16_t)h->height)
            h = b;
        if ((uint16_t)c->height >= (uint16_t)h->height)
            h = c;
        return h->type;
    };

    // a material by texture of the map, made at its first triangle
    std::map<int, int> textureMaterial;
    auto textured = [&](int imageID) {
        auto it = textureMaterial.find(imageID);
        if (it != textureMaterial.end())
            return it->second;
        int material = -1;
        RSImage *src = area->GetImageByID(imageID);
        if (src && src->data && src->width && src->height) {
            Image img;
            img.name = std::string(src->name);
            img.width = (int)src->width;
            img.height = (int)src->height;
            img.channels = 4;
            img.pixels.resize((size_t)img.width * img.height * 4);
            for (int y = 0; y < img.height; y++)
                for (int x = 0; x < img.width; x++) {
                    // the palette of the game (PALETTE.IFF): the one SCRenderer::init
                    // gives the images (RSImage takes Renderer.getPalette()); its
                    // alpha, as Texture::updateContent: index 255 clear
                    const Texel &t = palette.colors[src->data[(size_t)y * img.width + x]];
                    // R4nkine's images start at the bottom row
                    uint8_t *d = &img.pixels[((size_t)(img.height - 1 - y) * img.width + x) * 4];
                    d[0] = t.r, d[1] = t.g, d[2] = t.b, d[3] = t.a;
                }
            // as SCRenderer::createTextureInGPU: GL_LINEAR, no mipmaps, clamped
            bleedClearTexels(img);
            img.mipmaps = false;
            img.clampS = img.clampT = true;
            model->images.push_back(std::move(img));
            Material m;
            m.name = std::string("terrain ") + src->name;
            m.shading = Shading::Unlit;
            if (dynamic) { // the texture times the intensity of the sun
                m.shading = Shading::PaletteRamp;
                m.rampTextured = true;
                m.rampAmbient = light.lighting.ambient;
                m.rampLight = light.lighting.light;
            }
            m.diffuseImage = (int)model->images.size() - 1;
            // the GL_DECAL of the game: mix(color, texel, alpha of the texel),
            // the colored triangle under it giving the color
            m.alpha = AlphaMode::Blend;
            m.decal = true;
            m.doubleSided = true;
            model->materials.push_back(m);
            material = (int)model->materials.size() - 1;
        }
        return textureMaterial[imageID] = material;
    };

    const float kSkirt = 600; // the depth of the skirts of the blocks
    const int last = BLOCK_PER_MAP_SIDE - 1;
    // the cells of a block at a level, then its seams with the blocks on its
    // right and below at the same level, as SCRenderer::renderBlock
    auto forEachCell = [&](int level, int i, const auto &fn) {
        AreaBlock *b = area->GetAreaBlockByID(level, i);
        if (!b || b->sideSize < 2)
            return;
        int side = b->sideSize;
        for (int x = 0; x < side - 1; x++)
            for (int y = 0; y < side - 1; y++)
                fn(b->GetVertice(x, y), b->GetVertice(x + 1, y), b->GetVertice(x + 1, y + 1), b->GetVertice(x, y + 1));
        AreaBlock *right = i % BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(level, i + 1) : nullptr;
        AreaBlock *below = i / BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(level, i + BLOCK_PER_MAP_SIDE) : nullptr;
        if (right)
            for (int y = 0; y < side - 1; y++)
                fn(b->GetVertice(side - 1, y), right->GetVertice(0, y), right->GetVertice(0, y + 1),
                   b->GetVertice(side - 1, y + 1));
        if (below)
            for (int x = 0; x < side - 1; x++)
                fn(b->GetVertice(x, side - 1), b->GetVertice(x + 1, side - 1), below->GetVertice(x + 1, 0),
                   below->GetVertice(x, 0));
        AreaBlock *corner = right && below ? area->GetAreaBlockByID(level, i + 1 + BLOCK_PER_MAP_SIDE) : nullptr;
        if (corner)
            fn(b->GetVertice(side - 1, side - 1), right->GetVertice(0, side - 1), corner->GetVertice(0, 0),
               below->GetVertice(side - 1, 0));
    };
    // dynamic: the normals of the vertices of each level, from all the cells
    // around them (across the blocks), pointing up
    std::vector<std::unordered_map<const MapVertex *, r4::Vec3>> normals(NUM_LODS);
    if (dynamic)
        for (int level = 0; level < NUM_LODS; level++) {
            auto &n = normals[level];
            auto face = [&](const MapVertex *a, const MapVertex *b, const MapVertex *c) {
                const float k = light.relief;
                r4::Vec3 A{a->v.x, a->v.y * k, a->v.z}, B{b->v.x, b->v.y * k, b->v.z}, C{c->v.x, c->v.y * k, c->v.z};
                r4::Vec3 f = r4::cross(B - A, C - A);
                if (f.y < 0)
                    f = -f;
                n[a] += f, n[b] += f, n[c] += f;
            };
            for (int i = 0; i < BLOCKS_PER_MAP; i++)
                forEachCell(level, i, [&](const MapVertex *cur, const MapVertex *right, const MapVertex *br,
                                          const MapVertex *bottom) {
                    face(cur, br, bottom);
                    face(cur, right, br);
                });
            for (auto &[v, nv] : n)
                nv = r4::length(nv) > 0 ? r4::normalize(nv) : r4::Vec3{0, 1, 0};
        }
    int currentLevel = 0;
    // the meshes of a level of a block, by material
    std::map<int, Mesh> meshes;
    auto add = [&](int material, const MapVertex *v, uint32_t color, r4::Vec2 uv, const MapVertex *normalOf = nullptr) {
        Mesh &m = meshes[material];
        Vertex out;
        out.position = {v->v.x, v->v.y, v->v.z};
        out.normal = {0, 1, 0};
        if (dynamic) {
            auto it = normals[currentLevel].find(normalOf ? normalOf : v);
            if (it != normals[currentLevel].end())
                out.normal = it->second;
        }
        out.uv = uv;
        out.color = color;
        m.vertices.push_back(out);
        m.indices.push_back((uint32_t)m.vertices.size() - 1);
    };
    // a triangle: its colors, and its texture over them when it has one
    auto triangle = [&](const MapVertex *a, const MapVertex *b, const MapVertex *c, const float (*cols)[4],
                        uint16_t imageID, const float (*uv)[2]) {
        const MapVertex *corner[3] = {a, b, c};
        if (dynamic) {
            uint32_t tone = topTone(typeOf(a, b, c));
            for (int k = 0; k < 3; k++)
                add(0, corner[k], tone, {0, 0});
        } else {
            for (int k = 0; k < 3; k++)
                add(0, corner[k], packColor(cols[k]), {0, 0});
        }
        if (imageID == 0xFF)
            return;
        int material = textured(imageID);
        if (material < 0)
            return;
        for (int k = 0; k < 3; k++)
            add(material, corner[k], 0xffffffffu, {uv[k][0], 1 - uv[k][1]});
    };
    // a cell, as SCRenderer::renderQuad: the lower triangle, then the upper one
    auto quad = [&](const MapVertex *current, const MapVertex *right, const MapVertex *bottomRight,
                    const MapVertex *bottom) {
        triangle(current, bottomRight, bottom, current->lowerColors, current->lowerImageID, current->lowerUV);
        triangle(current, right, bottomRight, current->upperColors, current->upperImageID, current->upperUV);
    };
    // a node of the meshes made since the last one
    auto takeMeshes = [&](const std::string &name) {
        Node node;
        node.name = name;
        for (auto &[material, mesh] : meshes) {
            mesh.material = material;
            mesh.name = model->materials[material].name;
            mesh.computeBounds();
            node.meshes.push_back((int)model->meshes.size());
            model->meshes.push_back(std::move(mesh));
        }
        meshes.clear();
        return node;
    };

    // the root, a group of the blocks; each block a LOD over its 3 levels
    model->nodes.push_back(Node{});
    model->nodes[0].name = "terrain";
    for (int i = 0; i < BLOCKS_PER_MAP; i++) {
        Node block;
        block.name = "block " + std::to_string(i % BLOCK_PER_MAP_SIDE) + "," + std::to_string(i / BLOCK_PER_MAP_SIDE);
        Node lod;
        lod.name = "levels of detail";
        lod.kind = NodeKind::LOD;
        lod.lodRanges = lodRanges;
        std::vector<Node> levels;
        for (int level = 0; level < NUM_LODS; level++) {
            currentLevel = level;
            AreaBlock *b = area->GetAreaBlockByID(level, i);
            if (b && b->sideSize >= 2) {
                // the cells of the block, then its seams with the blocks on its
                // right and below at the same level, as SCRenderer::renderBlock
                int side = b->sideSize;
                for (int x = 0; x < side - 1; x++)
                    for (int y = 0; y < side - 1; y++)
                        quad(b->GetVertice(x, y), b->GetVertice(x + 1, y), b->GetVertice(x + 1, y + 1),
                             b->GetVertice(x, y + 1));
                AreaBlock *right = i % BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(level, i + 1) : nullptr;
                AreaBlock *below =
                    i / BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(level, i + BLOCK_PER_MAP_SIDE) : nullptr;
                if (right)
                    for (int y = 0; y < side - 1; y++)
                        quad(b->GetVertice(side - 1, y), right->GetVertice(0, y), right->GetVertice(0, y + 1),
                             b->GetVertice(side - 1, y + 1));
                if (below)
                    for (int x = 0; x < side - 1; x++)
                        quad(b->GetVertice(x, side - 1), b->GetVertice(x + 1, side - 1), below->GetVertice(x + 1, 0),
                             below->GetVertice(x, 0));
                AreaBlock *corner = right && below ? area->GetAreaBlockByID(level, i + 1 + BLOCK_PER_MAP_SIDE) : nullptr;
                if (corner)
                    quad(b->GetVertice(side - 1, side - 1), right->GetVertice(0, side - 1), corner->GetVertice(0, 0),
                         below->GetVertice(side - 1, 0));
                // the skirts: walls down from the edges of the block, so that a
                // neighbor at another level leaves no crack (not in the game,
                // which draws all its blocks at one level)
                auto at = [&](int x, int y) -> const MapVertex * {
                    // the vertex x, y of the block and its seams (x, y up to side)
                    AreaBlock *src = b;
                    if (x == side && y == side)
                        src = corner, x = 0, y = 0;
                    else if (x == side)
                        src = right, x = 0;
                    else if (y == side)
                        src = below, y = 0;
                    return src ? src->GetVertice(x, y) : nullptr;
                };
                int endX = right ? side : side - 1, endY = below ? side : side - 1;
                auto skirt = [&](const MapVertex *a, const MapVertex *c) {
                    if (!a || !c)
                        return;
                    MapVertex da = *a, dc = *c;
                    da.v.y -= kSkirt;
                    dc.v.y -= kSkirt;
                    uint32_t ca = dynamic ? topTone(a->type) : packColor(a->color);
                    uint32_t cc = dynamic ? topTone(c->type) : packColor(c->color);
                    add(0, a, ca, {0, 0}), add(0, c, cc, {0, 0}), add(0, &dc, cc, {0, 0}, c);
                    add(0, a, ca, {0, 0}), add(0, &dc, cc, {0, 0}, c), add(0, &da, ca, {0, 0}, a);
                };
                for (int x = 0; x < endX; x++) {
                    skirt(at(x, 0), at(x + 1, 0));
                    skirt(at(x, endY), at(x + 1, endY));
                }
                for (int y = 0; y < endY; y++) {
                    skirt(at(0, y), at(0, y + 1));
                    skirt(at(endX, y), at(endX, y + 1));
                }
            }
            levels.push_back(takeMeshes("LVL" + std::to_string(level) + " (" +
                                        std::to_string(b ? b->sideSize * b->sideSize : 0) + " vertices)"));
        }
        // the nodes: the block, its LOD, the levels
        int blockIndex = (int)model->nodes.size();
        model->nodes.push_back(block);
        model->nodes[0].children.push_back(blockIndex);
        int lodIndex = (int)model->nodes.size();
        model->nodes.push_back(lod);
        model->nodes[blockIndex].children.push_back(lodIndex);
        for (Node &l : levels) {
            model->nodes.push_back(l);
            model->nodes[lodIndex].children.push_back((int)model->nodes.size() - 1);
        }
    }
    if (model->meshes.empty())
        return nullptr;
    return model;
}

std::shared_ptr<r4::assets::Model> overlayModel(RSArea *area, const VGAPalette &palette) {
    using namespace r4::assets;
    if (!area || area->objectOverlay.empty())
        return nullptr;
    auto model = std::make_shared<Model>();
    model->source = "overlays";

    // the images of the overlays in one atlas, so that the triangles of an
    // overlay make one mesh, drawn in the order of its drawList: cell 0
    // white (the colored triangles), then a cell by textured color c (the
    // image c + 255, its clear texels in the palette color c: the GL_DECAL
    // of the game); 2 texels of border by cell, its edges repeated (the
    // GL_CLAMP_TO_EDGE of the game)
    const int kBorder = 2, kCell = 128 + 2 * kBorder;
    std::map<int, int> cellOf; // color -> cell
    std::vector<RSImage *> cellImage{nullptr};
    std::vector<int> cellColor{-1};
    for (const AreaOverlay &ov : area->objectOverlay)
        for (uint16_t j : ov.drawList) {
            if (j >= 400 || !ov.vertices)
                continue;
            int c = ov.trianles[j].color;
            if (c == 255 || cellOf.count(c))
                continue;
            RSImage *src = area->GetImageByID(c + 255);
            if (!src || !src->data || !src->width || !src->height || src->width > 128 || src->height > 128)
                continue;
            cellOf[c] = (int)cellImage.size();
            cellImage.push_back(src);
            cellColor.push_back(c);
        }
    int columns = (int)std::ceil(std::sqrt((double)cellImage.size()));
    int rows = ((int)cellImage.size() + columns - 1) / columns;
    Image atlas;
    atlas.name = "overlay atlas";
    atlas.width = columns * kCell;
    atlas.height = rows * kCell;
    atlas.channels = 4;
    atlas.mipmaps = false;
    atlas.clampS = atlas.clampT = true;
    std::vector<uint8_t> top((size_t)atlas.width * atlas.height * 4, 255); // rows from the top
    for (size_t cell = 0; cell < cellImage.size(); cell++) {
        int x0 = (int)(cell % columns) * kCell, y0 = (int)(cell / columns) * kCell;
        RSImage *src = cellImage[cell];
        const Texel &under = palette.colors[cellColor[cell] & 0xff];
        int w = src ? (int)src->width : 1, h = src ? (int)src->height : 1;
        for (int y = -kBorder; y < kCell - kBorder; y++)
            for (int x = -kBorder; x < kCell - kBorder; x++) {
                uint8_t *d = &top[((size_t)(y0 + kBorder + y) * atlas.width + x0 + kBorder + x) * 4];
                if (!src) {
                    d[0] = d[1] = d[2] = d[3] = 255;
                    continue;
                }
                int sx = std::clamp(x, 0, w - 1), sy = std::clamp(y, 0, h - 1);
                const Texel &t = palette.colors[src->data[(size_t)sy * w + sx]];
                const Texel &o = t.a ? t : under;
                d[0] = o.r, d[1] = o.g, d[2] = o.b, d[3] = 255;
            }
    }
    atlas.pixels.resize(top.size());
    for (int y = 0; y < atlas.height; y++) // R4nkine's images start at the bottom row
        std::memcpy(&atlas.pixels[(size_t)(atlas.height - 1 - y) * atlas.width * 4], &top[(size_t)y * atlas.width * 4],
                    (size_t)atlas.width * 4);
    model->images.push_back(std::move(atlas));
    const float atlasW = (float)(columns * kCell), atlasH = (float)(rows * kCell);
    // an uv of a cell (in its image, from its top left) in the atlas
    auto atlasUV = [&](int cell, float u, float v) {
        float px = (cell % columns) * kCell + kBorder + u, py = (cell / columns) * kCell + kBorder + v;
        return r4::Vec2{px / atlasW, 1 - py / atlasH};
    };

    // over the terrain (drawn after it), without depth: each triangle over
    // the ones before it, as the GL_LEQUAL of SCRenderer::renderMapOverlay
    Material m;
    m.name = "overlays";
    m.shading = Shading::Unlit;
    m.vertexColors = true;
    m.diffuse = {1, 1, 1, 1};
    m.diffuseImage = 0;
    m.doubleSided = true;
    m.decal = true;
    m.alpha = AlphaMode::Blend; // opaque texels: with the transparent ones, without depth
    m.drawOrder = 1;
    model->materials.push_back(m);

    const float kTriUvScale = 128.0f; // the uvs of the .TRI: 0..125, 128 a whole tile
    Node root;
    root.name = "overlays";
    for (const AreaOverlay &ov : area->objectOverlay) {
        if (!ov.vertices || ov.nbTriangles <= 0)
            continue;
        Mesh mesh;
        mesh.name = "overlay";
        mesh.material = 0;
        for (uint16_t j : ov.drawList) {
            if (j >= 400)
                continue;
            const AreaOverlayTriangles &tri = ov.trianles[j];
            auto it = cellOf.find(tri.color);
            int cell = tri.color != 255 && it != cellOf.end() ? it->second : 0;
            const Texel &t = palette.colors[tri.color & 0xff];
            uint32_t color = cell == 0 ? (uint32_t)t.r | (uint32_t)t.g << 8 | (uint32_t)t.b << 16 | 0xff000000u
                                       : 0xffffffffu;
            RSImage *img = cellImage[cell];
            for (int k = 0; k < 3; k++) {
                const AoVPoints &p = ov.vertices[tri.verticesIdx[k]];
                Vertex v;
                v.position = {(float)p.x, (float)p.y, (float)p.z};
                v.normal = {0, 1, 0};
                // the uv of the game (uv / 128 of the image) in texels of its cell
                v.uv = img ? atlasUV(cell, tri.uv[k].u / kTriUvScale * img->width,
                                     tri.uv[k].v / kTriUvScale * img->height)
                           : atlasUV(0, 64, 64);
                v.color = color;
                mesh.vertices.push_back(v);
                mesh.indices.push_back((uint32_t)mesh.vertices.size() - 1);
            }
        }
        if (mesh.indices.empty())
            continue;
        mesh.computeBounds();
        root.meshes.push_back((int)model->meshes.size());
        model->meshes.push_back(std::move(mesh));
    }
    if (model->meshes.empty())
        return nullptr;
    model->nodes.push_back(root);
    return model;
}

r4::Mat4 mapObjectMatrix(const MapObject &object) {
    // the columns given to glMultMatrixf by SCRenderer::renderObjects
    const float (*o)[3] = object.orientation;
    r4::Mat4 rotation(1.0f);
    rotation[0] = r4::Vec4(o[1][0], o[1][2], -o[1][1], 0);
    rotation[1] = r4::Vec4(o[2][0], o[2][2], -o[2][1], 0);
    rotation[2] = r4::Vec4(o[0][0], o[0][2], -o[0][1], 0);
    return r4::translate({object.position.x, object.position.y, object.position.z}) * rotation;
}

r4::Mat4 missionPartMatrix(const MISN_PART &part) {
    // the glRotatef of SCRenderer::renderMissionObjects, in degrees
    return r4::translate({part.position.x, part.position.y, part.position.z}) *
           r4::rotate(r4::radians(360.0f - (float)part.azymuth + 90.0f), {0, 1, 0}) *
           r4::rotate(r4::radians((float)part.pitch), {0, 0, 1}) *
           r4::rotate(r4::radians(-(float)part.roll), {1, 0, 0});
}

// --- the forests ------------------------------------------------------------------------

namespace {

// a number of 0..1 from integers: the same at each load
float hash01(uint32_t a, uint32_t b, uint32_t seed) {
    uint32_t h = a * 0x8da6b343u ^ b * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xffffff) / (float)0x1000000;
}

// a smooth noise of 0..1 over the map: the woods and the clearings
float valueNoise(float x, float z, float scale, uint32_t seed) {
    float fx = x / scale, fz = z / scale;
    int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
    float tx = fx - ix, tz = fz - iz;
    tx = tx * tx * (3 - 2 * tx);
    tz = tz * tz * (3 - 2 * tz);
    auto at = [&](int i, int j) { return hash01((uint32_t)i, (uint32_t)j, seed); };
    float a = at(ix, iz) + (at(ix + 1, iz) - at(ix, iz)) * tx;
    float b = at(ix, iz + 1) + (at(ix + 1, iz + 1) - at(ix, iz + 1)) * tx;
    return a + (b - a) * tz;
}

// a small random generator, for the trees of a triangle
struct Random {
    uint32_t state;
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (state & 0xffffff) / (float)0x1000000;
    }
};

} // namespace

namespace {

constexpr int kMapSide = BLOCK_PER_MAP_SIDE * 20; // the vertices of LVL0 on a side
constexpr float kCell = BLOCK_WIDTH / 20.0f;      // between two of them
constexpr int kGreen = 3, kWater = 0;
// the density at the heart of a wood, and between the woods, x perKm2
constexpr float kWoodCore = 25, kScattered = 0.1f;

const MapVertex *mapVertex(RSArea *area, int x, int y) {
    if (x < 0 || y < 0 || x >= kMapSide || y >= kMapSide)
        return nullptr;
    AreaBlock *b = area->GetAreaBlockByCoo(0, x / 20, y / 20);
    if (!b || b->sideSize != 20)
        return nullptr;
    return b->GetVertice(x % 20, y % 20);
}

// the type of a triangle: the one of its highest vertex (RSArea::highestVertex)
const MapVertex *highest(const MapVertex *a, const MapVertex *b, const MapVertex *c) {
    const MapVertex *h = a;
    if ((uint16_t)b->height >= (uint16_t)h->height)
        h = b;
    if ((uint16_t)c->height >= (uint16_t)h->height)
        h = c;
    return h;
}

bool green(const MapVertex *a, const MapVertex *b, const MapVertex *c) {
    return highest(a, b, c)->type == kGreen && a->type != kWater && b->type != kWater && c->type != kWater;
}

// each cell of a chunk: its 4 corners, the lower triangle then the upper
// one, as RSArea
template <class F> void forEachTriangle(RSArea *area, int cx, int cz, int cellsPerChunk, F f) {
    for (int y = cz * cellsPerChunk; y < (cz + 1) * cellsPerChunk; y++)
        for (int x = cx * cellsPerChunk; x < (cx + 1) * cellsPerChunk; x++) {
            const MapVertex *cur = mapVertex(area, x, y), *right = mapVertex(area, x + 1, y),
                            *bottomRight = mapVertex(area, x + 1, y + 1), *bottom = mapVertex(area, x, y + 1);
            if (!cur || !right || !bottomRight || !bottom)
                continue;
            uint32_t key = (uint32_t)(y * kMapSide + x) * 2;
            f(cur, bottomRight, bottom, (int)cur->lowerImageID, key);
            f(cur, right, bottomRight, (int)cur->upperImageID, key + 1);
        }
}

int cellsPerChunk(const ForestSettings &st) { return std::max(1, (int)std::lround(st.chunk / kCell)); }

} // namespace

std::vector<ForestChunk> forestChunks(RSArea *area, const ForestSettings &st) {
    std::vector<ForestChunk> chunks;
    if (!area)
        return chunks;
    const int cells = cellsPerChunk(st);
    const int perSide = (kMapSide - 1 + cells - 1) / cells;
    const float half = BLOCK_WIDTH * BLOCK_PER_MAP_SIDE_DIV_2;
    for (int cz = 0; cz < perSide; cz++)
        for (int cx = 0; cx < perSide; cx++) {
            bool any = false;
            float low = 1e30f, high = -1e30f;
            forEachTriangle(area, cx, cz, cells, [&](const MapVertex *a, const MapVertex *b, const MapVertex *c, int, uint32_t) {
                if (!green(a, b, c))
                    return;
                any = true;
                for (const MapVertex *v : {a, b, c})
                    low = std::min(low, v->v.y), high = std::max(high, v->v.y);
            });
            if (!any)
                continue;
            ForestChunk chunk;
            chunk.cx = cx;
            chunk.cz = cz;
            float side = cells * kCell;
            float trees = 40 * st.treeScale; // the tallest tree
            chunk.center = {-half + (cx + 0.5f) * side, (low + high + trees) / 2, -half + (cz + 0.5f) * side};
            float dy = (high + trees - low) / 2;
            chunk.radius = std::sqrt(side * side / 2 + dy * dy);
            chunks.push_back(chunk);
        }
    return chunks;
}

void plantForest(RSArea *area, const ForestSettings &st, ForestChunk &chunk) {
    chunk.trees[0].clear();
    chunk.trees[1].clear();
    if (!area)
        return;
    // the boxes of the runways, and their surroundings
    struct Box {
        float x0, z0, x1, z1;
    };
    std::vector<Box> runways;
    for (const AreaOverlay &ov : area->objectOverlay) {
        if (ov.verticesVec.empty())
            continue;
        Box b{1e30f, 1e30f, -1e30f, -1e30f};
        for (const AoVPoints &p : ov.verticesVec) {
            b.x0 = std::min(b.x0, (float)p.x), b.x1 = std::max(b.x1, (float)p.x);
            b.z0 = std::min(b.z0, (float)p.z), b.z1 = std::max(b.z1, (float)p.z);
        }
        const float margin = 300;
        runways.push_back({b.x0 - margin, b.z0 - margin, b.x1 + margin, b.z1 + margin});
    }
    auto free = [&](float x, float z) {
        for (const Box &b : runways)
            if (x > b.x0 && x < b.x1 && z > b.z0 && z < b.z1)
                return false;
        for (const r4::Vec3 &k : st.keepOut)
            if ((x - k.x) * (x - k.x) + (z - k.y) * (z - k.y) < k.z * k.z)
                return false;
        return true;
    };
    forEachTriangle(area, chunk.cx, chunk.cz, cellsPerChunk(st),
                    [&](const MapVertex *a, const MapVertex *b, const MapVertex *c, int image, uint32_t key) {
        if (!green(a, b, c))
            return;
        auto it = st.images.find(image);
        bool woodImage = image != 0xFF && it != st.images.end();
        r4::Vec3 pa{a->v.x, a->v.y, a->v.z}, pb{b->v.x, b->v.y, b->v.z}, pc{c->v.x, c->v.y, c->v.z};
        float areaKm2 = 0.5f * std::abs((pb.x - pa.x) * (pc.z - pa.z) - (pc.x - pa.x) * (pb.z - pa.z)) / 1e6f;
        Random rnd{key * 2654435761u + st.seed * 97u + 1};
        // the candidates at the highest density, kept by the density of their place
        float top = (woodImage ? it->second : st.perKm2 * kWoodCore) * st.density;
        float expected = top * areaKm2;
        int n = (int)expected + (rnd.next() < expected - (int)expected ? 1 : 0);
        for (int i = 0; i < n; i++) {
            // a point of the triangle, uniform
            float u = rnd.next(), v = rnd.next();
            if (u + v > 1)
                u = 1 - u, v = 1 - v;
            r4::Vec3 p = pa + (pb - pa) * u + (pc - pa) * v;
            float keep = rnd.next(), species = rnd.next(), turn = rnd.next(), size = rnd.next();
            // the woods, where the noise is high (perKm2 x kWoodCore at their
            // heart), and a few trees alone between them; the textures of
            // woods: their density everywhere
            if (!woodImage) {
                float wood = valueNoise(p.x, p.z, 2500, st.seed) * 0.7f + valueNoise(p.x, p.z, 600, st.seed + 7) * 0.3f;
                float share = std::clamp((wood - 0.6f) * 5.0f, 0.0f, 1.0f) + kScattered / kWoodCore;
                if (keep > share)
                    continue;
            }
            if (!free(p.x, p.z))
                continue;
            // conifers on the higher ground
            int sp = species < 0.35f + std::clamp((p.y - 500) / 3000, 0.0f, 0.5f) ? 0 : 1;
            r4::Mat4 m = r4::translate(p) * r4::rotate(turn * 6.2831853f, {0, 1, 0});
            float k = (0.7f + size * 0.6f) * st.treeScale;
            m = m * r4::scale({k, k * (0.85f + species * 0.3f), k});
            chunk.trees[sp].push_back(m);
        }
    });
}

std::shared_ptr<r4::assets::Model> treeModel(int species, bool far, const VGAPalette &palette,
                                            const std::vector<PaletteRange> &ranges, Lighting lighting) {
    using namespace r4::assets;
    auto model = std::make_shared<Model>();
    const std::string name = std::string(species == 0 ? "conifer" : "broadleaf") + (far ? " far" : "");
    model->images.push_back(rampImage(palette, ranges));
    Material m;
    m.name = "tree, palette ramp";
    m.shading = Shading::PaletteRamp;
    m.diffuseImage = 0;
    m.rampAmbient = lighting.ambient;
    m.rampLight = lighting.light;
    model->materials.push_back(m);
    Mesh mesh;
    mesh.name = name;
    mesh.material = 0;
    // the colors at full light: their ramps darken them (the green ramp from
    // 64, the brown one from 96)
    const uint32_t conifer = 64 + 13, leaves = 64 + 18, trunk = 96 + 10;
    // a triangle with its face normal, turned outward from the center of its solid
    auto tri = [&](r4::Vec3 a, r4::Vec3 b, r4::Vec3 c, r4::Vec3 center, uint32_t color) {
        r4::Vec3 n = r4::cross(b - a, c - a);
        if (r4::dot(n, (a + b + c) / 3.0f - center) < 0) {
            std::swap(b, c);
            n = -n;
        }
        n = r4::normalize(n);
        for (r4::Vec3 p : {a, b, c}) {
            Vertex v;
            v.position = p;
            v.normal = n;
            v.color = (color & 0xff) | 0xff000000u;
            mesh.vertices.push_back(v);
            mesh.indices.push_back((uint32_t)mesh.vertices.size() - 1);
        }
    };
    auto ring = [](int sides, float r, float y, float turn) {
        std::vector<r4::Vec3> p;
        for (int i = 0; i < sides; i++) {
            float a = (i + turn) * 6.2831853f / sides;
            p.push_back({r * std::cos(a), y, r * std::sin(a)});
        }
        return p;
    };
    // a solid of rings from the bottom to the top (a ring of radius 0: a point)
    auto solid = [&](const std::vector<std::pair<float, float>> &rings, int sides, uint32_t color, float turn = 0) {
        r4::Vec3 center{0, (rings.front().second + rings.back().second) / 2, 0};
        for (size_t k = 0; k + 1 < rings.size(); k++) {
            auto lo = ring(sides, rings[k].first, rings[k].second, turn);
            auto hi = ring(sides, rings[k + 1].first, rings[k + 1].second, turn);
            for (int i = 0; i < sides; i++) {
                int j = (i + 1) % sides;
                if (rings[k].first > 0)
                    tri(lo[i], lo[j], hi[j], center, color);
                if (rings[k + 1].first > 0)
                    tri(lo[i], hi[j], hi[i], center, color);
            }
        }
        // the bottom, when it is not a point
        if (rings.front().first > 0) {
            auto lo = ring(sides, rings.front().first, rings.front().second, turn);
            for (int i = 1; i + 1 < sides; i++)
                tri(lo[0], lo[i], lo[i + 1], center, color);
        }
    };
    if (species == 0) {
        if (far) {
            solid({{5.5f, 2}, {0, 25}}, 4, conifer, 0.5f);
        } else {
            solid({{0.7f, 0}, {0.7f, 5}}, 5, trunk);
            solid({{6, 4}, {0, 17}}, 7, conifer);
            solid({{4.5f, 11}, {0, 25}}, 7, conifer, 0.5f);
        }
    } else {
        if (far) {
            solid({{0, 5}, {7, 13}, {0, 21}}, 4, leaves);
        } else {
            solid({{0.9f, 0}, {0.8f, 8}}, 5, trunk);
            solid({{0, 5.5f}, {5.5f, 8.5f}, {7, 13}, {5, 18}, {0, 21}}, 7, leaves);
        }
    }
    model->meshes.push_back(std::move(mesh));
    Node root;
    root.name = name;
    root.meshes = {0};
    model->nodes.push_back(root);
    return model;
}

} // namespace r4bridge
