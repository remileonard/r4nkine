// r4/render/renderer.h - culls the graph, sorts the draws by state (the transparent from back to front) and draws runs of the same mesh, material and pipeline as one instanced draw.
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

struct GpuMesh {
    gpu::BufferHandle vertices;
    gpu::BufferHandle indices;
    uint32_t indexCount = 0;
    assets::Primitive primitive = assets::Primitive::TRIANGLES;
    Sphere bounds;
    Box box;
    std::string name;
};

struct MaterialInstance {
    assets::Material params;
    gpu::TextureHandle texture;
    gpu::TextureHandle indexTexture;
    gpu::BufferHandle uniforms;
    uint32_t id = 0; // for sorting
};

struct Camera {
    Vec3 position;
    Mat4 view;
    Mat4 projection;
    uint32_t mask = 0xffffffffu;
    float znear = 0.1f;
    float zfar = 1000.0f;
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
    Vec3 lightDirection{-0.4f, -0.3f, -0.85f}; // where the light goes, in world space
    Vec3 lightColor{1, 1, 1};
    Vec3 ambient{0.25f, 0.25f, 0.28f};
    Vec4 clearColor{0.08f, 0.09f, 0.14f, 1};
    bool fog = false;
    Vec3 fogColor{0.6f, 0.65f, 0.75f};
    float fogStart = 100;
    float fogEnd = 1000;
    float time = 0; // seconds, for the animated textures
};

struct RenderOptions {
    // off: the pass keeps the color and the depth of its target, for a host that drew before
    bool clear = true;
    bool frustumCulling = true; // off: everything drawn
    bool freezeCulling = false; // the culling and the LODs keep the eye of the moment it was set
    bool sortByState = true;    // off: in the order of the graph
    bool instancing = true;     // off: one draw per item
    bool textures = true;
    bool lighting = true; // off: everything unlit
    float lodScale = 1;   // multiplies the distances of the LODs and instancers
    // the normal of the face instead of those of the vertices
    bool flatShading = false;
    // PALETTE_RAMP lit at the vertices and interpolated; the back of a two-sided face lit by its face normal
    bool rampGouraud = false;
    // for the textures uploaded from then on; 1: off
    float anisotropy = 1;
    // one shadow map, an orthographic view from the light over the view up to shadowDistance, faded at its end
    bool shadows = false;
    int shadowMapSize = 2048;
    float shadowDistance = 200;
    // how far towards the light the casters are taken beyond the view covered
    float shadowCasterReach = 2000;
    // the state of the shadow pass, with the mask of the camera: a node whose mask shares no bit with it casts no shadow, nor its children
    uint32_t shadowMask = 0xffffffffu;
    // the part of the light the unlit materials lose in the shadow
    float shadowStrength = 0.5f;
    // against the acne: the receivers pushed along their normal by this many texels, the depth compared minus this bias
    float shadowNormalOffset = 1.5f;
    float shadowBias = 0.0005f;
};

struct RenderStats {
    uint32_t nodesVisited = 0;
    uint32_t nodesCulled = 0;
    uint32_t drawables = 0;
    uint32_t instancesVisited = 0;
    uint32_t instancesCulled = 0;
    uint32_t opaqueItems = 0;
    uint32_t transparentItems = 0;
    uint32_t shadowCasters = 0;
    double shadowMs = 0; // its cull included
    double cullMs = 0;   // CPU times of the frame
    double sortMs = 0;
    double uploadMs = 0;
    double drawMs = 0;
    gpu::FrameStats gpu;
};

class Renderer {
public:
    explicit Renderer(gpu::Device &device);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    gpu::Device &device() {
        return device_;
    }

    // its parts for an Instancer: scene::collectParts(*upload(model))
    scene::NodePtr upload(const assets::Model &model);
    std::shared_ptr<GpuMesh> uploadMesh(const assets::Mesh &mesh);
    std::shared_ptr<MaterialInstance> createMaterial(const assets::Material &m, gpu::TextureHandle texture = {}, gpu::TextureHandle indexTexture = {});
    gpu::TextureHandle uploadImage(const assets::Image &image, bool mipmaps = true);

    // the viewport in pixels, from the bottom left
    void render(scene::Node &root, const Camera &camera, int x, int y, int width, int height);

    Environment &environment() {
        return env_;
    }
    RenderOptions &options() {
        return options_;
    }
    void setWireframe(bool on) {
        wireframe_ = on;
    }
    // a render target, or the window when the handle is null
    void setTarget(gpu::RenderTargetHandle target) {
        target_ = target;
    }
    gpu::RenderTargetHandle target() const {
        return target_;
    }
    bool wireframe() const {
        return wireframe_;
    }
    const RenderStats &stats() const {
        return stats_;
    }
    const std::string &error() const {
        return error_;
    }

private:
    struct DrawItem {
        GpuMesh *mesh;
        MaterialInstance *material;
        Mat4 world;
        float depth;
        int order;
        uint64_t key;
        gpu::PipelineHandle pipeline;
    };
    void cull(scene::Node &node, const Mat4 &parent, const Frustum &frustum, bool inside, uint32_t state, Vec3 eye);
    void cullInstancer(scene::Instancer &node, const Mat4 &world, const Frustum &frustum, bool inside, Vec3 eye);
    void addItem(const scene::Drawable &d, const Mat4 &world, Vec3 eye);
    gpu::PipelineHandle pipelineFor(const assets::Material &m, assets::Primitive primitive);
    bool createShaders();
    // false: no shadow
    bool shadowView(const Camera &camera, Mat4 &lightViewProj, float &texel);
    void drawShadows(size_t base);

    gpu::Device &device_;
    Environment env_;
    RenderOptions options_;
    gpu::RenderTargetHandle target_;
    RenderStats stats_;
    // kept while it is frozen
    Mat4 cullViewProj_;
    Vec3 cullEye_;
    bool frozen_ = false;
    std::string error_;
    bool wireframe_ = false;
    gpu::ShaderHandle shader_;
    std::unordered_map<uint32_t, gpu::PipelineHandle> pipelines_;
    gpu::BufferHandle frameUniforms_;
    gpu::BufferHandle instanceBuffer_;
    size_t instanceCapacity_ = 0;
    std::vector<float> instanceData_;
    std::vector<DrawItem> opaque_;
    std::vector<DrawItem> transparent_;
    std::vector<DrawItem> casters_;
    gpu::ShaderHandle shadowShader_;
    gpu::PipelineHandle shadowPipeline_;
    gpu::RenderTargetHandle shadowMap_;
    int shadowMapSize_ = 0;
    std::shared_ptr<MaterialInstance> defaultMaterial_;
    gpu::TextureHandle white_;
    uint32_t nextMaterialId_ = 1;
    std::vector<gpu::TextureHandle> ownedTextures_;
    // the mean color of the opaque texels of each uploaded texture, drawn in its place without textures
    std::unordered_map<uint32_t, Vec4> textureMeans_;
};

} // namespace r4::render
