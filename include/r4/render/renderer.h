// r4/render/renderer.h - from the scene graph to the GPU.
//
// Each frame: the graph is culled against the view (bounding spheres, a
// branch fully inside is not tested further), the visible drawables go to a
// draw list, the list is sorted (opaque by pipeline, material and mesh to
// keep the state changes few; transparent from back to front), the matrices
// of the objects are written in one buffer of instances, then the draws are
// sent: the consecutive items with the same mesh, material and pipeline go
// in one instanced draw (a forest of one tree model is a few draw calls).
#pragma once

#include "r4/axes.h"
#include "r4/assets/model.h"
#include "r4/gpu/device.h"
#include "r4/math.h"
#include "r4/scene/node.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace r4::render {

// a mesh on the GPU
struct GpuMesh {
    gpu::BufferHandle vertices, indices;
    uint32_t indexCount = 0;
    assets::Primitive primitive = assets::Primitive::Triangles;
    Sphere bounds;
    std::string name;
};

// a material on the GPU: its parameters in a uniform buffer
struct MaterialInstance {
    assets::Material params;
    gpu::TextureHandle texture;
    gpu::BufferHandle uniforms;
    uint32_t id = 0; // for sorting
};

struct Camera {
    Vec3 position;
    Mat4 view, projection;
    uint32_t mask = 0xffffffffu;
    float znear = 0.1f, zfar = 1000.0f;
    void lookAt(Vec3 eye, Vec3 center, Vec3 up = axes::kUp) {
        position = eye;
        view = glm::lookAt(eye, center, up);
    }
    void perspective(float fovyDeg, float aspect, float n, float f) {
        znear = n;
        zfar = f;
        projection = glm::perspective(radians(fovyDeg), aspect, n, f);
    }
};

struct Environment {
    Vec3 lightDirection{-0.4f, -0.3f, -0.85f}; // where the light goes, world space
    Vec3 lightColor{1, 1, 1};
    Vec3 ambient{0.25f, 0.25f, 0.28f};
    Vec4 clearColor{0.08f, 0.09f, 0.14f, 1};
    bool fog = false;
    Vec3 fogColor{0.6f, 0.65f, 0.75f};
    float fogStart = 100, fogEnd = 1000;
};

// the switches of the renderer, for the viewers and the measures
struct RenderOptions {
    bool frustumCulling = true;  // off: everything is drawn
    bool freezeCulling = false;  // the culling and the LODs keep the eye of the moment it was set
    bool sortByState = true;     // off: the opaque items in the order of the graph
    bool instancing = true;      // off: one draw per item
    bool textures = true;
    bool lighting = true;        // off: everything unlit
    float lodScale = 1;          // the distances of the LODs and instancers are multiplied by it
    // the normal of the face instead of the normals of the vertices: the flat
    // shading of the old games
    bool flatShading = false;
    // PaletteRamp: the intensity computed at the vertices and interpolated
    // (the Gouraud of the old games), not at each pixel; the back of a
    // two-sided face lit by its face normal, as RealSpace
    bool rampGouraud = false;
    // the anisotropic filtering of the textures uploaded from then on (1: off;
    // the device limits it)
    float anisotropy = 1;
    // the shadows of the sun: one shadow map, an orthographic view from the
    // light over the part of the view up to shadowDistance from the eye;
    // they fade out at its end
    bool shadows = false;
    int shadowMapSize = 2048;
    float shadowDistance = 200;
    // how far towards the light the casters are taken, beyond the view
    // covered (an aircraft high above the ground it shades)
    float shadowCasterReach = 2000;
    // the unlit materials (the baked terrain, the runways) darkened in the
    // shadow by this part; the lit ones lose their direct light
    float shadowStrength = 0.5f;
    // against the acne: the receivers pushed along their normal by this many
    // texels of the map, the depth compared minus this bias (0..1 of the range)
    float shadowNormalOffset = 1.5f;
    float shadowBias = 0.0005f;
};

