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
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace r4bridge {

namespace {

uint32_t packColor(const float c[4]) {
    auto b = [](float v) {
        return (uint32_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f);
    };
    return b(c[0]) | b(c[1]) << 8 | b(c[2]) << 16 | b(c[3]) << 24;
}

} // namespace

std::shared_ptr<r4::assets::Model> terrainModel(RSArea *area, const VGAPalette &palette, const std::vector<float> &lodRanges, const TerrainLight &light) {
    using namespace r4::assets;
    if (!area || !area->GetAreaBlockByID(0, 0)) {
        return nullptr;
    }
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
        if ((uint16_t)b->height >= (uint16_t)h->height) {
            h = b;
        }
        if ((uint16_t)c->height >= (uint16_t)h->height) {
            h = c;
        }
        return h->type;
    };

    // a material by texture of the map, made at its first triangle
    std::map<int, int> textureMaterial;
    auto textured = [&](int imageID) {
        auto it = textureMaterial.find(imageID);
        if (it != textureMaterial.end()) {
            return it->second;
        }
        int material = -1;
        RSImage *src = area->GetImageByID(imageID);
        if (src && src->data && src->width && src->height) {
            Image img;
            img.name = std::string(src->name);
            img.width = (int)src->width;
            img.height = (int)src->height;
            img.channels = 4;
            img.pixels.resize((size_t)img.width * img.height * 4);
            for (int y = 0; y < img.height; y++) {
                for (int x = 0; x < img.width; x++) {
                    // the palette of the game (PALETTE.IFF): the one SCRenderer::init
                    // gives the images (RSImage takes Renderer.getPalette()); its
                    // alpha, as Texture::updateContent: index 255 clear
                    const Texel &t = palette.colors[src->data[(size_t)y * img.width + x]];
                    // R4nkine's images start at the bottom row
                    uint8_t *d = &img.pixels[((size_t)(img.height - 1 - y) * img.width + x) * 4];
                    d[0] = t.r;
                    d[1] = t.g;
                    d[2] = t.b;
                    d[3] = t.a;
                }
            }
            // as SCRenderer::createTextureInGPU (GL_LINEAR, clamped), or better
            finishTexture(img);
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
        if (!b || b->sideSize < 2) {
            return;
        }
        int side = b->sideSize;
        for (int x = 0; x < side - 1; x++) {
            for (int y = 0; y < side - 1; y++) {
                fn(b->GetVertice(x, y), b->GetVertice(x + 1, y), b->GetVertice(x + 1, y + 1), b->GetVertice(x, y + 1));
            }
        }
        AreaBlock *right = i % BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(level, i + 1) : nullptr;
        AreaBlock *below = i / BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(level, i + BLOCK_PER_MAP_SIDE) : nullptr;
        if (right) {
            for (int y = 0; y < side - 1; y++) {
                fn(b->GetVertice(side - 1, y), right->GetVertice(0, y), right->GetVertice(0, y + 1), b->GetVertice(side - 1, y + 1));
            }
        }
        if (below) {
            for (int x = 0; x < side - 1; x++) {
                fn(b->GetVertice(x, side - 1), b->GetVertice(x + 1, side - 1), below->GetVertice(x + 1, 0), below->GetVertice(x, 0));
            }
        }
        AreaBlock *corner = right && below ? area->GetAreaBlockByID(level, i + 1 + BLOCK_PER_MAP_SIDE) : nullptr;
        if (corner) {
            fn(b->GetVertice(side - 1, side - 1), right->GetVertice(0, side - 1), corner->GetVertice(0, 0), below->GetVertice(side - 1, 0));
        }
    };
    // dynamic: the normals of the vertices of each level, from all the cells
    // around them (across the blocks), pointing up
    std::vector<std::unordered_map<const MapVertex *, r4::Vec3>> normals(NUM_LODS);
    if (dynamic) {
        for (int level = 0; level < NUM_LODS; level++) {
            auto &n = normals[level];
            auto face = [&](const MapVertex *a, const MapVertex *b, const MapVertex *c) {
                const float k = light.relief;
                r4::Vec3 A{a->v.x, a->v.y * k, a->v.z};
                r4::Vec3 B{b->v.x, b->v.y * k, b->v.z};
                r4::Vec3 C{c->v.x, c->v.y * k, c->v.z};
                r4::Vec3 f = r4::cross(B - A, C - A);
                if (f.y < 0) {
                    f = -f;
                }
                n[a] += f;
                n[b] += f;
                n[c] += f;
            };
            for (int i = 0; i < BLOCKS_PER_MAP; i++) {
                forEachCell(level, i, [&](const MapVertex *cur, const MapVertex *right, const MapVertex *br, const MapVertex *bottom) {
                    face(cur, br, bottom);
                    face(cur, right, br);
                });
            }
            for (auto &[v, nv] : n) {
                nv = r4::length(nv) > 0 ? r4::normalize(nv) : r4::Vec3{0, 1, 0};
            }
        }
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
            if (it != normals[currentLevel].end()) {
                out.normal = it->second;
            }
        }
        out.uv = uv;
        out.color = color;
        m.vertices.push_back(out);
        m.indices.push_back((uint32_t)m.vertices.size() - 1);
    };
    // a triangle: its colors, and its texture over them when it has one
    auto triangle = [&](const MapVertex *a, const MapVertex *b, const MapVertex *c, const float(*cols)[4], uint16_t imageID, const float(*uv)[2]) {
        const MapVertex *corner[3] = {a, b, c};
        if (dynamic) {
            uint32_t tone = topTone(typeOf(a, b, c));
            for (int k = 0; k < 3; k++) {
                add(0, corner[k], tone, {0, 0});
            }
        } else {
            for (int k = 0; k < 3; k++) {
                add(0, corner[k], packColor(cols[k]), {0, 0});
            }
        }
        if (imageID == 0xFF) {
            return;
        }
        int material = textured(imageID);
        if (material < 0) {
            return;
        }
        for (int k = 0; k < 3; k++) {
            add(material, corner[k], 0xffffffffu, {uv[k][0], 1 - uv[k][1]});
        }
    };
    // a cell, as SCRenderer::renderQuad: the lower triangle, then the upper one
    auto quad = [&](const MapVertex *current, const MapVertex *right, const MapVertex *bottomRight, const MapVertex *bottom) {
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
                for (int x = 0; x < side - 1; x++) {
                    for (int y = 0; y < side - 1; y++) {
                        quad(b->GetVertice(x, y), b->GetVertice(x + 1, y), b->GetVertice(x + 1, y + 1), b->GetVertice(x, y + 1));
                    }
                }
                AreaBlock *right = i % BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(level, i + 1) : nullptr;
                AreaBlock *below = i / BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(level, i + BLOCK_PER_MAP_SIDE) : nullptr;
                if (right) {
                    for (int y = 0; y < side - 1; y++) {
                        quad(b->GetVertice(side - 1, y), right->GetVertice(0, y), right->GetVertice(0, y + 1), b->GetVertice(side - 1, y + 1));
                    }
                }
                if (below) {
                    for (int x = 0; x < side - 1; x++) {
                        quad(b->GetVertice(x, side - 1), b->GetVertice(x + 1, side - 1), below->GetVertice(x + 1, 0), below->GetVertice(x, 0));
                    }
                }
                AreaBlock *corner = right && below ? area->GetAreaBlockByID(level, i + 1 + BLOCK_PER_MAP_SIDE) : nullptr;
                if (corner) {
                    quad(b->GetVertice(side - 1, side - 1), right->GetVertice(0, side - 1), corner->GetVertice(0, 0), below->GetVertice(side - 1, 0));
                }
                // the skirts: walls down from the edges of the block, so that a
                // neighbor at another level leaves no crack (not in the game,
                // which draws all its blocks at one level)
                auto at = [&](int x, int y) -> const MapVertex * {
                    // the vertex x, y of the block and its seams (x, y up to side)
                    AreaBlock *src = b;
                    if (x == side && y == side) {
                        src = corner;
                        x = 0;
                        y = 0;
                    } else if (x == side) {
                        src = right;
                        x = 0;
                    } else if (y == side) {
                        src = below;
                        y = 0;
                    }
                    return src ? src->GetVertice(x, y) : nullptr;
                };
                int endX = right ? side : side - 1;
                int endY = below ? side : side - 1;
                auto skirt = [&](const MapVertex *a, const MapVertex *c) {
                    if (!a || !c) {
                        return;
                    }
                    MapVertex da = *a;
                    MapVertex dc = *c;
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
            levels.push_back(takeMeshes("LVL" + std::to_string(level) + " (" + std::to_string(b ? b->sideSize * b->sideSize : 0) + " vertices)"));
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
    if (model->meshes.empty()) {
        return nullptr;
    }
    return model;
}

std::shared_ptr<r4::assets::Model> overlayModel(RSArea *area, const VGAPalette &palette) {
    using namespace r4::assets;
    if (!area || area->objectOverlay.empty()) {
        return nullptr;
    }
    auto model = std::make_shared<Model>();
    model->source = "overlays";

    // the images of the overlays in one atlas, so that the triangles of an
    // overlay make one mesh, drawn in the order of its drawList: cell 0
    // white (the colored triangles), then a cell by textured color c (the
    // image c + 255, its clear texels in the palette color c: the GL_DECAL
    // of the game); a border by cell, its edges repeated (the
    // GL_CLAMP_TO_EDGE of the game)
    std::map<int, int> cellOf; // color -> cell
    std::vector<RSImage *> cellImage{nullptr};
    std::vector<int> cellColor{-1};
    for (const AreaOverlay &ov : area->objectOverlay) {
        for (uint16_t j : ov.drawList) {
            if (j >= 400 || !ov.vertices) {
                continue;
            }
            int c = ov.trianles[j].color;
            if (c == 255 || cellOf.count(c)) {
                continue;
            }
            RSImage *src = area->GetImageByID(c + 255);
            if (!src || !src->data || !src->width || !src->height || src->width > 128 || src->height > 128) {
                continue;
            }
            cellOf[c] = (int)cellImage.size();
            cellImage.push_back(src);
            cellColor.push_back(c);
        }
    }
    // each cell as an image (its clear texels in its palette color), upscaled
    // as the textures (textureQuality); a wider border with mipmaps, so that
    // their smaller levels do not take the neighbor cells
    const TextureQuality &quality = textureQuality();
    const int scale = r4::assets::upscaleFactor(quality.upscale);
    const int kBorder = quality.mipmaps ? 8 : 2;
    const int kCell = 128 * scale + 2 * kBorder;
    std::vector<Image> cells;
    for (size_t cell = 0; cell < cellImage.size(); cell++) {
        RSImage *src = cellImage[cell];
        const Texel &under = palette.colors[cellColor[cell] & 0xff];
        Image c; // rows from the top, as RSImage
        c.width = src ? (int)src->width : 1;
        c.height = src ? (int)src->height : 1;
        c.channels = 4;
        c.clampS = c.clampT = true;
        c.pixels.assign((size_t)c.width * c.height * 4, 255);
        for (int y = 0; src && y < c.height; y++) {
            for (int x = 0; x < c.width; x++) {
                const Texel &t = palette.colors[src->data[(size_t)y * c.width + x]];
                const Texel &o = t.a ? t : under;
                uint8_t *d = &c.pixels[((size_t)y * c.width + x) * 4];
                d[0] = o.r;
                d[1] = o.g;
                d[2] = o.b;
                d[3] = 255;
            }
        }
        if (src && scale > 1) {
            c = r4::assets::upscale(c, quality.upscale);
        }
        cells.push_back(std::move(c));
    }
    int columns = (int)std::ceil(std::sqrt((double)cellImage.size()));
    int rows = ((int)cellImage.size() + columns - 1) / columns;
    Image atlas;
    atlas.name = "overlay atlas";
    atlas.width = columns * kCell;
    atlas.height = rows * kCell;
    atlas.channels = 4;
    atlas.mipmaps = quality.mipmaps;
    atlas.clampS = atlas.clampT = true;
    std::vector<uint8_t> top((size_t)atlas.width * atlas.height * 4, 255); // rows from the top
    for (size_t cell = 0; cell < cells.size(); cell++) {
        int x0 = (int)(cell % columns) * kCell;
        int y0 = (int)(cell / columns) * kCell;
        const Image &c = cells[cell];
        // the cell, its edges repeated into its border
        for (int y = -kBorder; y < kCell - kBorder; y++) {
            for (int x = -kBorder; x < kCell - kBorder; x++) {
                int sx = std::clamp(x, 0, c.width - 1);
                int sy = std::clamp(y, 0, c.height - 1);
                std::memcpy(&top[((size_t)(y0 + kBorder + y) * atlas.width + x0 + kBorder + x) * 4], &c.pixels[((size_t)sy * c.width + sx) * 4], 4);
            }
        }
    }
    atlas.pixels.resize(top.size());
    for (int y = 0; y < atlas.height; y++) { // R4nkine's images start at the bottom row
        std::memcpy(&atlas.pixels[(size_t)(atlas.height - 1 - y) * atlas.width * 4], &top[(size_t)y * atlas.width * 4], (size_t)atlas.width * 4);
    }
    model->images.push_back(std::move(atlas));
    const float atlasW = (float)(columns * kCell);
    const float atlasH = (float)(rows * kCell);
    // an uv of a cell (in texels of its image of the game, from its top left) in the atlas
    auto atlasUV = [&](int cell, float u, float v) {
        float px = (cell % columns) * kCell + kBorder + u * scale;
        float py = (cell / columns) * kCell + kBorder + v * scale;
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
        if (!ov.vertices || ov.nbTriangles <= 0) {
            continue;
        }
        Mesh mesh;
        mesh.name = "overlay";
        mesh.material = 0;
        for (uint16_t j : ov.drawList) {
            if (j >= 400) {
                continue;
            }
            const AreaOverlayTriangles &tri = ov.trianles[j];
            auto it = cellOf.find(tri.color);
            int cell = tri.color != 255 && it != cellOf.end() ? it->second : 0;
            const Texel &t = palette.colors[tri.color & 0xff];
            uint32_t color = cell == 0 ? (uint32_t)t.r | (uint32_t)t.g << 8 | (uint32_t)t.b << 16 | 0xff000000u : 0xffffffffu;
            RSImage *img = cellImage[cell];
            for (int k = 0; k < 3; k++) {
                const AoVPoints &p = ov.vertices[tri.verticesIdx[k]];
                Vertex v;
                v.position = {(float)p.x, (float)p.y, (float)p.z};
                v.normal = {0, 1, 0};
                // the uv of the game (uv / 128 of the image) in texels of its cell
                v.uv = img ? atlasUV(cell, tri.uv[k].u / kTriUvScale * img->width, tri.uv[k].v / kTriUvScale * img->height) : atlasUV(0, 64, 64);
                v.color = color;
                mesh.vertices.push_back(v);
                mesh.indices.push_back((uint32_t)mesh.vertices.size() - 1);
            }
        }
        if (mesh.indices.empty()) {
            continue;
        }
        mesh.computeBounds();
        root.meshes.push_back((int)model->meshes.size());
        model->meshes.push_back(std::move(mesh));
    }
    if (model->meshes.empty()) {
        return nullptr;
    }
    model->nodes.push_back(root);
    return model;
}

r4::Mat4 mapObjectMatrix(const MapObject &object) {
    // the columns given to glMultMatrixf by SCRenderer::renderObjects
    const float(*o)[3] = object.orientation;
    r4::Mat4 rotation(1.0f);
    rotation[0] = r4::Vec4(o[1][0], o[1][2], -o[1][1], 0);
    rotation[1] = r4::Vec4(o[2][0], o[2][2], -o[2][1], 0);
    rotation[2] = r4::Vec4(o[0][0], o[0][2], -o[0][1], 0);
    return r4::translate({object.position.x, object.position.y, object.position.z}) * rotation;
}

r4::Mat4 missionPartMatrix(const MISN_PART &part) {
    // the glRotatef of SCRenderer::renderMissionObjects, in degrees
    return r4::translate({part.position.x, part.position.y, part.position.z}) * r4::rotate(r4::radians(360.0f - (float)part.azymuth + 90.0f), {0, 1, 0}) * r4::rotate(r4::radians((float)part.pitch), {0, 0, 1}) * r4::rotate(r4::radians(-(float)part.roll), {1, 0, 0});
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
    float fx = x / scale;
    float fz = z / scale;
    int ix = (int)std::floor(fx);
    int iz = (int)std::floor(fz);
    float tx = fx - ix;
    float tz = fz - iz;
    tx = tx * tx * (3 - 2 * tx);
    tz = tz * tz * (3 - 2 * tz);
    auto at = [&](int i, int j) {
        return hash01((uint32_t)i, (uint32_t)j, seed);
    };
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
// the green types: 3 the green of the palette from 64 (meadows and woods),
// 4 the darker one from 128 (the jungle of NICARAGA)
constexpr int kGreen = 3;
constexpr int kJungle = 4;
constexpr int kWater = 0;
// the density at the heart of a wood, and between the woods, x perKm2
constexpr float kWoodCore = 25;
constexpr float kScattered = 0.1f;

const MapVertex *mapVertex(RSArea *area, int x, int y) {
    if (x < 0 || y < 0 || x >= kMapSide || y >= kMapSide) {
        return nullptr;
    }
    AreaBlock *b = area->GetAreaBlockByCoo(0, x / 20, y / 20);
    if (!b || b->sideSize != 20) {
        return nullptr;
    }
    return b->GetVertice(x % 20, y % 20);
}

// the type of a triangle: the one of its highest vertex (RSArea::highestVertex)
const MapVertex *highest(const MapVertex *a, const MapVertex *b, const MapVertex *c) {
    const MapVertex *h = a;
    if ((uint16_t)b->height >= (uint16_t)h->height) {
        h = b;
    }
    if ((uint16_t)c->height >= (uint16_t)h->height) {
        h = c;
    }
    return h;
}

bool green(const MapVertex *a, const MapVertex *b, const MapVertex *c) {
    int type = highest(a, b, c)->type;
    return (type == kGreen || type == kJungle) && a->type != kWater && b->type != kWater && c->type != kWater;
}

// each cell of a chunk: its 4 corners, the lower triangle then the upper
// one, as RSArea
template <class F> void forEachTriangle(RSArea *area, int cx, int cz, int cellsPerChunk, F f) {
    for (int y = cz * cellsPerChunk; y < (cz + 1) * cellsPerChunk; y++) {
        for (int x = cx * cellsPerChunk; x < (cx + 1) * cellsPerChunk; x++) {
            const MapVertex *cur = mapVertex(area, x, y);
            const MapVertex *right = mapVertex(area, x + 1, y);
            const MapVertex *bottomRight = mapVertex(area, x + 1, y + 1);
            const MapVertex *bottom = mapVertex(area, x, y + 1);
            if (!cur || !right || !bottomRight || !bottom) {
                continue;
            }
            uint32_t key = (uint32_t)(y * kMapSide + x) * 2;
            f(cur, bottomRight, bottom, (int)cur->lowerImageID, key, cur->lowerUV);
            f(cur, right, bottomRight, (int)cur->upperImageID, key + 1, cur->upperUV);
        }
    }
}

int cellsPerChunk(const ForestSettings &st) {
    return std::max(1, (int)std::lround(st.chunk / kCell));
}

// the texels of trees of a texture (ForestMode::Textures): dark green, and
// not a flat fill (their neighbors vary); none when they are too few
const std::vector<uint32_t> &treeTexelsOf(RSArea *area, const ForestSettings &st, int id) {
    static const std::vector<uint32_t> none;
    if (id == 0xFF || !st.palette || st.notWoods.count(id) || st.bare.count(id)) {
        return none;
    }
    auto &cache = *st.treeTexels;
    auto it = cache.find(id);
    if (it != cache.end()) {
        return it->second;
    }
    std::vector<uint32_t> &out = cache[id];
    RSImage *img = area->GetImageByID(id);
    if (!img || !img->data || !img->width || !img->height) {
        return out;
    }
    const int w = (int)img->width;
    const int h = (int)img->height;
    auto lum = [&](int x, int y) {
        const Texel &t = st.palette->colors[img->data[(size_t)y * w + x]];
        return (t.r + t.g + t.b) / 3;
    };
    size_t visible = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t k = img->data[(size_t)y * w + x];
            if (k == 255) {
                continue;
            }
            visible++;
            const Texel &t = st.palette->colors[k];
            if (t.g < t.r + 6 || t.g < t.b + 6 || (t.r + t.g + t.b) / 3 > 78) {
                continue;
            }
            int lo = 255;
            int hi = 0;
            for (int yy = std::max(0, y - 1); yy <= std::min(h - 1, y + 1); yy++) {
                for (int xx = std::max(0, x - 1); xx <= std::min(w - 1, x + 1); xx++) {
                    if (img->data[(size_t)yy * w + xx] != 255) {
                        int l = lum(xx, yy);
                        lo = std::min(lo, l);
                        hi = std::max(hi, l);
                    }
                }
            }
            if (hi - lo >= 10) {
                out.push_back((uint32_t)(y * w + x));
            }
        }
    }
    if (out.size() < st.minTreeShare * visible) {
        out.clear();
    }
    return out;
}

} // namespace

