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

// The matrix of an object of the map: its position, then its 3 x 3
// orientation as SCRenderer::renderObjects turns it (libRealSpace's axes).
r4::Mat4 mapObjectMatrix(const MapObject &object);

// The matrix of a part of a mission: its position, then its azimuth, pitch
// and roll as SCRenderer::renderMissionObjects turns them (libRealSpace's axes).
r4::Mat4 missionPartMatrix(const MISN_PART &part);

} // namespace r4bridge
