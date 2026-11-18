// r4/assets/model.h - the neutral format every loader produces.
//
// The space of R4nkine is Z-up, as Performer and the flight simulators: X
// to the right, Y forward, Z up, in meters. The loaders of Y-up formats (OBJ,
// glTF) turn their root node; the meshes keep the coordinates of the file.
//
// A model is a tree of nodes holding meshes; meshes reference materials,
// materials reference images. Nothing here touches the GPU: r4::scene builds
// the GPU objects from it. So a game can swap any asset for another format
// (an old RealSpace object for a glTF one) without its code changing.
#pragma once

#include "r4/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r4::assets {

// an image in memory: rows from the bottom (OpenGL order), 8 bits per channel
struct Image {
    std::string name; // the file it came from
    int width = 0, height = 0, channels = 0; // 1 grey, 2 grey+alpha, 3 RGB, 4 RGBA
    std::vector<uint8_t> pixels;
    bool clampS = false, clampT = false; // the wrap: clamped to the edges, else repeated
    bool mipmaps = true; // false: filtered without mipmaps (the textures of the old games, the atlases)
    bool valid() const { return width > 0 && height > 0 && channels > 0; }
};

// how a material is lit
enum class Shading {
    Unlit,       // the color as is
    Phong,       // the per-pixel Blinn-Phong of the classic demos
    // lit along the ramps of a fixed 256-color palette, as the 8-bit games:
    // the palette index in the red of the vertex colors, the table of the
    // shades (256 colors x 32 intensities, made by the loader) as the image
    // of the material; or, rampTextured, a texture times the intensity
    PaletteRamp,
};

// Opaque; Mask: cut where alpha < alphaCutoff; Blend: mixed by alpha over
// what is behind; Additive: added to what is behind (glows, the glass of the
// 8-bit games). Blend and Additive are drawn after the opaque, far to near.
enum class AlphaMode { Opaque, Mask, Blend, Additive };

struct Material {
    std::string name;
    Shading shading = Shading::Phong;
    Vec4 diffuse{0.8f, 0.8f, 0.8f, 1.0f};
    Vec3 ambient{0.2f, 0.2f, 0.2f};
    Vec3 specular{0, 0, 0};
    Vec3 emissive{0, 0, 0};
    float shininess = 16;
    int diffuseImage = -1; // an index in Model::images
    bool sphereMap = false; // the texture coordinates from the normals (environment map)
    AlphaMode alpha = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool vertexColors = false; // the vertex colors multiply the diffuse
    bool depthWrite = true;    // false: drawn without writing the depth (flames, glows)
    // drawn over the coplanar faces before it (decals: runway markings,
    // the subfaces of OpenFlight)
    bool decal = false;
    // the bin of its draws, as the bins of Performer: the lower ones first,
    // the opaque then the transparent (Blend, Additive) in each (a decal
    // drawn without depth after the ground it lies on, the runways after
    // the terrain)
    int drawOrder = 0;
    // PaletteRamp: the intensity is rampAmbient + rampLight * max(N.L, 0)
    float rampAmbient = 0.375f, rampLight = 0.625f;
    bool rampTextured = false;
};

// how the indices of a mesh are read
enum class Primitive { Triangles, Lines, Points };

// the vertex of every mesh: the renderer uses one format, so that any mesh
// can go with any material in one pipeline
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    uint32_t color = 0xffffffffu; // RGBA, R in the low byte
};

struct Mesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices; // by 3 (triangles), 2 (lines) or 1 (points)
    Primitive primitive = Primitive::Triangles;
    int material = -1;             // an index in Model::materials, -1: the default
    Box bounds;
    void computeBounds();
    // normals from the triangles, for the meshes that have none: smooth
    // across the edges where the faces meet at less than creaseDegrees
    // (a sphere, a fuselage), sharp beyond (the edges of a box), the
    // vertices of a sharp edge split. The "crease angle" of the Performer tools.
    void computeNormals(float creaseDegrees = 60);
};

enum class NodeKind { Group, Switch, LOD, Billboard };

struct Node {
    std::string name;
    NodeKind kind = NodeKind::Group;
    Mat4 local;                  // relative to the parent
    std::vector<int> meshes;     // indices in Model::meshes (none on a Switch or LOD)
    std::vector<int> children;   // indices in Model::nodes
    // the state bits the node belongs to: drawn when they meet the state of
    // the camera (the statebits of the flight objects, the masks of pfb)
    uint32_t mask = 0xffffffffu;
    int switchValue = -1;            // Switch: the child shown, -1 all, -2 none
    std::vector<float> lodRanges;    // LOD: child i in [ranges[i], ranges[i+1])
    Vec3 lodCenter;
    bool hasLodCenter = false;
    // Billboard: its meshes turned to the eye about Z (axis) or about Z and
    // X (point); they are made facing -Y
    bool billboardPoint = false;
};

struct Model {
    std::string source; // the file
    std::vector<Node> nodes;   // nodes[0] is the root
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Image> images;
    // the state to draw the model in when nothing else is said (a plane of
    // flight: its body, landing gear and weapons, not its shadows)
    uint32_t defaultState = 0xffffffffu;
    Box bounds() const;        // in the space of the root
};

// the loaders, chosen by the extension of the file; null with the reason in
// *error when it cannot be read
std::shared_ptr<Model> loadModel(const std::string &path, std::string *error = nullptr);
// the extensions known, lowercase without the dot
std::vector<std::string> modelExtensions();

// the folders where the files named by the models (textures, external
// references) are searched after the folder of the model, as the file path
// of Performer (pfFilePath, the -F option of perfly)
void addSearchPath(const std::string &folder);
const std::vector<std::string> &searchPaths();
// a file named in a model of the folder dir: as given (absolute or relative
// to dir), then by its base name in dir, ../textures, textures and the
// search paths; empty when not found
std::string findAsset(const std::string &dir, const std::string &name);

// the images: SGI (.rgb .rgba .bw .int .inta .sgi), PPM/PGM, the scrambled
// 5551E of perfly.face, PNG, JPEG, BMP, TGA, GIF (stb_image); by their
// content, not their extension
bool loadImage(const std::string &path, Image &image, std::string *error = nullptr);
bool decodeImage(const uint8_t *data, size_t size, Image &image, std::string *error = nullptr);

// the Wavefront OBJ (and its MTL materials) from a file
std::shared_ptr<Model> loadOBJ(const std::string &path, std::string *error);
// the .d objects of the SGI flight simulator (libgobj, text form)
std::shared_ptr<Model> loadGObj(const std::string &path, std::string *error);
// the YAODL objects of powerflip and flip (.ydl, .defs)
std::shared_ptr<Model> loadYAODL(const std::string &path, std::string *error);
// the .bin objects of flip (quadrilaterals with normals)
std::shared_ptr<Model> loadFlipBin(const std::string &path, std::string *error);
// the binary files of IRIS Performer (.pfb, versions 1 and 2)
std::shared_ptr<Model> loadPFB(const std::string &path, std::string *error);
// the Designer's Workbench files (.dwb) of the F-18 demo
std::shared_ptr<Model> loadDWB(const std::string &path, std::string *error);
// the line drawings of the Mekton demo (.gset)
std::shared_ptr<Model> loadGSet(const std::string &path, std::string *error);
// OpenFlight (.flt), versions 11, 12 and 14.2, with its external references
std::shared_ptr<Model> loadFLT(const std::string &path, std::string *error);
// glTF 2.0 (.gltf, .glb)
std::shared_ptr<Model> loadGLTF(const std::string &path, std::string *error);

} // namespace r4::assets