struct RenderStats {
    uint32_t nodesVisited = 0, nodesCulled = 0, drawables = 0;
    uint32_t instancesVisited = 0, instancesCulled = 0; // of the instancers
    uint32_t opaqueItems = 0, transparentItems = 0;
    uint32_t shadowCasters = 0; // the items drawn in the shadow map
    double shadowMs = 0; // CPU time of the shadow pass (its cull included)
    double cullMs = 0, sortMs = 0, uploadMs = 0, drawMs = 0; // CPU times of the frame
    gpu::FrameStats gpu;
};

class Renderer {
public:
    explicit Renderer(gpu::Device &device);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    gpu::Device &device() { return device_; }

    // the GPU objects of a model, as a subgraph (nodes, geodes, drawables);
    // its parts for an Instancer: scene::collectParts(*upload(model))
    scene::NodePtr upload(const assets::Model &model);
    std::shared_ptr<GpuMesh> uploadMesh(const assets::Mesh &mesh);
    std::shared_ptr<MaterialInstance> createMaterial(const assets::Material &m, gpu::TextureHandle texture = {});
    gpu::TextureHandle uploadImage(const assets::Image &image, bool mipmaps = true);

    // draws the graph from the camera in the viewport (pixels, from the bottom left)
    void render(scene::Node &root, const Camera &camera, int x, int y, int width, int height);

    Environment &environment() { return env_; }
    RenderOptions &options() { return options_; }
    void setWireframe(bool on) { wireframe_ = on; }
    // where render() draws: a render target (a post process), or the window
    // when the handle is null
    void setTarget(gpu::RenderTargetHandle target) { target_ = target; }
    gpu::RenderTargetHandle target() const { return target_; }
    bool wireframe() const { return wireframe_; }
    const RenderStats &stats() const { return stats_; }
    const std::string &error() const { return error_; }

private:
    struct DrawItem {
        GpuMesh *mesh;
        MaterialInstance *material;
        Mat4 world;
        float depth;
        int order; // the drawOrder of its material
        uint64_t key;
        gpu::PipelineHandle pipeline;
    };
    void cull(scene::Node &node, const Mat4 &parent, const Frustum &frustum, bool inside, uint32_t state,
              Vec3 eye);
    void cullInstancer(scene::Instancer &node, const Mat4 &world, const Frustum &frustum, bool inside, Vec3 eye);
    void addItem(const scene::Drawable &d, const Mat4 &world, Vec3 eye);
    gpu::PipelineHandle pipelineFor(const assets::Material &m, assets::Primitive primitive);
    bool createShaders();
    // the view of the shadow map over the camera's view; false: no shadow
    bool shadowView(const Camera &camera, Mat4 &lightViewProj, float &texel);
    void drawShadows(size_t base);

    gpu::Device &device_;
    Environment env_;
    RenderOptions options_;
    gpu::RenderTargetHandle target_;
    RenderStats stats_;
    // the eye of the culling, kept while it is frozen
    Mat4 cullViewProj_;
    Vec3 cullEye_;
    bool frozen_ = false;
    std::string error_;
    bool wireframe_ = false;
    gpu::ShaderHandle shader_;
    std::unordered_map<uint32_t, gpu::PipelineHandle> pipelines_;
    gpu::BufferHandle frameUniforms_, instanceBuffer_;
    size_t instanceCapacity_ = 0;
    std::vector<float> instanceData_;
    std::vector<DrawItem> opaque_, transparent_, casters_;
    gpu::ShaderHandle shadowShader_;
    gpu::PipelineHandle shadowPipeline_;
    gpu::RenderTargetHandle shadowMap_;
    int shadowMapSize_ = 0;
    std::shared_ptr<MaterialInstance> defaultMaterial_;
    gpu::TextureHandle white_;
    uint32_t nextMaterialId_ = 1;
    std::vector<gpu::TextureHandle> ownedTextures_;
};

} // namespace r4::render
