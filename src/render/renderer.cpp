#include "r4/render/renderer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>

namespace r4::render {
namespace {

// the uniform blocks (std140): the slots and layouts the shaders share
enum Slot : uint32_t { kFrameSlot = 0, kMaterialSlot = 2 };
// the matrices of the instances: a vertex stream read once per instance
constexpr uint32_t kInstanceStride = 64;

struct FrameBlock {
    float view[16], projection[16], viewProjection[16];
    float lightDirection[4]; // view space, towards the light
    float lightColor[4];
    float ambient[4];
    float fogColor[4];
    float fogParams[4]; // start, end, enabled
    float options[4];   // textures, lighting
};

struct MaterialBlock {
    float diffuse[4];
    float ambient[4];
    float specular[4]; // w: shininess
    float emissive[4];
    float flags[4]; // texture, alpha cutoff (0: none), vertex colors, unlit
    float flags2[4]; // sphere map
};

const char *kVertexShader = R"(#version 330
layout(std140) uniform Frame {
    mat4 view, projection, viewProjection;
    vec4 lightDirection, lightColor, ambientLight, fogColor, fogParams, options;
};
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
// the model matrix of the instance, by columns
layout(location = 4) in vec4 inModel0;
layout(location = 5) in vec4 inModel1;
layout(location = 6) in vec4 inModel2;
layout(location = 7) in vec4 inModel3;
out vec3 vNormal;
out vec3 vPosition;
out vec2 vUV;
out vec4 vColor;
void main() {
    mat4 modelView = view * mat4(inModel0, inModel1, inModel2, inModel3);
    vec4 p = modelView * vec4(inPosition, 1.0);
    vPosition = p.xyz;
    // the normals: the inverse transpose, right for non-uniform scales
    vNormal = transpose(inverse(mat3(modelView))) * inNormal;
    vUV = inUV;
    vColor = inColor;
    gl_Position = projection * p;
}
)";

const char *kFragmentShader = R"(#version 330
layout(std140) uniform Frame {
    mat4 view, projection, viewProjection;
    vec4 lightDirection, lightColor, ambientLight, fogColor, fogParams, options;
};
layout(std140) uniform Material {
    vec4 diffuse, ambient, specular, emissive, flags, flags2;
};
uniform sampler2D diffuseMap;
in vec3 vNormal;
in vec3 vPosition;
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec4 base = diffuse;
    vec2 uv = vUV;
    if (flags2.x > 0.5) {
        // the sphere map of IRIS GL and OpenGL 1.x
        vec3 u = normalize(vPosition);
        vec3 n = normalize(vNormal);
        if (dot(n, u) > 0.0) n = -n;
        vec3 r = reflect(u, n);
        float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
        uv = r.xy / m + 0.5;
    }
    if (flags.x > 0.5 && options.x > 0.5) base *= texture(diffuseMap, uv);
    if (flags.z > 0.5) base *= vColor;
    if (flags.y > 0.0 && base.a < flags.y) discard;
    vec3 color;
    if (flags.w > 0.5 || options.y < 0.5 || dot(vNormal, vNormal) < 1e-12) {
        color = base.rgb; // unlit, or no normal (lines, points)
    } else {
        // Blinn-Phong, both faces lit: the normal turned towards the eye
        // (whatever the winding of the faces, as the two-sided lighting)
        vec3 n = normalize(vNormal);
        if (dot(n, vPosition) > 0.0) n = -n;
        vec3 l = lightDirection.xyz;
        vec3 v = normalize(-vPosition);
        float nl = max(dot(n, l), 0.0);
        float spec = 0.0;
        if (nl > 0.0 && specular.w > 0.0)
            spec = pow(max(dot(n, normalize(l + v)), 0.0), specular.w);
        color = emissive.rgb + base.rgb * (ambient.rgb * ambientLight.rgb + lightColor.rgb * nl) +
                specular.rgb * lightColor.rgb * spec;
    }
    if (fogParams.z > 0.5) {
        float f = clamp((length(vPosition) - fogParams.x) / max(fogParams.y - fogParams.x, 1e-6), 0.0, 1.0);
        color = mix(color, fogColor.rgb, f);
    }
    fragColor = vec4(color, base.a);
}
)";

