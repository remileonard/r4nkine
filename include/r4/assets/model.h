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
    bool valid() const { return width > 0 && height > 0 && channels > 0; }
};

// how a material is lit
enum class Shading {
    Unlit,       // the color as is
    Phong,       // the per-pixel Blinn-Phong of the classic demos
    PaletteRamp, // lit along a ramp of a fixed 256 color palette (to come)
};

enum class AlphaMode { Opaque, Mask, Blend };

struct Material {
    std::string name;
    Shading shading = Shading::Phong;
    Vec4 diffuse{0.8f, 0.8f, 0.8f, 1.0f};
    Vec3 ambient{0.2f, 0.2f, 0.2f};
    Vec3 specular{0, 0, 0};
    Vec3 emissive{0, 0, 0};
    float shininess = 16;
    int diffuseImage = -1; // an index in Model::images
    AlphaMode alpha = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool vertexColors = false; // the vertex colors multiply the diffuse
};

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
    std::vector<uint32_t> indices; // triangles
    int material = -1;             // an index in Model::materials, -1: the default
    Box bounds;
    void computeBounds();
    // smooth normals from the triangles, for the meshes that have none
    void computeNormals();
};

struct Node {
    std::string name;
    Mat4 local;                  // relative to the parent
    std::vector<int> meshes;     // indices in Model::meshes
    std::vector<int> children;   // indices in Model::nodes
};

struct Model {
    std::string source; // the file
    std::vector<Node> nodes;   // nodes[0] is the root
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Image> images;
    Box bounds() const;        // in the space of the root
};

// the loaders, chosen by the extension of the file; null with the reason in
// *error when it cannot be read
std::shared_ptr<Model> loadModel(const std::string &path, std::string *error = nullptr);
// the extensions known, lowercase without the dot
std::vector<std::string> modelExtensions();

// the images: SGI (.rgb .rgba .bw .sgi), PPM/PGM; others to come
bool loadImage(const std::string &path, Image &image, std::string *error = nullptr);

// the Wavefront OBJ (and its MTL materials) from a file
std::shared_ptr<Model> loadOBJ(const std::string &path, std::string *error);

} // namespace r4::assets
