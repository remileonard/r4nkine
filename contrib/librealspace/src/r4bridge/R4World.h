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

class RSArea;
struct MapObject;
struct MISN_PART;
struct VGAPalette;

namespace r4bridge {

// The terrain of an area: its 18 x 18 blocks at one level of detail (0 the
// finest, 400 vertices a block; 1: 100; 2: 25), the seams between the blocks
// included, as SCRenderer::renderBlock draws them:
// - every triangle with its colors (lit by RSArea), unlit, as the game;
// - the textured ones (TXMPACK, ACCPACK) also with their image over the colors
//   (the GL_DECAL of the game), clear where their palette is (its alpha).
// In libRealSpace's axes (Y up). Null when the area has no blocks.
std::shared_ptr<r4::assets::Model> terrainModel(RSArea *area, const VGAPalette &palette, int lod = 0);

// The matrix of an object of the map: its position, then its 3 x 3
// orientation as SCRenderer::renderObjects turns it (libRealSpace's axes).
r4::Mat4 mapObjectMatrix(const MapObject &object);

// The matrix of a part of a mission: its position, then its azimuth, pitch
// and roll as SCRenderer::renderMissionObjects turns them (libRealSpace's axes).
r4::Mat4 missionPartMatrix(const MISN_PART &part);

} // namespace r4bridge