void copyMat(float *dst, const Mat4 &m) { std::memcpy(dst, dataOf(m), 64); }
void copyVec(float *dst, Vec3 v, float w) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = w;
}

gpu::VertexLayout vertexLayout() {
    gpu::VertexStream s;
    s.stride = sizeof(assets::Vertex);
    s.attribs = {
        {0, 3, gpu::AttribType::Float, false, (uint32_t)offsetof(assets::Vertex, position)},
        {1, 3, gpu::AttribType::Float, false, (uint32_t)offsetof(assets::Vertex, normal)},
        {2, 2, gpu::AttribType::Float, false, (uint32_t)offsetof(assets::Vertex, uv)},
        {3, 4, gpu::AttribType::UByte, true, (uint32_t)offsetof(assets::Vertex, color)},
    };
    gpu::VertexStream inst;
    inst.stride = kInstanceStride;
    inst.perInstance = true;
    for (uint32_t c = 0; c < 4; c++)
        inst.attribs.push_back({4 + c, 4, gpu::AttribType::Float, false, 16 * c});
    gpu::VertexLayout l;
    l.streams.push_back(s);
    l.streams.push_back(inst);
    return l;
}

} // namespace

Renderer::Renderer(gpu::Device &device) : device_(device) {
    createShaders();
    frameUniforms_ = device_.createBuffer(gpu::BufferUsage::Uniform, nullptr, sizeof(FrameBlock), true);
    uint8_t white[4] = {255, 255, 255, 255};
    gpu::TextureDesc td;
    td.width = td.height = 1;
    td.mipmaps = false;
    white_ = device_.createTexture(td, white);
    ownedTextures_.push_back(white_);
    defaultMaterial_ = createMaterial(assets::Material{});
}

Renderer::~Renderer() {
    defaultMaterial_.reset();
    for (auto &[key, p] : pipelines_)
        device_.destroyPipeline(p);
    for (gpu::TextureHandle t : ownedTextures_)
        device_.destroyTexture(t);
    device_.destroyBuffer(frameUniforms_);
    device_.destroyBuffer(instanceBuffer_);
    device_.destroyShader(shader_);
}

bool Renderer::createShaders() {
    gpu::ShaderDesc d;
    d.vertex = kVertexShader;
    d.fragment = kFragmentShader;
    d.uniformBlocks = {{"Frame", kFrameSlot}, {"Material", kMaterialSlot}};
    d.samplers = {{"diffuseMap", 0}};
    d.debugName = "phong";
    shader_ = device_.createShader(d);
    if (!shader_)
        error_ = device_.lastError();
    return (bool)shader_;
}

gpu::PipelineHandle Renderer::pipelineFor(const assets::Material &m, assets::Primitive primitive) {
    bool blend = m.alpha == assets::AlphaMode::Blend;
    uint32_t key = (blend ? 1u : 0u) | (m.doubleSided ? 2u : 0u) | (wireframe_ ? 4u : 0u) |
                   (m.depthWrite ? 0u : 8u) | (m.decal ? 16u : 0u) | (uint32_t)primitive << 8;
    auto it = pipelines_.find(key);
    if (it != pipelines_.end())
        return it->second;
    gpu::PipelineDesc d;
    d.shader = shader_;
    d.layout = vertexLayout();
    d.primitive = primitive == assets::Primitive::Lines    ? gpu::Primitive::Lines
                  : primitive == assets::Primitive::Points ? gpu::Primitive::Points
                                                           : gpu::Primitive::Triangles;
    bool tris = primitive == assets::Primitive::Triangles;
    d.cull = m.doubleSided || wireframe_ || !tris ? gpu::CullMode::None : gpu::CullMode::Back;
    d.blend = blend ? gpu::BlendMode::Alpha : gpu::BlendMode::Opaque;
    d.depthWrite = !blend && m.depthWrite;
    d.wireframe = wireframe_ && tris;
    if (m.decal) {
        d.polygonOffsetFactor = -1;
        d.polygonOffsetUnits = -2;
    }
    gpu::PipelineHandle p = device_.createPipeline(d);
    pipelines_[key] = p;
    return p;
}

