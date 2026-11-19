//
//  R4World.h
//  libRealSpace
//
//  The worlds of libRealSpace for R4nkine: the terrain of an area (RSArea),
//  and the places of the objects of the map (MapObject) and of a mission
//  (MISN_PART), as SCRenderer draws them.
//
//  All of it stays in libRealSpace's axes (Y up): the models of the objects
//  are made with Axes::RealSpace, and the whole world is turned once, at its
//  root, by realSpaceToR4(). That matrix is the only place where the two
//  conventions meet.
//
#pragma once

#include "R4Bridge.h"

#include "r4/assets/model.h"
#include "r4/scene/node.h"

#include <map>
#include <set>
#include <memory>
#include <vector>

class RSArea;
struct MapObject;
struct MISN_PART;
struct VGAPalette;

namespace r4bridge {

// The terrain of an area: a group of its 18 x 18 blocks ("block x,y"), each
// a LOD node over its 3 levels of RSArea (LVL0 400 vertices, LVL1 100,
// LVL2 25), the seams with its neighbors at the same level included, as
// SCRenderer::renderBlock draws them:
// - every triangle with its colors (lit by RSArea), unlit, as the game;
// - the textured ones (TXMPACK, ACCPACK) also with their image over the colors
//   (the GL_DECAL of the game), in the palette of the game, index 255 clear.
// lodRanges: those of the LOD nodes (level i from ranges[i] to ranges[i+1]).
// The game draws all its blocks at one level (world_detail); the default
// here: the finest up to 2 blocks (40000), the coarsest beyond 5 (100000).
// In libRealSpace's axes (Y up). Null when the area has no blocks.
// The light of the terrain:
// - baked (the default): the colors of RSArea, lit by the light of the data
//   (the flag of each vertex along the ramp of its terrain type), unlit;
// - dynamic: the brightest tone of the ramp of each triangle's terrain type
//   (RSArea::terrainTone), darkened along that ramp by the sun as the objects
//   (PaletteRamp: ambient + light * N.L), with normals made from the relief;
//   the textures times that intensity.
struct TerrainLight {
    bool dynamic = false;
    const std::vector<PaletteRange> *ranges = nullptr; // the ramps of the palette
    Lighting lighting;
    // the heights multiplied by it for the normals only (not the geometry):
    // the relief of the maps is gentle (a vertex every 1000), the light of
    // the data exaggerates it
    float relief = 1;
};
std::shared_ptr<r4::assets::Model> terrainModel(RSArea *area, const VGAPalette &palette,
                                               const std::vector<float> &lodRanges = {0, 40000, 100000, 1e30f},
                                               const TerrainLight &light = {});

// The overlays of an area: its runways and the ground around them, as
// SCRenderer::renderMapOverlay draws them over the terrain: a mesh by
// overlay, its triangles in the order of its drawList, drawn without depth
// after the terrain (drawOrder 1), each over the ones before (the GL_LEQUAL
// of the game). A triangle of color 255 has the palette color of its index;
// the others the image GetImageByID(color + 255), uvs / 128, its clear texels
// in that palette color (the GL_DECAL of the game); the images in one atlas.
// In libRealSpace's axes (Y up). Null when the area has no overlay.
std::shared_ptr<r4::assets::Model> overlayModel(RSArea *area, const VGAPalette &palette);

// The forests of an area: trees on its green (terrain type 3, the green ramp
// of the palette), generated: RSArea has none, the game paints them only in
// some textures (217 on TURKEY).
// - each triangle of LVL0 whose type is green (3, or 4 the jungle; that of
//   its highest vertex, as RSArea) and without water gets trees at random
//   (broadleaves in the jungle), by its area and a
//   density: a noise of a few km makes woods and clearings, and the textures
//   of woods (images) have their own density;
// - none near the runways (overlays) and the places of keepOut;
// - the same trees at each load (a seed).
// The trees by chunks of chunk x chunk (a whole number of cells), each its
// matrices by species (the species of treeModel), in libRealSpace's axes (Y up).
struct ForestSettings {
    float density = 1;                                     // multiplies all the densities
    // on the green: perKm2 x 25 at the heart of the woods (a noise of a few km),
    // perKm2 x 0.1 between them
    float perKm2 = 30;
    float treeScale = 1.4f;                                // the size of the trees
    // the jungle (type 4) is most of its map: its woods larger, less dense
    float jungleDensity = 0.4f;
    // per km2 under these textures: the woods of TURKEY, the jungle of NICARAGA
    std::map<int, float> images{{217, 250}, {216, 120}, {218, 300}, {219, 300}};
    // no tree under these: the city, the harbor
    std::set<int> bare{224, 225, 229, 231};
    // the side of a chunk (a whole number of cells): small, so that the
    // chunks out of the view are left whole, few trees tested one by one
    float chunk = 2000;
    std::vector<r4::Vec3> keepOut;                         // x, z, radius (libRealSpace)
    uint32_t seed = 1;
};
// the species of treeModel: a conifer and a broadleaf in the green of the
// meadows (the ramp from 64), the same two in the green of the jungle (from 128)
constexpr int kTreeSpecies = 4;

// The chunks of the map with some green, without their trees: the trees of a
// chunk are planted when it is needed (plantForest), the same at each time.
struct ForestChunk {
    int cx = 0, cz = 0;      // its place in the grid of chunks
    r4::Vec3 center;         // its bounds, the trees included
    float radius = 0;
    std::vector<r4::scene::Placement> trees[kTreeSpecies]; // by species, about Y (libRealSpace's up)
};
std::vector<ForestChunk> forestChunks(RSArea *area, const ForestSettings &settings = {});
void plantForest(RSArea *area, const ForestSettings &settings, ForestChunk &chunk);

// A tree, low poly with flat faces, in the palette (PaletteRamp, the shades
// of the green and brown ramps): species 0 a conifer, 1 a broadleaf (2, 3: the same
// in the darker green of the jungle); simple:
// the simpler one for the distance (a pyramid, an octahedron). About 25 high
// (the scale of the objects of the game, 2.1 times real), its foot at 0.
// In libRealSpace's axes (Y up).
std::shared_ptr<r4::assets::Model> treeModel(int species, bool simple, const VGAPalette &palette,
                                            const std::vector<PaletteRange> &ranges, Lighting lighting = {});

// The buildings of the cities, villages, farms and harbors: on the cells
// whose texture shows them (by its image of TXMPACK, the same on every map),
// boxes standing on what the image shows, their roofs that image (seen from
// above they are the texture), their walls a facade made here (offices,
// houses, sheds), lit by the sun (PaletteRamp) and casting shadows:
// - the downtowns (229, 230), the harbor (223-228, a triangle each half of
//   the cell), the farm (233), the villages (248-251): their buildings read
//   on the images, by hand (kBuildings);
// - the houses (231): their light grey roofs, found on the image.
// None where an object of the map or of the mission stands: keepOut, x, z
// and radius each (libRealSpace's axes). A node by cell ("city x,y"). In
// libRealSpace's axes (Y up). Null without any.
std::shared_ptr<r4::assets::Model> cityModel(RSArea *area, const VGAPalette &palette, Lighting lighting = {},
                                            const std::vector<r4::Vec3> &keepOut = {}, size_t *buildings = nullptr);

// The matrix of an object of the map: its position, then its 3 x 3
// orientation as SCRenderer::renderObjects turns it (libRealSpace's axes).
r4::Mat4 mapObjectMatrix(const MapObject &object);

// The matrix of a part of a mission: its position, then its azimuth, pitch
// and roll as SCRenderer::renderMissionObjects turns them (libRealSpace's axes).
r4::Mat4 missionPartMatrix(const MISN_PART &part);

} // namespace r4bridge
