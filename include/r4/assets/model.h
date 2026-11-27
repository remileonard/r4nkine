// r4/assets/model.h - the format every loader produces, a tree of nodes, meshes, materials and images, independent of the GPU.
// Z-up, in meters (r4/axes.h): the loaders of Y-up formats turn their root node by axes::fromYUpMatrix().
#pragma once

#include "r4/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r4::assets {

// rows from the bottom (OpenGL order), 8 bits per channel
struct Image {
    std::string name;
    int width = 0;
    int height = 0;
    int channels = 0; // 1 grey, 2 grey+alpha, 3 RGB, 4 RGBA
    std::vector<uint8_t> pixels;
    bool clampS = false; // else repeated
    bool clampT = false;
    bool mipmaps = true;
    bool valid() const {
        return width > 0 && height > 0 && channels > 0;
    }
};

enum class Shading {
    UNLIT,
    PHONG,
    // the palette index in the red of the vertex colors, the image the table of the shades (256 colors x 32 intensities)
    PALETTE_RAMP,
};

// MASK: cut under alphaCutoff; BLEND and ADDITIVE: drawn after the opaque, from far to near
enum class AlphaMode {
    NONE,
    MASK,
    BLEND,
    ADDITIVE
};

struct Material {
    std::string name;
    Shading shading = Shading::PHONG;
    Vec4 diffuse{0.8f, 0.8f, 0.8f, 1.0f};
    Vec3 ambient{0.2f, 0.2f, 0.2f};
    Vec3 specular{0, 0, 0};
    Vec3 emissive{0, 0, 0};
    float shininess = 16;
    int diffuseImage = -1;  // an index in Model::images
    int emissiveImage = -1; // an index in Model::images; it multiplies the emissive color
    // a tangent space normal map (OpenGL convention, +Y up), an index in Model::images; normalScale scales its X and Y
    int normalImage = -1;
    float normalScale = 1;
    // glTF: the metal in blue and the roughness in green multiply metallic and roughness; the ambient occlusion in red (its
    // strength: 1 the full texture, 0 none). Indices in Model::images.
    int metallicRoughnessImage = -1;
    int occlusionImage = -1;
    float occlusionStrength = 1;
    bool sphereMap = false;
    AlphaMode alpha = AlphaMode::NONE;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool vertexColors = false;
    bool depthWrite = true;
    // BLEND writing the depth all the same
    bool blendDepthWrite = false;
    // drawn only where the depth equals the one already there (the GL_EQUAL decals)
    bool depthEqual = false;
    // the texels under this alpha dropped, whatever the alpha mode
    float alphaTest = 0;
    // drawn over the coplanar faces drawn before it
    bool decal = false;
    // the bin of its draws, the lower first, as the bins of Performer; below 0 the background bins, drawn before all the others, blended or not
    int drawOrder = 0;
    // off: drawn whatever the depth already there
    bool depthTest = true;
    // off: never in the fog of the environment
    bool fog = true;
    bool castShadows = true;
    bool receiveShadows = true;
    // PALETTE_RAMP: the intensity is rampAmbient + rampLight * max(N.L, 0)
    float rampAmbient = 0.375f;
    float rampLight = 0.625f;
    bool rampTextured = false;
    // PALETTE_RAMP: an image of palette indices (red) and alpha, each texel shaded along the ramp of diffuseImage; the intensity is the red of the vertex colors with vertexColors
    int indexImage = -1;
    // indexImage as a grid of equal cells, columns x rows; the cell of a vertex is the green + 256 x blue of its color, the log2 of the width and height of its image in that cell the low and high nibbles of its alpha
    int indexAtlasColumns = 0;
    int indexAtlasRows = 0;
    // diffuseImage as frames stacked from top to bottom, shown in turn
    int frames = 1;
    float framesPerSecond = 0;
    // a PBR material (glTF) with roughness >= 0: it reflects the environment of the renderer, with the Fresnel effect. A metal
    // (metallic 1) has no diffuse light: its color is its reflection and its highlight. roughness < 0: Phong only.
    float metallic = 0;
    float roughness = -1;
};

enum class Primitive {
    TRIANGLES,
    LINES,
    POINTS
};

// one vertex format for every mesh, so that any mesh goes with any material
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    uint32_t color = 0xffffffffu; // RGBA, R in the low byte
};

struct Mesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Primitive primitive = Primitive::TRIANGLES;
    int material = -1; // -1: the default material
    Box bounds;
    void computeBounds();
    // smooth across the edges whose faces meet under creaseDegrees, split beyond
    void computeNormals(float creaseDegrees = 60);
};

enum class NodeKind {
    GROUP,
    SWITCH,
    LOD,
    BILLBOARD
};

struct Node {
    std::string name;
    NodeKind kind = NodeKind::GROUP;
    Mat4 local;
    std::vector<int> meshes;
    std::vector<int> children;
    // drawn when it meets the state of the camera
    uint32_t mask = 0xffffffffu;
    int switchValue = -1;         // -1 all, -2 none
    std::vector<float> lodRanges; // child i in [ranges[i], ranges[i+1])
    Vec3 lodCenter;
    bool hasLodCenter = false;
    // BILLBOARD: turned to the eye about Z (axis) or about Z and X (point), made facing -Y
    bool billboardPoint = false;
};

struct Model {
    std::string source;
    std::vector<Node> nodes; // nodes[0] is the root
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Image> images;
    // the state drawn when the camera gives none
    uint32_t defaultState = 0xffffffffu;
    Box bounds() const;
};

// by the extension of the file; null, and *error, when it cannot be read
std::shared_ptr<Model> loadModel(const std::string &path, std::string *error = nullptr);
std::vector<std::string> modelExtensions();

// the folders searched for the files a model names, as pfFilePath
void addSearchPath(const std::string &folder);
const std::vector<std::string> &searchPaths();
// as given, then by its base name in dir, ../textures, textures and the search paths; empty when not found
std::string findAsset(const std::string &dir, const std::string &name);

// SGI, PPM/PGM, 5551E, PNG, JPEG, BMP, TGA, GIF, by their content
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
// the other formats, read by Assimp when R4nkine is built with it (R4_WITH_ASSIMP); loadModel uses it for the extensions without a loader
std::shared_ptr<Model> loadAssimp(const std::string &path, std::string *error);
// the extensions Assimp reads; empty without Assimp
std::vector<std::string> assimpExtensions();

} // namespace r4::assets
