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
std::shared_ptr<r4::assets::Model> terrainModel(RSArea *area, const VGAPalette &palette,
                                               const std::vector<float> &lodRanges = {0, 40000, 100000, 1e30f});

// The overlays of an area: its runways and the ground around them, as
// SCRenderer::renderMapOverlay draws them over the terrain:
// - the triangles of each overlay in the order of its drawList, each raised
//   a little over the ones before (the game draws them over each other with
//   GL_LEQUAL);
// - a triangle with a color of 255 has the palette color of its index only;
//   the others the image GetImageByID(color + 255), its clear texels taking
//   that palette color (the GL_DECAL of the game), uvs / 128.
// In libRealSpace's axes (Y up). Null when the area has no overlay.
std::shared_ptr<r4::assets::Model> overlayModel(RSArea *area, const VGAPalette &palette);

// The matrix of an object of the map: its position, then its 3 x 3
// orientation as SCRenderer::renderObjects turns it (libRealSpace's axes).
r4::Mat4 mapObjectMatrix(const MapObject &object);

// The matrix of a part of a mission: its position, then its azimuth, pitch
// and roll as SCRenderer::renderMissionObjects turns them (libRealSpace's axes).
r4::Mat4 missionPartMatrix(const MISN_PART &part);

} // namespace r4bridge