gpu::TextureHandle Renderer::uploadImage(const assets::Image &image, bool mipmaps) {
    if (!image.valid())
        return {};
    gpu::TextureDesc d;
    d.width = image.width;
    d.height = image.height;
    d.mipmaps = mipmaps;
    d.wrapS = image.clampS ? gpu::Wrap::Clamp : gpu::Wrap::Repeat;
    d.wrapT = image.clampT ? gpu::Wrap::Clamp : gpu::Wrap::Repeat;
    // grey images are expanded: the shader reads RGBA
    std::vector<uint8_t> rgba;
    const void *pixels = image.pixels.data();
    switch (image.channels) {
    case 3: d.format = gpu::TextureFormat::RGB8; break;
    case 4: d.format = gpu::TextureFormat::RGBA8; break;
    default: {
        size_t n = (size_t)image.width * image.height;
        rgba.resize(n * 4);
        for (size_t i = 0; i < n; i++) {
            uint8_t g = image.pixels[i * image.channels];
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = g;
            rgba[i * 4 + 3] = image.channels == 2 ? image.pixels[i * 2 + 1] : 255;
        }
        pixels = rgba.data();
        d.format = gpu::TextureFormat::RGBA8;
    }
    }
    gpu::TextureHandle t = device_.createTexture(d, pixels);
    if (t)
        ownedTextures_.push_back(t);
    return t;
}

std::shared_ptr<GpuMesh> Renderer::uploadMesh(const assets::Mesh &mesh) {
    gpu::Device *dev = &device_;
    auto m = std::shared_ptr<GpuMesh>(new GpuMesh, [dev](GpuMesh *p) {
        dev->destroyBuffer(p->vertices);
        dev->destroyBuffer(p->indices);
        delete p;
    });
    m->name = mesh.name;
    m->vertices = device_.createBuffer(gpu::BufferUsage::Vertex, mesh.vertices.data(),
                                       mesh.vertices.size() * sizeof(assets::Vertex), false);
    m->indices = device_.createBuffer(gpu::BufferUsage::Index, mesh.indices.data(),
                                      mesh.indices.size() * sizeof(uint32_t), false);
    m->indexCount = (uint32_t)mesh.indices.size();
    m->primitive = mesh.primitive;
    Box b = mesh.bounds;
    if (b.empty())
        for (const assets::Vertex &v : mesh.vertices)
            b.extend(v.position);
    m->bounds = sphereOf(b);
    return m;
}

std::shared_ptr<MaterialInstance> Renderer::createMaterial(const assets::Material &p, gpu::TextureHandle texture) {
    gpu::Device *dev = &device_;
    auto m = std::shared_ptr<MaterialInstance>(new MaterialInstance, [dev](MaterialInstance *mi) {
        dev->destroyBuffer(mi->uniforms);
        delete mi;
    });
    m->params = p;
    m->texture = texture;
    m->id = nextMaterialId_++;
    MaterialBlock b{};
    b.diffuse[0] = p.diffuse.x;
    b.diffuse[1] = p.diffuse.y;
    b.diffuse[2] = p.diffuse.z;
    b.diffuse[3] = p.diffuse.w;
    copyVec(b.ambient, p.ambient, 1);
    copyVec(b.specular, p.specular, p.specular.x + p.specular.y + p.specular.z > 0 ? p.shininess : 0);
    copyVec(b.emissive, p.emissive, 1);
    b.flags[0] = texture ? 1.0f : 0.0f;
    b.flags[1] = p.alpha == assets::AlphaMode::Mask ? p.alphaCutoff : 0.0f;
    b.flags[2] = p.vertexColors ? 1.0f : 0.0f;
    b.flags[3] = p.shading == assets::Shading::Unlit ? 1.0f : 0.0f;
    b.flags2[0] = p.sphereMap ? 1.0f : 0.0f;
    m->uniforms = device_.createBuffer(gpu::BufferUsage::Uniform, &b, sizeof b, false);
    return m;
}

