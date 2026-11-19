//
//  TerrainDiag.h - ce que porte le terrain d'une zone (RSArea), en images :
//  ses types de terrain et ses textures, pour savoir où poser des objets
//  (les arbres sur le vert, les immeubles sur les villes).
//
#pragma once

#include <string>
#include <vector>

class RSArea;
struct PaletteRange;
struct VGAPalette;

// Écrit dans dir (qui doit exister), au niveau le plus fin (LVL0, 360 x 360
// sommets ; en haut le nord, l'axe -z de libRealSpace) :
// - terrain.ppm  : les couleurs du terrain (celles de RSArea) ;
// - types.ppm    : le type de terrain de chaque sommet, du ton le plus clair de sa rampe ;
// - textures.ppm : les triangles texturés, chacun de la couleur moyenne de son image ;
// - tex_<id>.ppm : chaque image utilisée, à sa taille ;
// - all_<id>.ppm : toutes les textures du jeu (TXMPACK, ACCPACK), utilisées ou non ;
// - types.txt, textures.txt : les comptes (sommets par type, triangles par image,
//   et sur quels types de terrain chaque image est posée).
// false (et pourquoi) en cas d'erreur.
bool terrainDiagnostic(RSArea *area, const VGAPalette &palette, const std::vector<PaletteRange> &ranges,
                       const std::string &dir, std::string &error);
