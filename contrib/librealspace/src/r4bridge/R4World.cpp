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

#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace r4bridge {

namespace {

uint32_t packColor(const float c[4]) {
    auto b = [](float v) { return (uint32_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f); };
    return b(c[0]) | b(c[1]) << 8 | b(c[2]) << 16 | b(c[3]) << 24;
}

} // namespace

std::shared_ptr<r4::assets::Model> terrainModel(RSArea *area, const VGAPalette &palette, int lod) {
    using namespace r4::assets;
    if (!area || lod < 0 || lod >= NUM_LODS || !area->GetAreaBlockByID(lod, 0))
        return nullptr;
    auto model = std::make_shared<Model>();
    model->source = "terrain";

    // the colors of the triangles: as they are (RSArea lit them), both faces
    // (the game finds the winding of its terrain at run time)
    Material colors;
    colors.name = "terrain colors";
    colors.shading = Shading::Unlit;
    colors.vertexColors = true;
    colors.diffuse = {1, 1, 1, 1};
    colors.doubleSided = true;
    model->materials.push_back(colors);

    // a material by texture of the map, made at its first triangle
    std::map<int, int> textureMaterial;
    auto textured = [&](int imageID) {
        auto it = textureMaterial.find(imageID);
        if (it != textureMaterial.end())
            return it->second;
        int material = -1;
        RSImage *src = area->GetImageByID(imageID);
        if (src && src->data && src->width && src->height) {
            const VGAPalette &pal = src->palette ? *src->palette : palette;
            Image img;
            img.name = std::string(src->name);
            img.width = (int)src->width;
            img.height = (int)src->height;
            img.channels = 4;
            img.pixels.resize((size_t)img.width * img.height * 4);
            for (int y = 0; y < img.height; y++)
                for (int x = 0; x < img.width; x++) {
                    uint8_t index = src->data[(size_t)y * img.width + x];
                    const Texel &t = pal.colors[index];
                    // R4nkine's images start at the bottom row
                    uint8_t *d = &img.pixels[((size_t)(img.height - 1 - y) * img.width + x) * 4];
                    // the alpha of the palette, as Texture::updateContent: the
                    // palette of the map textures leaves its unused indices clear
                    d[0] = t.r, d[1] = t.g, d[2] = t.b, d[3] = t.a;
                }
            bleedClearTexels(img);
            model->images.push_back(std::move(img));
            Material m;
            m.name = std::string("terrain ") + src->name;
            m.shading = Shading::Unlit;
            m.diffuseImage = (int)model->images.size() - 1;
            m.alpha = AlphaMode::Mask;
            m.decal = true; // over the colors: the GL_DECAL of the game
            m.doubleSided = true;
            model->materials.push_back(m);
            material = (int)model->materials.size() - 1;
        }
        return textureMaterial[imageID] = material;
    };

    std::map<int, Mesh> meshes; // by material
    auto add = [&](int material, const MapVertex *v, uint32_t color, r4::Vec2 uv) {
        Mesh &m = meshes[material];
        Vertex out;
        out.position = {v->v.x, v->v.y, v->v.z};
        out.normal = {0, 1, 0};
        out.uv = uv;
        out.color = color;
        m.vertices.push_back(out);
        m.indices.push_back((uint32_t)m.vertices.size() - 1);
    };
    // a triangle: its colors, and its texture over them when it has one
    auto triangle = [&](const MapVertex *a, const MapVertex *b, const MapVertex *c, const float (*cols)[4],
                        uint16_t imageID, const float (*uv)[2]) {
        const MapVertex *corner[3] = {a, b, c};
        for (int k = 0; k < 3; k++)
            add(0, corner[k], packColor(cols[k]), {0, 0});
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

    // the blocks and their seams, as SCRenderer::renderBlock
    const int last = BLOCK_PER_MAP_SIDE - 1;
    for (int i = 0; i < BLOCKS_PER_MAP; i++) {
        AreaBlock *block = area->GetAreaBlockByID(lod, i);
        if (!block || block->sideSize < 2)
            continue;
        int side = block->sideSize;
        for (int x = 0; x < side - 1; x++)
            for (int y = 0; y < side - 1; y++)
                quad(block->GetVertice(x, y), block->GetVertice(x + 1, y), block->GetVertice(x + 1, y + 1),
                     block->GetVertice(x, y + 1));
        AreaBlock *right = i % BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(lod, i + 1) : nullptr;
        AreaBlock *below = i / BLOCK_PER_MAP_SIDE != last ? area->GetAreaBlockByID(lod, i + BLOCK_PER_MAP_SIDE) : nullptr;
        if (right)
            for (int y = 0; y < side - 1; y++)
                quad(block->GetVertice(side - 1, y), right->GetVertice(0, y), right->GetVertice(0, y + 1),
                     block->GetVertice(side - 1, y + 1));
        if (below)
            for (int x = 0; x < side - 1; x++)
                quad(block->GetVertice(x, side - 1), block->GetVertice(x + 1, side - 1), below->GetVertice(x + 1, 0),
                     below->GetVertice(x, 0));
        AreaBlock *corner = right && below ? area->GetAreaBlockByID(lod, i + 1 + BLOCK_PER_MAP_SIDE) : nullptr;
        if (corner)
            quad(block->GetVertice(side - 1, side - 1), right->GetVertice(0, side - 1), corner->GetVertice(0, 0),
                 below->GetVertice(side - 1, 0));
    }

    Node root;
    root.name = "terrain";
    for (auto &[material, mesh] : meshes) {
        mesh.material = material;
        mesh.name = model->materials[material].name;
        mesh.computeBounds();
        root.meshes.push_back((int)model->meshes.size());
        model->meshes.push_back(std::move(mesh));
    }
    if (model->meshes.empty())
        return nullptr;
    model->nodes.push_back(root);
    return model;
}

std::shared_ptr<r4::assets::Model> overlayModel(RSArea *area, const VGAPalette &palette) {
    using namespace r4::assets;
    if (!area || area->objectOverlay.empty())
        return nullptr;
    auto model = std::make_shared<Model>();
    model->source = "overlays";
    // over the terrain, both faces, as they are (not lit)
    Material colors;
    colors.name = "overlay colors";
    colors.shading = Shading::Unlit;
    colors.vertexColors = true;
    colors.diffuse = {1, 1, 1, 1};
    colors.doubleSided = true;
    colors.decal = true;
    model->materials.push_back(colors);

    // a textured triangle of color c: the image c + 255, its clear texels in
    // the palette color c (the color the game gives it under GL_DECAL)
    std::map<int, int> textureMaterial;
    auto textured = [&](int c) {
        auto it = textureMaterial.find(c);
        if (it != textureMaterial.end())
            return it->second;
        int material = -1;
        RSImage *src = area->GetImageByID(c + 255);
        if (src && src->data && src->width && src->height) {
            const VGAPalette &pal = src->palette ? *src->palette : palette;
            const Texel &under = palette.colors[c & 0xff];
            Image img;
            img.name = std::string(src->name);
            img.width = (int)src->width;
            img.height = (int)src->height;
            img.channels = 4;
            img.pixels.resize((size_t)img.width * img.height * 4);
            for (int y = 0; y < img.height; y++)
                for (int x = 0; x < img.width; x++) {
                    const Texel &t = pal.colors[src->data[(size_t)y * img.width + x]];
                    const Texel &o = t.a ? t : under;
                    uint8_t *d = &img.pixels[((size_t)(img.height - 1 - y) * img.width + x) * 4];
                    d[0] = o.r, d[1] = o.g, d[2] = o.b, d[3] = 255;
                }
            model->images.push_back(std::move(img));
            Material m;
            m.name = std::string("overlay ") + src->name;
            m.shading = Shading::Unlit;
            m.diffuseImage = (int)model->images.size() - 1;
            m.decal = true;
            m.doubleSided = true;
            model->materials.push_back(m);
            material = (int)model->materials.size() - 1;
        }
        return textureMaterial[c] = material;
    };

    const float kTriUvScale = 128.0f; // the uvs of the .TRI: 0..125, 128 a whole tile
    const float kLift = 0.05f;        // a triangle over the ones drawn before it
    std::map<int, Mesh> meshes;
    for (const AreaOverlay &ov : area->objectOverlay) {
        if (!ov.vertices || ov.nbTriangles <= 0)
            continue;
        int rank = 0;
        for (uint16_t j : ov.drawList) {
            if (j >= 400)
                continue;
            const AreaOverlayTriangles &tri = ov.trianles[j];
            int material = tri.color != 255 ? textured(tri.color) : 0;
            if (material < 0)
                material = 0;
            const Texel &t = palette.colors[tri.color & 0xff];
            uint32_t color = material == 0 ? (uint32_t)t.r | (uint32_t)t.g << 8 | (uint32_t)t.b << 16 | 0xff000000u
                                           : 0xffffffffu;
            Mesh &m = meshes[material];
            for (int k = 0; k < 3; k++) {
                const AoVPoints &p = ov.vertices[tri.verticesIdx[k]];
                Vertex v;
                v.position = {(float)p.x, (float)p.y + rank * kLift, (float)p.z};
                v.normal = {0, 1, 0};
                v.uv = {tri.uv[k].u / kTriUvScale, 1 - tri.uv[k].v / kTriUvScale};
                v.color = color;
                m.vertices.push_back(v);
                m.indices.push_back((uint32_t)m.vertices.size() - 1);
            }
            rank++;
        }
    }
    Node root;
    root.name = "overlays";
    for (auto &[material, mesh] : meshes) {
        mesh.material = material;
        mesh.name = model->materials[material].name;
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

} // namespace r4bridge