scene::NodePtr Renderer::upload(const assets::Model &model) {
    std::vector<gpu::TextureHandle> textures(model.images.size());
    for (size_t i = 0; i < model.images.size(); i++)
        textures[i] = uploadImage(model.images[i]);
    std::vector<std::shared_ptr<MaterialInstance>> materials;
    for (const assets::Material &m : model.materials)
        materials.push_back(createMaterial(
            m, m.diffuseImage >= 0 && m.diffuseImage < (int)textures.size() ? textures[m.diffuseImage]
                                                                             : gpu::TextureHandle{}));
    std::vector<std::shared_ptr<GpuMesh>> meshes;
    for (const assets::Mesh &m : model.meshes)
        meshes.push_back(uploadMesh(m));

    std::vector<scene::NodePtr> nodes(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); i++) {
        const assets::Node &n = model.nodes[i];
        scene::NodePtr node;
        if (n.kind == assets::NodeKind::Switch) {
            auto sw = scene::make<scene::Switch>();
            sw->setValue(n.switchValue == -1 ? scene::Switch::kAll : n.switchValue < -1 ? scene::Switch::kNone : n.switchValue);
            node = sw;
        } else if (n.kind == assets::NodeKind::LOD) {
            auto lod = scene::make<scene::LOD>();
            lod->setRanges(n.lodRanges);
            if (n.hasLodCenter)
                lod->setCenter(n.lodCenter);
            node = lod;
        } else if (n.kind == assets::NodeKind::Billboard || !n.meshes.empty()) {
            std::shared_ptr<scene::Geode> g;
            if (n.kind == assets::NodeKind::Billboard) {
                auto b = scene::make<scene::Billboard>();
                b->setMode(n.billboardPoint ? scene::Billboard::Mode::Point : scene::Billboard::Mode::Axis);
                g = b;
            } else {
                g = scene::make<scene::Geode>();
            }
            for (int mi : n.meshes) {
                const assets::Mesh &mesh = model.meshes[mi];
                g->addDrawable({meshes[mi], mesh.material >= 0 && mesh.material < (int)materials.size()
                                                ? materials[mesh.material]
                                                : defaultMaterial_});
            }
            node = g;
        } else {
            node = scene::make<scene::Group>();
        }
        node->setName(n.name);
        node->setMatrix(n.local);
        node->setMask(n.mask);
        nodes[i] = node;
    }
    for (size_t i = 0; i < model.nodes.size(); i++)
        for (int c : model.nodes[i].children)
            if (c > 0 && c < (int)nodes.size())
                nodes[i]->addChild(nodes[c]);
    if (nodes.empty())
        return scene::make<scene::Group>();
    nodes[0]->setStateFilter(model.defaultState);
    return nodes[0];
}

void Renderer::addItem(const scene::Drawable &d, const Mat4 &world, Vec3 eye) {
    if (!d.mesh || !d.mesh->indexCount)
        return;
    MaterialInstance *mat = d.material ? d.material.get() : defaultMaterial_.get();
    DrawItem item;
    item.mesh = d.mesh.get();
    item.material = mat;
    item.world = world;
    Vec3 c = transformPoint(world, d.mesh->bounds.center);
    item.depth = length(c - eye);
    item.pipeline = pipelineFor(mat->params, d.mesh->primitive);
    item.key = (uint64_t)item.pipeline.id << 48 | (uint64_t)(mat->id & 0xffffff) << 24 |
               (uint64_t)((uintptr_t)item.mesh >> 4 & 0xffffff);
    (mat->params.alpha == assets::AlphaMode::Blend ? transparent_ : opaque_).push_back(item);
    stats_.drawables++;
}