std::vector<ForestChunk> forestChunks(RSArea *area, const ForestSettings &st) {
    std::vector<ForestChunk> chunks;
    if (!area) {
        return chunks;
    }
    const int cells = cellsPerChunk(st);
    const int perSide = (kMapSide - 1 + cells - 1) / cells;
    const float half = BLOCK_WIDTH * BLOCK_PER_MAP_SIDE_DIV_2;
    for (int cz = 0; cz < perSide; cz++) {
        for (int cx = 0; cx < perSide; cx++) {
            bool any = false;
            float low = 1e30f;
            float high = -1e30f;
            forEachTriangle(area, cx, cz, cells, [&](const MapVertex *a, const MapVertex *b, const MapVertex *c, int image, uint32_t, const float(*)[2]) {
                if (st.mode == ForestMode::Textures ? treeTexelsOf(area, st, image).empty() : !green(a, b, c)) {
                    return;
                }
                any = true;
                for (const MapVertex *v : {a, b, c}) {
                    low = std::min(low, v->v.y);
                    high = std::max(high, v->v.y);
                }
            });
            if (!any) {
                continue;
            }
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
    }
    return chunks;
}

void plantForest(RSArea *area, const ForestSettings &st, ForestChunk &chunk) {
    for (auto &t : chunk.trees) {
        t.clear();
    }
    if (!area) {
        return;
    }
    // the boxes of the runways, and their surroundings
    struct Box {
        float x0;
        float z0;
        float x1;
        float z1;
    };
    std::vector<Box> runways;
    for (const AreaOverlay &ov : area->objectOverlay) {
        if (ov.verticesVec.empty()) {
            continue;
        }
        Box b{1e30f, 1e30f, -1e30f, -1e30f};
        for (const AoVPoints &p : ov.verticesVec) {
            b.x0 = std::min(b.x0, (float)p.x);
            b.x1 = std::max(b.x1, (float)p.x);
            b.z0 = std::min(b.z0, (float)p.z);
            b.z1 = std::max(b.z1, (float)p.z);
        }
        const float margin = 300;
        runways.push_back({b.x0 - margin, b.z0 - margin, b.x1 + margin, b.z1 + margin});
    }
    auto free = [&](float x, float z) {
        for (const Box &b : runways) {
            if (x > b.x0 && x < b.x1 && z > b.z0 && z < b.z1) {
                return false;
            }
        }
        for (const r4::Vec3 &k : st.keepOut) {
            if ((x - k.x) * (x - k.x) + (z - k.y) * (z - k.y) < k.z * k.z) {
                return false;
            }
        }
        return true;
    };
    // a tree: its species by its ground (conifers on the higher ground, few
    // in the jungle), its turn and size; the 4 numbers drawn for each
    auto plant = [&](r4::Vec3 p, bool jungle, float species, float turn, float size) {
        float conifers = jungle ? 0.1f : 0.35f + std::clamp((p.y - 500) / 3000, 0.0f, 0.5f);
        int sp = (species < conifers ? 0 : 1) + (jungle ? 2 : 0);
        r4::scene::Placement t;
        t.position = p;
        t.heading = turn * 6.2831853f;
        t.scale = (0.7f + size * 0.6f) * st.treeScale;
        t.scaleUp = t.scale * (0.85f + species * 0.3f);
        chunk.trees[sp].push_back(t);
    };
    forEachTriangle(area, chunk.cx, chunk.cz, cellsPerChunk(st), [&](const MapVertex *a, const MapVertex *b, const MapVertex *c, int image, uint32_t key, const float(*uv)[2]) {
        if (st.mode == ForestMode::Textures) {
            // the trees painted in the texture: a tree on its texels of trees
            // (each by chance: one by areaPerTree), through the triangle's uvs
            const std::vector<uint32_t> &texels = treeTexelsOf(area, st, image);
            if (texels.empty()) {
                return;
            }
            RSImage *img = area->GetImageByID(image);
            const float w = (float)img->width;
            const float h = (float)img->height;
            r4::Vec2 t0{uv[0][0] * w, uv[0][1] * h};
            r4::Vec2 t1{uv[1][0] * w, uv[1][1] * h};
            r4::Vec2 t2{uv[2][0] * w, uv[2][1] * h};
            float det = (t1.x - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (t1.y - t0.y);
            if (std::abs(det) < 1e-6f) {
                return;
            }
            r4::Vec3 pa{a->v.x, a->v.y, a->v.z};
            r4::Vec3 pb{b->v.x, b->v.y, b->v.z};
            r4::Vec3 pc{c->v.x, c->v.y, c->v.z};
            const bool jungle = highest(a, b, c)->type == kJungle;
            const float chance = (kCell / w) * (kCell / h) / st.areaPerTree * st.density;
            Random rnd{key * 2654435761u + st.seed * 97u + 1};
            for (uint32_t texel : texels) {
                r4::Vec2 tex{(float)(texel % (uint32_t)w) + rnd.next(), (float)(texel / (uint32_t)w) + rnd.next()};
                float keep = rnd.next();
                float species = rnd.next();
                float turn = rnd.next();
                float size = rnd.next();
                if (keep >= chance) {
                    continue;
                }
                // in this triangle (the texels of the other half are the other's)
                float s = ((tex.x - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (tex.y - t0.y)) / det;
                float t = ((t1.x - t0.x) * (tex.y - t0.y) - (tex.x - t0.x) * (t1.y - t0.y)) / det;
                if (s < 0 || t < 0 || s + t > 1) {
                    continue;
                }
                r4::Vec3 p = pa + (pb - pa) * s + (pc - pa) * t;
                if (free(p.x, p.z)) {
                    plant(p, jungle, species, turn, size);
                }
            }
            return;
        }
        if (!green(a, b, c) || st.bare.count(image)) {
            return;
        }
        const bool jungle = highest(a, b, c)->type == kJungle;
        auto it = st.images.find(image);
        bool woodImage = image != 0xFF && it != st.images.end();
        r4::Vec3 pa{a->v.x, a->v.y, a->v.z};
        r4::Vec3 pb{b->v.x, b->v.y, b->v.z};
        r4::Vec3 pc{c->v.x, c->v.y, c->v.z};
        float areaKm2 = 0.5f * std::abs((pb.x - pa.x) * (pc.z - pa.z) - (pc.x - pa.x) * (pb.z - pa.z)) / 1e6f;
        Random rnd{key * 2654435761u + st.seed * 97u + 1};
        // the candidates at the highest density, kept by the density of their place
        float top = (woodImage ? it->second : st.perKm2 * kWoodCore) * st.density * (jungle ? st.jungleDensity : 1.0f);
        float expected = top * areaKm2;
        int n = (int)expected + (rnd.next() < expected - (int)expected ? 1 : 0);
        for (int i = 0; i < n; i++) {
            // a point of the triangle, uniform
            float u = rnd.next();
            float v = rnd.next();
            if (u + v > 1) {
                u = 1 - u;
                v = 1 - v;
            }
            r4::Vec3 p = pa + (pb - pa) * u + (pc - pa) * v;
            float keep = rnd.next();
            float species = rnd.next();
            float turn = rnd.next();
            float size = rnd.next();
            // the woods, where the noise is high (perKm2 x kWoodCore at their
            // heart), and a few trees alone between them; the textures of
            // woods: their density everywhere
            if (!woodImage) {
                float wood = valueNoise(p.x, p.z, 2500, st.seed) * 0.7f + valueNoise(p.x, p.z, 600, st.seed + 7) * 0.3f;
                // the jungle: woods over most of it
                float share = std::clamp((wood - (jungle ? 0.45f : 0.6f)) * 5.0f, 0.0f, 1.0f) + kScattered / kWoodCore;
                if (keep > share) {
                    continue;
                }
            }
            if (free(p.x, p.z)) {
                plant(p, jungle, species, turn, size);
            }
        }
    });
}

std::shared_ptr<r4::assets::Model> treeModel(int species, bool simple, const VGAPalette &palette, const std::vector<PaletteRange> &ranges, Lighting lighting) {
    using namespace r4::assets;
    auto model = std::make_shared<Model>();
    const std::string name = std::string(species % 2 == 0 ? "conifer" : "broadleaf") + (species >= 2 ? " (jungle)" : "") + (simple ? " simple" : "");
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
    const uint32_t green = species >= 2 ? 128 : 64; // the ramp of their ground
    const uint32_t conifer = green + 13;
    const uint32_t leaves = green + 18;
    const uint32_t trunk = 96 + 10;
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
                if (rings[k].first > 0) {
                    tri(lo[i], lo[j], hi[j], center, color);
                }
                if (rings[k + 1].first > 0) {
                    tri(lo[i], hi[j], hi[i], center, color);
                }
            }
        }
        // the bottom, when it is not a point
        if (rings.front().first > 0) {
            auto lo = ring(sides, rings.front().first, rings.front().second, turn);
            for (int i = 1; i + 1 < sides; i++) {
                tri(lo[0], lo[i], lo[i + 1], center, color);
            }
        }
    };
    if (species % 2 == 0) {
        if (simple) {
            solid({{5.5f, 2}, {0, 25}}, 4, conifer, 0.5f);
        } else {
            solid({{0.7f, 0}, {0.7f, 5}}, 5, trunk);
            solid({{6, 4}, {0, 17}}, 7, conifer);
            solid({{4.5f, 11}, {0, 25}}, 7, conifer, 0.5f);
        }
    } else {
        if (simple) {
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

// --- the cities -------------------------------------------------------------------------

namespace {

// a building: its rectangle of texels in its image (from its top left, x1
// and y1 included) and its height
struct Footprint {
    int x0;
    int y0;
    int x1;
    int y1;
    float height;
};

// the buildings of the textures of the game (TXMPACK, the same on every
// map), read on their images by hand; the dark towers of the downtowns higher
const std::map<int, std::vector<Footprint>> kBuildings{
    // 223-228 : the harbor (64 x 64, a triangle each half): sheds on the piers, goods
    {223, {{16, 13, 63, 16, 16}, {50, 17, 55, 39, 16}, {5, 50, 38, 54, 16}}},
    {224, {{22, 3, 58, 7, 16}, {8, 34, 19, 52, 12}, {23, 48, 45, 53, 16}}},
    {225, {{22, 3, 56, 9, 16}, {7, 15, 14, 40, 16}}},
    {226, {{15, 4, 44, 10, 16}, {4, 18, 12, 38, 16}, {47, 19, 58, 32, 12}, {13, 53, 19, 58, 10}, {48, 41, 56, 46, 10}}},
    {227, {{25, 8, 63, 15, 16}, {6, 45, 46, 54, 16}}},
    {228, {{47, 9, 56, 46, 16}, {9, 50, 44, 55, 16}}},
    // 229 : downtown
    {229, {{5, 4, 24, 27, 140}, {25, 3, 41, 12, 30}, {43, 3, 57, 29, 70}, {67, 3, 93, 23, 60}, {5, 37, 26, 57, 160}, {26, 37, 46, 57, 90}, {46, 37, 58, 57, 120}, {67, 34, 88, 58, 200}, {98, 35, 117, 58, 80}, {8, 67, 55, 91, 35}, {100, 68, 122, 92, 150}, {68, 82, 100, 90, 25}, {35, 102, 53, 119, 60}, {76, 99, 119, 121, 50}}},
    // 230 : downtown, the other
    {230, {{5, 5, 27, 24, 90}, {37, 4, 57, 27, 150}, {5, 26, 14, 59, 40}, {13, 46, 29, 58, 40}, {38, 29, 57, 59, 180}, {68, 8, 92, 55, 70}, {104, 17, 116, 53, 110}, {4, 68, 25, 94, 60}, {34, 67, 59, 88, 130}, {69, 68, 89, 98, 160}, {69, 101, 92, 124, 100}, {100, 77, 122, 119, 25}, {44, 99, 60, 115, 25}}},
    // 233 : a farm
    {233, {{34, 69, 53, 82, 18}, {74, 74, 80, 80, 14}, {84, 86, 94, 96, 12}, {17, 87, 24, 105, 12}, {17, 107, 24, 117, 10}, {56, 80, 61, 84, 8}}},
    // 248 : a village on green, with its church
    {248, {{9, 11, 32, 30, 18}, {36, 34, 50, 57, 30}, {51, 44, 59, 57, 12}, {84, 18, 109, 46, 25}, {107, 4, 114, 8, 10}, {119, 28, 126, 35, 10}, {3, 53, 9, 58, 10}, {11, 53, 17, 58, 10}, {19, 53, 25, 58, 10}, {12, 69, 56, 78, 14}, {47, 81, 54, 87, 10}, {20, 88, 30, 97, 12}, {20, 100, 30, 109, 12}, {44, 89, 56, 100, 16}, {44, 103, 56, 113, 16}, {81, 69, 86, 79, 14}, {93, 71, 107, 80, 12}, {113, 71, 123, 82, 12}, {69, 73, 77, 79, 10}, {69, 80, 77, 85, 10}, {69, 86, 77, 91, 10}, {69, 98, 76, 102, 10}, {69, 104, 76, 108, 10}, {99, 91, 107, 100, 12}, {99, 102, 107, 106, 10}, {99, 108, 107, 113, 10}, {69, 115, 104, 124, 14}}},
    // 249 : a village on green
    {249, {{33, 24, 44, 33, 12}, {19, 33, 28, 43, 12}, {30, 46, 40, 56, 12}, {49, 35, 56, 45, 10}, {50, 48, 57, 56, 10}, {68, 51, 75, 56, 9}, {3, 53, 9, 58, 9}, {11, 53, 17, 58, 9}, {19, 53, 25, 58, 9}, {77, 53, 83, 58, 9}, {85, 53, 91, 58, 9}, {93, 53, 99, 58, 9}, {101, 53, 107, 58, 9}, {109, 53, 115, 58, 9}, {117, 53, 123, 58, 9}, {78, 69, 122, 78, 14}, {113, 81, 120, 87, 10}, {19, 71, 34, 82, 12}, {38, 70, 44, 76, 10}, {47, 70, 54, 80, 12}}},
    // 250 : a village on sand
    {250, {{55, 31, 61, 41, 10}, {26, 41, 40, 45, 8}, {26, 48, 40, 53, 8}, {45, 40, 51, 45, 9}, {57, 47, 63, 56, 10}, {43, 52, 48, 55, 8}, {73, 44, 90, 49, 8}, {73, 50, 90, 55, 8}, {98, 50, 116, 56, 10}, {22, 70, 35, 75, 8}, {38, 76, 42, 92, 7}, {48, 76, 52, 92, 7}, {53, 89, 58, 93, 8}, {62, 69, 82, 86, 12}, {76, 84, 82, 95, 10}, {88, 69, 95, 75, 10}, {97, 69, 112, 76, 12}, {97, 81, 110, 86, 8}, {98, 92, 105, 98, 10}, {38, 102, 45, 108, 9}}},
    // 251 : a village on sand, with its silos
    {251, {{39, 13, 45, 26, 30}, {49, 13, 56, 27, 30}, {5, 25, 13, 37, 12}, {70, 6, 88, 17, 10}, {8, 47, 26, 57, 12}, {28, 44, 41, 51, 10}, {28, 52, 42, 59, 10}, {43, 53, 50, 59, 9}, {71, 31, 103, 56, 14}, {104, 43, 111, 49, 12}, {51, 42, 57, 47, 10}, {9, 68, 40, 74, 10}, {23, 77, 43, 84, 10}, {44, 75, 51, 80, 9}, {45, 81, 52, 86, 10}, {86, 70, 112, 77, 10}, {79, 75, 86, 81, 10}, {104, 78, 125, 92, 12}, {15, 90, 42, 117, 12}, {44, 90, 54, 103, 10}, {44, 107, 51, 112, 10}, {60, 96, 95, 117, 10}, {99, 98, 120, 117, 12}, {61, 87, 73, 92, 9}, {89, 90, 97, 96, 10}, {68, 115, 75, 121, 9}}},
};
// the houses: found on their image, by their light grey roofs
constexpr int kHousesImage = 231;

// the facade of a texture's buildings
enum class Facade {
    Offices,
    Houses,
    Industry
};
Facade facadeOf(int image) {
    if (image == 229 || image == 230) {
        return Facade::Offices;
    }
    if (image >= 223 && image <= 228) {
        return Facade::Industry;
    }
    return Facade::Houses;
}

// the houses: the components of light grey texels (their roofs), 2 x 2 at
// least; the rings of the outlined houses fall apart and are dropped
std::vector<Footprint> housesOf(const RSImage *img, const VGAPalette &palette) {
    std::vector<Footprint> houses;
    const int w = (int)img->width;
    const int h = (int)img->height;
    std::vector<uint8_t> light((size_t)w * h);
    for (size_t i = 0; i < light.size(); i++) {
        uint8_t k = img->data[i];
        const Texel &t = palette.colors[k];
        int lo = std::min({t.r, t.g, t.b});
        int hi = std::max({t.r, t.g, t.b});
        light[i] = k != 255 && hi - lo <= 12 && (t.r + t.g + t.b) / 3 >= 0x8c;
    }
    std::vector<int> stack;
    for (int start = 0; start < w * h; start++) {
        if (!light[start]) {
            continue;
        }
        int x0 = w;
        int y0 = h;
        int x1 = -1;
        int y1 = -1;
        int n = 0;
        stack.push_back(start);
        light[start] = 0;
        while (!stack.empty()) {
            int j = stack.back();
            stack.pop_back();
            int x = j % w;
            int y = j / w;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
            n++;
            const int nx[4] = {x + 1, x - 1, x, x};
            const int ny[4] = {y, y, y + 1, y - 1};
            for (int k = 0; k < 4; k++) {
                if (nx[k] >= 0 && ny[k] >= 0 && nx[k] < w && ny[k] < h && light[(size_t)ny[k] * w + nx[k]]) {
                    light[(size_t)ny[k] * w + nx[k]] = 0;
                    stack.push_back(ny[k] * w + nx[k]);
                }
            }
        }
        if (n >= 4 && x1 - x0 >= 1 && y1 - y0 >= 1) {
            houses.push_back({x0, y0, x1, y1, 11 + hash01((uint32_t)x0, (uint32_t)y0, 3) * 5});
        }
    }
    return houses;
}

// a facade, a floor of 8 high and 8 wide by repeat: rows of windows
// (offices), a window by floor (houses), grey cladding without windows (the
// sheds of the harbor)
r4::assets::Image facadeImage(Facade kind) {
    r4::assets::Image img;
    img.name = kind == Facade::Offices ? "facade, offices" : kind == Facade::Houses ? "facade, houses" : "facade, industry";
    img.width = img.height = 16;
    img.channels = 4;
    img.pixels.resize(16 * 16 * 4);
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            // R4nkine's images from the bottom row: y 0 is the bottom of the floor
            uint8_t *d = &img.pixels[((size_t)y * 16 + x) * 4];
            uint8_t c[3];
            switch (kind) {
            case Facade::Offices:
                if (y >= 5 && y <= 12 && (x % 8) >= 1 && (x % 8) <= 6) {
                    c[0] = 46;
                    c[1] = 58;
                    c[2] = 72;
                } else {
                    c[0] = 150;
                    c[1] = 150;
                    c[2] = 146;
                }
                break;
            case Facade::Houses:
                if (y >= 6 && y <= 11 && x >= 5 && x <= 10) {
                    c[0] = 46;
                    c[1] = 58;
                    c[2] = 72;
                } else {
                    c[0] = 196;
                    c[1] = 186;
                    c[2] = 160;
                }
                break;
            default: // vertical ribs
                c[0] = c[1] = c[2] = (uint8_t)(x % 4 == 0 ? 92 : 118);
                break;
            }
            d[0] = c[0];
            d[1] = c[1];
            d[2] = c[2];
            d[3] = 255;
        }
    }
    img.mipmaps = true;
    return img;
}

} // namespace

std::shared_ptr<r4::assets::Model> cityModel(RSArea *area, const VGAPalette &palette, Lighting lighting, const std::vector<r4::Vec3> &keepOut, size_t *buildings) {
    using namespace r4::assets;
    if (buildings) {
        *buildings = 0;
    }
    if (!area) {
        return nullptr;
    }
    auto model = std::make_shared<Model>();
    model->source = "cities";
    Node root;
    root.name = "cities";
    model->nodes.push_back(root);
    // the materials: lit by the sun as the objects, the texture times the intensity
    auto material = [&](const std::string &name, int image) {
        Material m;
        m.name = name;
        m.shading = Shading::PaletteRamp;
        m.rampTextured = true;
        m.rampAmbient = lighting.ambient;
        m.rampLight = lighting.light;
        m.diffuseImage = image;
        model->materials.push_back(m);
        return (int)model->materials.size() - 1;
    };
    int walls[3];
    for (Facade f : {Facade::Offices, Facade::Houses, Facade::Industry}) {
        model->images.push_back(facadeImage(f));
        walls[(int)f] = material(model->images.back().name, (int)model->images.size() - 1);
    }
    // by texture with buildings: its roofs (the image itself) and its footprints
    struct Built {
        int roofs = -1;
        std::vector<Footprint> footprints;
        float w = 0;
        float h = 0;
        int walls = 0;
    };
    std::map<int, Built> built;
    auto builtOf = [&](int id) -> Built * {
        if (id == 0xFF || (id != kHousesImage && !kBuildings.count(id))) {
            return nullptr;
        }
        auto it = built.find(id);
        if (it != built.end()) {
            return it->second.roofs >= 0 ? &it->second : nullptr;
        }
        Built &b = built[id];
        RSImage *src = area->GetImageByID(id);
        if (!src || !src->data || !src->width || !src->height) {
            return nullptr;
        }
        Image img;
        img.name = "roofs " + std::to_string(id);
        img.width = (int)src->width;
        img.height = (int)src->height;
        img.channels = 4;
        img.pixels.resize((size_t)img.width * img.height * 4);
        for (int y = 0; y < img.height; y++) {
            for (int x = 0; x < img.width; x++) {
                const Texel &t = palette.colors[src->data[(size_t)y * img.width + x]];
                uint8_t *d = &img.pixels[((size_t)(img.height - 1 - y) * img.width + x) * 4];
                d[0] = t.r;
                d[1] = t.g;
                d[2] = t.b;
                d[3] = 255;
            }
        }
        finishTexture(img);
        model->images.push_back(std::move(img));
        b.roofs = material("roofs " + std::to_string(id), (int)model->images.size() - 1);
        b.w = (float)src->width;
        b.h = (float)src->height;
        b.walls = walls[(int)facadeOf(id)];
        b.footprints = id == kHousesImage ? housesOf(src, palette) : kBuildings.at(id);
        return &b;
    };
    // none on the objects of the map and of the mission: a footprint whose
    // box comes within the radius of one is left
    auto free = [&](r4::Vec2 lo, r4::Vec2 hi) {
        for (const r4::Vec3 &k : keepOut) {
            float dx = std::max({lo.x - k.x, 0.0f, k.x - hi.x});
            float dz = std::max({lo.y - k.y, 0.0f, k.y - hi.y});
            if (dx * dx + dz * dz < k.z * k.z) {
                return false;
            }
        }
        return true;
    };
    auto quad = [](Mesh &m, const r4::Vec3(&p)[4], const r4::Vec2(&uv)[4], r4::Vec3 n) {
        // turned to face n
        bool flip = r4::dot(r4::cross(p[1] - p[0], p[2] - p[0]), n) < 0;
        uint32_t base = (uint32_t)m.vertices.size();
        for (int k = 0; k < 4; k++) {
            Vertex v;
            v.position = p[k];
            v.normal = n;
            v.uv = uv[k];
            v.color = 0xffffffffu;
            m.vertices.push_back(v);
        }
        const uint32_t order[2][6] = {{0, 1, 2, 0, 2, 3}, {0, 2, 1, 0, 3, 2}};
        for (uint32_t i : order[flip ? 1 : 0]) {
            m.indices.push_back(base + i);
        }
    };

    for (int y = 0; y + 1 < kMapSide; y++) {
        for (int x = 0; x + 1 < kMapSide; x++) {
            const MapVertex *cur = mapVertex(area, x, y);
            const MapVertex *right = mapVertex(area, x + 1, y);
            const MapVertex *bottomRight = mapVertex(area, x + 1, y + 1);
            const MapVertex *bottom = mapVertex(area, x, y + 1);
            if (!cur || !right || !bottomRight || !bottom) {
                continue;
            }
            std::map<int, Mesh> meshes;              // by material
            std::set<std::pair<int, size_t>> placed; // image, footprint: once by cell
            // the two triangles of the cell as RSArea, each its image and uvs
            // (the whole image over the cell, or a half of it over a triangle)
            struct Tri {
                const MapVertex *v[3];
                int image;
                const float (*uv)[2];
            } tris[2] = {{{cur, bottomRight, bottom}, cur->lowerImageID, cur->lowerUV}, {{cur, right, bottomRight}, cur->upperImageID, cur->upperUV}};
            for (const Tri &t : tris) {
                Built *b = builtOf(t.image);
                if (!b) {
                    continue;
                }
                // a texel of the image on the ground: through the triangle's
                // uvs (texels, from the top), its barycentric coordinates
                r4::Vec2 t0{t.uv[0][0] * b->w, t.uv[0][1] * b->h};
                r4::Vec2 t1{t.uv[1][0] * b->w, t.uv[1][1] * b->h};
                r4::Vec2 t2{t.uv[2][0] * b->w, t.uv[2][1] * b->h};
                float det = (t1.x - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (t1.y - t0.y);
                if (std::abs(det) < 1e-6f) {
                    continue;
                }
                auto bary = [&](r4::Vec2 p) {
                    float a = ((p.x - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (p.y - t0.y)) / det;
                    float c = ((t1.x - t0.x) * (p.y - t0.y) - (p.x - t0.x) * (t1.y - t0.y)) / det;
                    return r4::Vec2{a, c};
                };
                r4::Vec2 p0{t.v[0]->v.x, t.v[0]->v.z};
                r4::Vec2 p1{t.v[1]->v.x, t.v[1]->v.z};
                r4::Vec2 p2{t.v[2]->v.x, t.v[2]->v.z};
                auto world = [&](r4::Vec2 tex) {
                    r4::Vec2 ab = bary(tex);
                    return p0 + (p1 - p0) * ab.x + (p2 - p0) * ab.y;
                };
                for (size_t fi = 0; fi < b->footprints.size(); fi++) {
                    const Footprint &f = b->footprints[fi];
                    // in this triangle: its center within the triangle's texels
                    r4::Vec2 center{(f.x0 + f.x1 + 1) * 0.5f, (f.y0 + f.y1 + 1) * 0.5f};
                    r4::Vec2 ab = bary(center);
                    if (ab.x < -1e-4f || ab.y < -1e-4f || ab.x + ab.y > 1 + 1e-4f || placed.count({t.image, fi})) {
                        continue;
                    }
                    placed.insert({t.image, fi});
                    const float u[4] = {(float)f.x0, (float)f.x1 + 1, (float)f.x1 + 1, (float)f.x0};
                    const float v[4] = {(float)f.y0, (float)f.y0, (float)f.y1 + 1, (float)f.y1 + 1};
                    r4::Vec2 c[4];
                    r4::Vec2 lo{1e30f, 1e30f};
                    r4::Vec2 hi{-1e30f, -1e30f};
                    for (int k = 0; k < 4; k++) {
                        c[k] = world({u[k], v[k]});
                        lo = r4::Vec2{std::min(lo.x, c[k].x), std::min(lo.y, c[k].y)};
                        hi = r4::Vec2{std::max(hi.x, c[k].x), std::max(hi.y, c[k].y)};
                    }
                    if (!free(lo, hi)) {
                        continue;
                    }
                    float low = 1e30f;
                    float high = -1e30f;
                    for (int k = 0; k < 4; k++) {
                        float g = area->getY(c[k].x, c[k].y) - 5; // getY is 5 above
                        low = std::min(low, g);
                        high = std::max(high, g);
                    }
                    // standing on the highest corner, its foot under the lowest
                    float bottomY = low - 10;
                    float top = high + f.height;
                    r4::Vec3 roof[4];
                    r4::Vec3 foot[4];
                    r4::Vec2 roofUV[4];
                    for (int k = 0; k < 4; k++) {
                        roof[k] = {c[k].x, top, c[k].y};
                        foot[k] = {c[k].x, bottomY, c[k].y};
                        roofUV[k] = {u[k] / b->w, 1 - v[k] / b->h};
                    }
                    quad(meshes[b->roofs], roof, roofUV, {0, 1, 0});
                    r4::Vec2 mid = (c[0] + c[1] + c[2] + c[3]) * 0.25f;
                    for (int k = 0; k < 4; k++) {
                        int j = (k + 1) % 4;
                        r4::Vec2 e = c[j] - c[k];
                        r4::Vec2 out{e.y, -e.x}; // a normal of the edge, turned outward
                        if (r4::dot(out, (c[k] + c[j]) * 0.5f - mid) < 0) {
                            out = -out;
                        }
                        r4::Vec3 n = r4::normalize(r4::Vec3{out.x, 0, out.y});
                        const r4::Vec3 p[4] = {foot[k], foot[j], roof[j], roof[k]};
                        // a floor of 8: the facade repeated along and up, from the roof down
                        float len = r4::length(e) / 8;
                        float up = (top - bottomY) / 8;
                        const r4::Vec2 uv[4] = {{0, -up}, {len, -up}, {len, 0}, {0, 0}};
                        quad(meshes[b->walls], p, uv, n);
                    }
                    if (buildings) {
                        ++*buildings;
                    }
                }
            }
            if (meshes.empty()) {
                continue;
            }
            Node node;
            node.name = "city " + std::to_string(x) + "," + std::to_string(y);
            for (auto &[mat, m] : meshes) {
                m.material = mat;
                m.name = node.name + " " + model->materials[mat].name;
                model->meshes.push_back(std::move(m));
                node.meshes.push_back((int)model->meshes.size() - 1);
            }
            model->nodes.push_back(node);
            model->nodes[0].children.push_back((int)model->nodes.size() - 1);
        }
    }
    if (model->nodes[0].children.empty()) {
        return nullptr;
    }
    return model;
}

} // namespace r4bridge