// each instance culled on its own sphere, its level chosen by its distance
void Renderer::cullInstancer(scene::Instancer &node, const Mat4 &world, const Frustum &frustum, bool inside,
                             Vec3 eye) {
    const auto &levels = node.levels();
    if (levels.empty())
        return;
    Sphere one;
    for (const auto &l : levels)
        one.extend(l.bounds);
    for (const Mat4 &m : node.instances()) {
        stats_.instancesVisited++;
        Mat4 w = world * m;
        Sphere s = one.transformed(w);
        if (!inside && frustum.test(s) == Frustum::Outside) {
            stats_.instancesCulled++;
            continue;
        }
        int level = node.select(length(s.center - eye) * options_.lodScale);
        if (level < 0) {
            stats_.instancesCulled++;
            continue;
        }
        for (const scene::InstancePart &p : levels[level].parts)
            addItem(p.drawable, w * p.local, eye);
    }
}

void Renderer::cull(scene::Node &node, const Mat4 &parent, const Frustum &frustum, bool inside, uint32_t state,
                    Vec3 eye) {
    if (!(node.mask() & state))
        return;
    state &= node.stateFilter();
    stats_.nodesVisited++;
    if (!inside) {
        Frustum::Result r = frustum.test(node.bounds().transformed(parent));
        if (r == Frustum::Outside) {
            stats_.nodesCulled++;
            return;
        }
        inside = r == Frustum::Inside;
    }
    Mat4 world = parent * node.matrix();
    if (node.type() == scene::NodeType::Billboard) {
        // turned to the eye, about the origin of the node
        auto &bb = static_cast<scene::Billboard &>(node);
        world = world * bb.rotationFor(transformPoint(inverse(world), eye));
    }
    if (node.type() == scene::NodeType::Geode || node.type() == scene::NodeType::Billboard) {
        for (const scene::Drawable &d : static_cast<scene::Geode &>(node).drawables())
            addItem(d, world, eye);
    } else if (node.type() == scene::NodeType::Instancer) {
        cullInstancer(static_cast<scene::Instancer &>(node), world, frustum, inside, eye);
    }
    const auto &children = node.children();
    switch (node.type()) {
    case scene::NodeType::Switch: {
        auto &sw = static_cast<scene::Switch &>(node);
        for (size_t i = 0; i < children.size(); i++)
            if (sw.isActive((int)i))
                cull(*children[i], world, frustum, inside, state, eye);
        break;
    }
    case scene::NodeType::LOD: {
        auto &lod = static_cast<scene::LOD &>(node);
        Vec3 c = transformPoint(world, lod.center());
        int i = lod.select(length(c - eye) * options_.lodScale);
        if (i >= 0)
            cull(*children[i], world, frustum, inside, state, eye);
        break;
    }
    default:
        for (const scene::NodePtr &c : children)
            cull(*c, world, frustum, inside, state, eye);
    }
}

void Renderer::render(scene::Node &root, const Camera &cam, int x, int y, int width, int height) {
    stats_ = {};
    opaque_.clear();
    transparent_.clear();
    if (!shader_)
        return;

    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    auto t0 = clock::now();
    Mat4 viewProj = cam.projection * cam.view;
    // the eye of the culling: the camera, or the one kept while frozen
    if (!options_.freezeCulling || !frozen_) {
        cullViewProj_ = viewProj;
        cullEye_ = cam.position;
    }
    frozen_ = options_.freezeCulling;
    cull(root, Mat4(1.0f), Frustum::fromMatrix(cullViewProj_), !options_.frustumCulling, cam.mask, cullEye_);
    auto t1 = clock::now();
    if (options_.sortByState)
        std::sort(opaque_.begin(), opaque_.end(), [](const DrawItem &a, const DrawItem &b) { return a.key < b.key; });
    // the transparent ones from back to front, from the eye of the camera
    for (DrawItem &it : transparent_)
        it.depth = length(transformPoint(it.world, it.mesh->bounds.center) - cam.position);
    std::sort(transparent_.begin(), transparent_.end(),
              [](const DrawItem &a, const DrawItem &b) { return a.depth > b.depth; });
    auto t2 = clock::now();
    stats_.opaqueItems = (uint32_t)opaque_.size();
    stats_.transparentItems = (uint32_t)transparent_.size();

    // the frame
    FrameBlock fb{};
    copyMat(fb.view, cam.view);
    copyMat(fb.projection, cam.projection);
    copyMat(fb.viewProjection, viewProj);
    Vec3 l = normalize(transformVector(cam.view, -env_.lightDirection));
    copyVec(fb.lightDirection, l, 0);
    copyVec(fb.lightColor, env_.lightColor, 1);
    copyVec(fb.ambient, env_.ambient, 1);
    copyVec(fb.fogColor, env_.fogColor, 1);
    fb.fogParams[0] = env_.fogStart;
    fb.fogParams[1] = env_.fogEnd;
    fb.fogParams[2] = env_.fog ? 1.0f : 0.0f;
    fb.options[0] = options_.textures ? 1.0f : 0.0f;
    fb.options[1] = options_.lighting ? 1.0f : 0.0f;
    device_.updateBuffer(frameUniforms_, 0, &fb, sizeof fb);

    // the matrices of the items, in the order of the draws
    size_t count = opaque_.size() + transparent_.size();
    instanceData_.resize(std::max<size_t>(count, 1) * 16);
    size_t k = 0;
    for (auto *list : {&opaque_, &transparent_})
        for (const DrawItem &it : *list)
            std::memcpy(&instanceData_[16 * k++], dataOf(it.world), 64);
    size_t bytes = instanceData_.size() * sizeof(float);
    if (bytes > instanceCapacity_) {
        device_.destroyBuffer(instanceBuffer_);
        instanceCapacity_ = std::max(bytes, instanceCapacity_ * 2);
        instanceBuffer_ = device_.createBuffer(gpu::BufferUsage::Vertex, nullptr, instanceCapacity_, true);
    }
    device_.updateBuffer(instanceBuffer_, 0, instanceData_.data(), bytes);
    auto t3 = clock::now();

    gpu::ClearValues clear;
    clear.r = env_.clearColor.x;
    clear.g = env_.clearColor.y;
    clear.b = env_.clearColor.z;
    clear.a = env_.clearColor.w;
    device_.beginPass(x, y, width, height, clear);
    device_.bindUniformBuffer(kFrameSlot, frameUniforms_, 0, sizeof(FrameBlock));
    const GpuMesh *lastMesh = nullptr;
    const MaterialInstance *lastMaterial = nullptr;
    size_t base = 0;
    for (auto *list : {&opaque_, &transparent_}) {
        const std::vector<DrawItem> &items = *list;
        for (size_t i = 0; i < items.size();) {
            const DrawItem &it = items[i];
            // the run of the same mesh, material and pipeline: one draw
            size_t j = i + 1;
            while (options_.instancing && j < items.size() && items[j].mesh == it.mesh &&
                   items[j].material == it.material && items[j].pipeline == it.pipeline)
                j++;
            device_.bindPipeline(it.pipeline);
            if (it.material != lastMaterial) {
                device_.bindUniformBuffer(kMaterialSlot, it.material->uniforms, 0, sizeof(MaterialBlock));
                device_.bindTexture(0, it.material->texture ? it.material->texture : white_);
                lastMaterial = it.material;
            }
            if (it.mesh != lastMesh) {
                device_.bindVertexBuffer(0, it.mesh->vertices);
                device_.bindIndexBuffer(it.mesh->indices, gpu::IndexType::U32);
                lastMesh = it.mesh;
            }
            device_.bindVertexBuffer(1, instanceBuffer_, (base + i) * kInstanceStride);
            device_.drawIndexed(it.mesh->indexCount, 0, 0, (uint32_t)(j - i));
            i = j;
        }
        base += items.size();
    }
    device_.endPass();
    stats_.gpu = device_.takeStats();
    auto t4 = clock::now();
    stats_.cullMs = ms(t0, t1);
    stats_.sortMs = ms(t1, t2);
    stats_.uploadMs = ms(t2, t3);
    stats_.drawMs = ms(t3, t4);
}

} // namespace r4::render
