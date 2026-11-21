#include "r4/render/renderer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>

namespace r4::render {
namespace {

// the uniform blocks (std140): the slots and layouts the shaders share
enum Slot : uint32_t {
    FRAME_SLOT = 0,
    MATERIAL_SLOT = 2
};
// the matrices of the instances: a vertex stream read once per instance
constexpr uint32_t kInstanceStride = 64;

struct FrameBlock {
    float view[16];
    float projection[16];
    float viewProjection[16];
    float lightDirection[4]; // view space, towards the light
    float lightColor[4];
    float ambient[4];
    float fogColor[4];
    float fogParams[4];      // start, end, enabled
    float options[4];        // textures, lighting, flat shading, ramp by vertex
    float lightViewProj[16]; // world to the shadow map
    float shadowParams[4];   // enabled, normal offset (world), depth bias, strength of the unlit
    float shadowParams2[4];  // a texel of the map (uv)
    float time[4];           // seconds
};

struct MaterialBlock {
    float diffuse[4];
    float ambient[4];
    float specular[4]; // w: shininess
    float emissive[4];
    float flags[4];  // texture, alpha cutoff (0: none), vertex colors, unlit
    float flags2[4]; // sphere map, palette mode (1 ramp, 2 lit by the ramp's intensity), ramp ambient, ramp light
    float flags3[4]; // receives the shadows, drawn over the face of the same depth, index atlas columns and rows
    float textureMean[4];
    float animation[4]; // frames, frames per second, out of the fog
};

const char *kVertexShader = R"(#version 330
layout(std140) uniform Frame {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 ambientLight;
    vec4 fogColor;
    vec4 fogParams;
    vec4 options;
    mat4 lightViewProj;
    vec4 shadowParams;
    vec4 shadowParams2;
    vec4 time;
};
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inModel0;
layout(location = 5) in vec4 inModel1;
layout(location = 6) in vec4 inModel2;
layout(location = 7) in vec4 inModel3;
layout(std140) uniform Material {
    vec4 diffuse;
    vec4 ambient;
    vec4 specular;
    vec4 emissive;
    vec4 flags;
    vec4 flags2;
    vec4 flags3;
    vec4 textureMean;
    vec4 animation;
};
out vec3 vNormal;
out vec3 vPosition;
out vec2 vUV;
out vec4 vColor;
out float vRampIntensity;
out vec4 vShadow;
invariant gl_Position;
void main() {
    mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
    mat4 modelView = view * model;
    vec4 p = modelView * vec4(inPosition, 1.0);
    vShadow = vec4(0.0);
    if (shadowParams.x > 0.5) {
        vec3 w = (model * vec4(inPosition, 1.0)).xyz;
        vec3 n = mat3(model) * inNormal;
        if (dot(n, n) > 1e-12) {
            w += normalize(n) * shadowParams.y;
        }
        vShadow = lightViewProj * vec4(w, 1.0);
    }
    vPosition = p.xyz;
    vNormal = transpose(inverse(mat3(modelView))) * inNormal;
    vUV = inUV;
    vColor = inColor;
    vRampIntensity = 1.0;
    if (dot(vNormal, vNormal) > 1e-12) {
        vRampIntensity = clamp(flags2.z + max(dot(normalize(vNormal), lightDirection.xyz), 0.0) * flags2.w, 0.0, 1.0);
    }
    gl_Position = projection * p;
}
)";

const char *kFragmentShader = R"(#version 330
layout(std140) uniform Frame {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 ambientLight;
    vec4 fogColor;
    vec4 fogParams;
    vec4 options;
    mat4 lightViewProj;
    vec4 shadowParams;
    vec4 shadowParams2;
    vec4 time;
};
layout(std140) uniform Material {
    vec4 diffuse;
    vec4 ambient;
    vec4 specular;
    vec4 emissive;
    vec4 flags;
    vec4 flags2;
    vec4 flags3;
    vec4 textureMean;
    vec4 animation;
};
uniform sampler2D diffuseMap;
uniform sampler2DShadow shadowMap;
uniform sampler2D indexMap;
in vec4 vShadow;
in vec3 vNormal;
in vec3 vPosition;
in vec2 vUV;
in vec4 vColor;
in float vRampIntensity;
out vec4 fragColor;
vec3 faceDirection;
vec3 seenNormal() {
    if (options.z > 0.5 && dot(faceDirection, faceDirection) > 0.0) {
        return normalize(faceDirection);
    }
    vec3 n = normalize(vNormal);
    if (dot(faceDirection, faceDirection) > 0.0 && dot(n, faceDirection) < 0.0) {
        n = -n;
    }
    return n;
}
float shadowFactor() {
    if (shadowParams.x < 0.5 || flags3.x < 0.5 || vShadow.w <= 0.0) {
        return 1.0;
    }
    vec3 p = vShadow.xyz / vShadow.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) {
        return 1.0;
    }
    float s = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            s += textureLod(shadowMap, vec3(p.xy + vec2(x, y) * shadowParams2.x, p.z - shadowParams.z), 0.0);
        }
    }
    s /= 9.0;
    vec2 e = abs(p.xy * 2.0 - 1.0);
    return mix(s, 1.0, smoothstep(0.85, 1.0, max(e.x, e.y)));
}
vec3 rampShade(int index, float intensity) {
    float t = intensity * 31.0;
    int k = int(min(floor(t), 30.0));
    return mix(texelFetch(diffuseMap, ivec2(index, k), 0).rgb, texelFetch(diffuseMap, ivec2(index, k + 1), 0).rgb, t - float(k));
}
vec4 shadedIndexTexel(ivec2 p, ivec2 origin, ivec2 size, float intensity) {
    vec4 t = texelFetch(indexMap, origin + clamp(p, ivec2(0), size - 1), 0);
    return vec4(rampShade(int(t.r * 255.0 + 0.5), intensity) * t.a, t.a);
}
vec4 shadedIndexTexture(vec2 uv, float intensity) {
    ivec2 size = textureSize(indexMap, 0);
    ivec2 origin = ivec2(0);
    if (flags3.z > 0.5) {
        ivec2 grid = ivec2(int(flags3.z + 0.5), int(flags3.w + 0.5));
        int cell = int(vColor.g * 255.0 + 0.5) + 256 * int(vColor.b * 255.0 + 0.5);
        origin = ivec2(cell % grid.x, cell / grid.x) * (size / grid);
        int log2Size = int(vColor.a * 255.0 + 0.5);
        size = ivec2(1 << (log2Size & 15), 1 << (log2Size >> 4));
    }
    vec2 st = uv * vec2(size) - 0.5;
    ivec2 p = ivec2(floor(st));
    vec2 f = st - floor(st);
    vec4 a = mix(shadedIndexTexel(p, origin, size, intensity), shadedIndexTexel(p + ivec2(1, 0), origin, size, intensity), f.x);
    vec4 b = mix(shadedIndexTexel(p + ivec2(0, 1), origin, size, intensity), shadedIndexTexel(p + ivec2(1, 1), origin, size, intensity), f.x);
    vec4 c = mix(a, b, f.y);
    if (c.a <= 0.0) {
        return vec4(0.0);
    }
    return vec4(c.rgb / c.a, c.a);
}
float rampIntensity(float shadow) {
    vec3 n = seenNormal();
    float intensity = clamp(flags2.z + max(dot(n, lightDirection.xyz), 0.0) * flags2.w * shadow, 0.0, 1.0);
    bool back = dot(faceDirection, faceDirection) > 0.0 && dot(vNormal, faceDirection) < 0.0;
    if (options.w > 0.5 && options.z < 0.5 && !back) {
        intensity = flags2.z + (vRampIntensity - flags2.z) * shadow;
    }
    return intensity;
}
vec3 fogged(vec3 color) {
    if (fogParams.z > 0.5 && animation.z < 0.5) {
        float f = clamp((length(vPosition) - fogParams.x) / max(fogParams.y - fogParams.x, 1e-6), 0.0, 1.0);
        color = mix(color, fogColor.rgb, f);
    }
    return color;
}
void main() {
    faceDirection = cross(dFdx(vPosition), dFdy(vPosition));
    float shadow = shadowFactor();
    vec4 base = diffuse;
    vec2 uv = vUV;
    if (flags2.x > 0.5) {
        vec3 u = normalize(vPosition);
        vec3 n = seenNormal();
        vec3 r = reflect(u, n);
        float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
        uv = r.xy / m + 0.5;
    }
    if (animation.x > 1.5) {
        float frame = mod(floor(time.x * animation.y), animation.x);
        float halfTexel = 0.5 / float(textureSize(diffuseMap, 0).y);
        float bottom = (animation.x - 1.0 - frame) / animation.x;
        uv.y = clamp(bottom + clamp(uv.y, 0.0, 1.0) / animation.x, bottom + halfTexel, bottom + 1.0 / animation.x - halfTexel);
    }
    bool lit = options.y > 0.5 && dot(vNormal, vNormal) > 1e-12;
    if (flags2.y > 2.5) {
        if (options.x < 0.5) {
            discard;
        }
        float intensity = 1.0;
        if (flags.z > 0.5) {
            intensity = vColor.r;
        } else if (lit) {
            intensity = rampIntensity(shadow);
        }
        base = shadedIndexTexture(uv, intensity);
        if (flags.z > 0.5 && options.y > 0.5) {
            base.rgb *= mix(1.0, shadow, shadowParams.w);
        }
        if (flags.y > 0.0 && base.a < flags.y) {
            discard;
        }
        fragColor = vec4(fogged(base.rgb), base.a);
        return;
    }
    if (flags2.y > 0.5 && lit) {
        float intensity = rampIntensity(shadow);
        if (flags2.y < 1.5) {
            base = vec4(rampShade(int(vColor.r * 255.0 + 0.5), intensity), diffuse.a);
        } else {
            if (options.x > 0.5) {
                base *= texture(diffuseMap, uv);
            } else if (flags3.y > 0.5) {
                discard;
            } else {
                base *= textureMean;
            }
            if (flags.y > 0.0 && base.a < flags.y) {
                discard;
            }
            base.rgb *= intensity;
        }
        fragColor = vec4(fogged(base.rgb), base.a);
        return;
    }
    if (flags.x > 0.5 && options.x > 0.5) {
        base *= texture(diffuseMap, uv);
    } else if (flags.x > 0.5) {
        base *= textureMean;
    }
    if (flags.z > 0.5) {
        base *= vColor;
    }
    if (flags.y > 0.0 && base.a < flags.y) {
        discard;
    }
    vec3 color;
    if (flags.w > 0.5 || options.y < 0.5 || dot(vNormal, vNormal) < 1e-12) {
        color = base.rgb;
        if (flags.w > 0.5 && options.y > 0.5) {
            color *= mix(1.0, shadow, shadowParams.w);
        }
    } else {
        vec3 n = seenNormal();
        vec3 l = lightDirection.xyz;
        vec3 v = normalize(-vPosition);
        float nl = max(dot(n, l), 0.0) * shadow;
        float spec = 0.0;
        if (nl > 0.0 && specular.w > 0.0) {
            spec = pow(max(dot(n, normalize(l + v)), 0.0), specular.w);
        }
        color = emissive.rgb + base.rgb * (ambient.rgb * ambientLight.rgb + lightColor.rgb * nl) + specular.rgb * lightColor.rgb * spec;
    }
    fragColor = vec4(fogged(color), base.a);
}
)";

// the depth from the light
const char *kShadowVertex = R"(#version 330
layout(std140) uniform Frame {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 ambientLight;
    vec4 fogColor;
    vec4 fogParams;
    vec4 options;
    mat4 lightViewProj;
    vec4 shadowParams;
    vec4 shadowParams2;
    vec4 time;
};
layout(location = 0) in vec3 inPosition;
layout(location = 4) in vec4 inModel0;
layout(location = 5) in vec4 inModel1;
layout(location = 6) in vec4 inModel2;
layout(location = 7) in vec4 inModel3;
void main() {
    gl_Position = lightViewProj * mat4(inModel0, inModel1, inModel2, inModel3) * vec4(inPosition, 1.0);
}
)";
const char *kShadowFragment = R"(#version 330
void main() {}
)";

void copyMat(float *dst, const Mat4 &m) {
    std::memcpy(dst, dataOf(m), 64);
}
Vec4 meanColor(const assets::Image &image) {
    double sum[3] = {0, 0, 0};
    size_t count = 0;
    const int n = image.channels;
    for (size_t i = 0; i + n <= image.pixels.size(); i += n) {
        const uint8_t *p = &image.pixels[i];
        bool opaque = n == 2 ? p[1] >= 128 : (n == 4 ? p[3] >= 128 : true);
        if (!opaque) {
            continue;
        }
        for (int c = 0; c < 3; c++) {
            sum[c] += n >= 3 ? p[c] : p[0];
        }
        count++;
    }
    if (count == 0) {
        return {1, 1, 1, 1};
    }
    return {(float)(sum[0] / count / 255), (float)(sum[1] / count / 255), (float)(sum[2] / count / 255), 1};
}

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
        {0, 3, gpu::AttribType::FLOAT, false, (uint32_t)offsetof(assets::Vertex, position)},
        {1, 3, gpu::AttribType::FLOAT, false, (uint32_t)offsetof(assets::Vertex, normal)},
        {2, 2, gpu::AttribType::FLOAT, false, (uint32_t)offsetof(assets::Vertex, uv)},
        {3, 4, gpu::AttribType::UBYTE, true, (uint32_t)offsetof(assets::Vertex, color)},
    };
    gpu::VertexStream inst;
    inst.stride = kInstanceStride;
    inst.perInstance = true;
    for (uint32_t c = 0; c < 4; c++) {
        inst.attribs.push_back({4 + c, 4, gpu::AttribType::FLOAT, false, 16 * c});
    }
    gpu::VertexLayout l;
    l.streams.push_back(s);
    l.streams.push_back(inst);
    return l;
}

} // namespace

Renderer::Renderer(gpu::Device &device) : device_(device) {
    createShaders();
    frameUniforms_ = device_.createBuffer(gpu::BufferUsage::UNIFORM, nullptr, sizeof(FrameBlock), true);
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
    for (auto &[key, p] : pipelines_) {
        device_.destroyPipeline(p);
    }
    for (gpu::TextureHandle t : ownedTextures_) {
        device_.destroyTexture(t);
    }
    device_.destroyBuffer(frameUniforms_);
    device_.destroyBuffer(instanceBuffer_);
    device_.destroyShader(shader_);
    device_.destroyShader(opaqueShader_);
    device_.destroyPipeline(shadowPipeline_);
    device_.destroyShader(shadowShader_);
    device_.destroyRenderTarget(shadowMap_);
}

bool Renderer::createShaders() {
    gpu::ShaderDesc d;
    d.vertex = kVertexShader;
    d.fragment = kFragmentShader;
    d.uniformBlocks = {{"Frame", FRAME_SLOT}, {"Material", MATERIAL_SLOT}};
    d.samplers = {{"diffuseMap", 0}, {"shadowMap", 1}, {"indexMap", 2}};
    d.debugName = "phong";
    shader_ = device_.createShader(d);
    if (!shader_) {
        error_ = device_.lastError();
        return false;
    }
    std::string opaque = kFragmentShader;
    for (size_t at = opaque.find("discard;"); at != std::string::npos; at = opaque.find("discard;", at)) {
        opaque.replace(at, 8, ";");
    }
    d.fragment = opaque;
    d.debugName = "phong without discard";
    opaqueShader_ = device_.createShader(d);
    gpu::ShaderDesc sd;
    sd.vertex = kShadowVertex;
    sd.fragment = kShadowFragment;
    sd.uniformBlocks = {{"Frame", FRAME_SLOT}};
    sd.debugName = "shadow";
    shadowShader_ = device_.createShader(sd);
    if (shadowShader_) {
        gpu::PipelineDesc pd;
        pd.shader = shadowShader_;
        pd.layout = vertexLayout();
        pd.cull = gpu::CullMode::NONE;
        pd.polygonOffsetFactor = 2;
        pd.polygonOffsetUnits = 4;
        shadowPipeline_ = device_.createPipeline(pd);
    }
    if (!shadowPipeline_) {
        error_ = device_.lastError();
    }
    return true;
}

gpu::PipelineHandle Renderer::pipelineFor(const MaterialInstance &mat, assets::Primitive primitive) {
    const assets::Material &m = mat.params;
    // the discards of the shader: the alpha test, and the layers on a face that are not drawn without textures
    const bool discards = m.alpha == assets::AlphaMode::MASK || m.alphaTest > 0 || m.depthEqual || mat.indexTexture || !opaqueShader_;
    bool blend = m.alpha == assets::AlphaMode::BLEND;
    bool additive = m.alpha == assets::AlphaMode::ADDITIVE;
    uint32_t key = (blend ? 1u : 0u) | (additive ? 32u : 0u) | (m.depthEqual ? 64u : 0u) | (m.blendDepthWrite ? 128u : 0u) | (m.doubleSided ? 2u : 0u) | (wireframe_ ? 4u : 0u) | (m.depthWrite ? 0u : 8u) | (m.decal ? 16u : 0u) | (uint32_t)primitive << 8 | (m.depthTest ? 0u : 1u << 16) | (discards ? 1u << 17 : 0u);
    auto it = pipelines_.find(key);
    if (it != pipelines_.end()) {
        return it->second;
    }
    gpu::PipelineDesc d;
    d.shader = discards ? shader_ : opaqueShader_;
    d.layout = vertexLayout();
    d.primitive = primitive == assets::Primitive::LINES ? gpu::Primitive::LINES : primitive == assets::Primitive::POINTS ? gpu::Primitive::POINTS : gpu::Primitive::TRIANGLES;
    bool tris = primitive == assets::Primitive::TRIANGLES;
    d.cull = m.doubleSided || wireframe_ || !tris ? gpu::CullMode::NONE : gpu::CullMode::BACK;
    d.blend = blend ? gpu::BlendMode::ALPHA : additive ? gpu::BlendMode::ADDITIVE : gpu::BlendMode::NONE;
    d.depthWrite = (!blend || m.blendDepthWrite) && m.depthWrite;
    if (m.depthEqual) {
        d.depthCompare = gpu::CompareOp::EQUAL;
    }
    if (!m.depthTest) {
        d.depthCompare = gpu::CompareOp::ALWAYS;
    }
    d.wireframe = wireframe_ && tris;
    if (m.decal && !m.depthEqual) {
        d.polygonOffsetFactor = -1;
        d.polygonOffsetUnits = -2;
    }
    gpu::PipelineHandle p = device_.createPipeline(d);
    pipelines_[key] = p;
    return p;
}

gpu::TextureHandle Renderer::uploadImage(const assets::Image &image, bool mipmaps) {
    if (!image.valid()) {
        return {};
    }
    gpu::TextureDesc d;
    d.width = image.width;
    d.height = image.height;
    d.mipmaps = mipmaps;
    d.wrapS = image.clampS ? gpu::Wrap::CLAMP : gpu::Wrap::REPEAT;
    d.wrapT = image.clampT ? gpu::Wrap::CLAMP : gpu::Wrap::REPEAT;
    d.anisotropy = options_.anisotropy;
    std::vector<uint8_t> rgba;
    const void *pixels = image.pixels.data();
    switch (image.channels) {
    case 3:
        d.format = gpu::TextureFormat::RGB8;
        break;
    case 4:
        d.format = gpu::TextureFormat::RGBA8;
        break;
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
    if (t) {
        ownedTextures_.push_back(t);
        textureMeans_[t.id] = meanColor(image);
    }
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
    m->vertices = device_.createBuffer(gpu::BufferUsage::VERTEX, mesh.vertices.data(), mesh.vertices.size() * sizeof(assets::Vertex), false);
    m->indices = device_.createBuffer(gpu::BufferUsage::INDEX, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t), false);
    m->indexCount = (uint32_t)mesh.indices.size();
    m->primitive = mesh.primitive;
    Box b = mesh.bounds;
    if (b.empty()) {
        for (const assets::Vertex &v : mesh.vertices) {
            b.extend(v.position);
        }
    }
    m->bounds = sphereOf(b);
    m->box = b;
    return m;
}

std::shared_ptr<MaterialInstance> Renderer::createMaterial(const assets::Material &p, gpu::TextureHandle texture, gpu::TextureHandle indexTexture) {
    gpu::Device *dev = &device_;
    auto m = std::shared_ptr<MaterialInstance>(new MaterialInstance, [dev](MaterialInstance *mi) {
        dev->destroyBuffer(mi->uniforms);
        delete mi;
    });
    m->params = p;
    m->texture = texture;
    m->indexTexture = indexTexture;
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
    b.flags[1] = p.alpha == assets::AlphaMode::MASK ? p.alphaCutoff : p.alphaTest;
    b.flags[2] = p.vertexColors ? 1.0f : 0.0f;
    b.flags[3] = p.shading == assets::Shading::UNLIT ? 1.0f : 0.0f;
    b.flags2[0] = p.sphereMap ? 1.0f : 0.0f;
    if (p.shading == assets::Shading::PALETTE_RAMP && texture) {
        b.flags2[1] = indexTexture ? 3.0f : (p.rampTextured ? 2.0f : 1.0f);
        b.flags2[2] = p.rampAmbient;
        b.flags2[3] = p.rampLight;
    }
    b.flags3[0] = p.receiveShadows ? 1.0f : 0.0f;
    b.flags3[1] = p.depthEqual ? 1.0f : 0.0f;
    b.flags3[2] = (float)p.indexAtlasColumns;
    b.flags3[3] = (float)p.indexAtlasRows;
    Vec4 mean{1, 1, 1, 1};
    auto it = texture ? textureMeans_.find(texture.id) : textureMeans_.end();
    if (it != textureMeans_.end()) {
        mean = it->second;
    }
    copyVec(b.textureMean, Vec3{mean.x, mean.y, mean.z}, mean.w);
    b.animation[0] = (float)std::max(1, p.frames);
    b.animation[1] = p.framesPerSecond;
    b.animation[2] = p.fog ? 0.0f : 1.0f;
    m->uniforms = device_.createBuffer(gpu::BufferUsage::UNIFORM, &b, sizeof b, false);
    return m;
}

scene::NodePtr Renderer::upload(const assets::Model &model) {
    std::vector<gpu::TextureHandle> textures(model.images.size());
    for (size_t i = 0; i < model.images.size(); i++) {
        textures[i] = uploadImage(model.images[i], model.images[i].mipmaps);
    }
    std::vector<std::shared_ptr<MaterialInstance>> materials;
    for (const assets::Material &m : model.materials) {
        gpu::TextureHandle texture = m.diffuseImage >= 0 && m.diffuseImage < (int)textures.size() ? textures[m.diffuseImage] : gpu::TextureHandle{};
        gpu::TextureHandle indexTexture = m.indexImage >= 0 && m.indexImage < (int)textures.size() ? textures[m.indexImage] : gpu::TextureHandle{};
        materials.push_back(createMaterial(m, texture, indexTexture));
    }
    std::vector<std::shared_ptr<GpuMesh>> meshes;
    for (const assets::Mesh &m : model.meshes) {
        meshes.push_back(uploadMesh(m));
    }

    std::vector<scene::NodePtr> nodes(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); i++) {
        const assets::Node &n = model.nodes[i];
        scene::NodePtr node;
        if (n.kind == assets::NodeKind::SWITCH) {
            auto sw = scene::make<scene::Switch>();
            sw->setValue(n.switchValue == -1 ? scene::Switch::kAll : n.switchValue < -1 ? scene::Switch::kNone : n.switchValue);
            node = sw;
        } else if (n.kind == assets::NodeKind::LOD) {
            auto lod = scene::make<scene::LOD>();
            lod->setRanges(n.lodRanges);
            if (n.hasLodCenter) {
                lod->setCenter(n.lodCenter);
            }
            node = lod;
        } else if (n.kind == assets::NodeKind::BILLBOARD || !n.meshes.empty()) {
            std::shared_ptr<scene::Geode> g;
            if (n.kind == assets::NodeKind::BILLBOARD) {
                auto b = scene::make<scene::Billboard>();
                b->setMode(n.billboardPoint ? scene::Billboard::Mode::Point : scene::Billboard::Mode::Axis);
                g = b;
            } else {
                g = scene::make<scene::Geode>();
            }
            for (int mi : n.meshes) {
                const assets::Mesh &mesh = model.meshes[mi];
                g->addDrawable({meshes[mi], mesh.material >= 0 && mesh.material < (int)materials.size() ? materials[mesh.material] : defaultMaterial_});
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
    for (size_t i = 0; i < model.nodes.size(); i++) {
        for (int c : model.nodes[i].children) {
            if (c > 0 && c < (int)nodes.size()) {
                nodes[i]->addChild(nodes[c]);
            }
        }
    }
    if (nodes.empty()) {
        return scene::make<scene::Group>();
    }
    nodes[0]->setStateFilter(model.defaultState);
    return nodes[0];
}

void Renderer::addBulk(const scene::Drawable &d, uint32_t first, uint32_t count, Vec3 depthAt, Vec3 eye) {
    if (!d.mesh || !d.mesh->indexCount) {
        return;
    }
    MaterialInstance *mat = d.material ? d.material.get() : defaultMaterial_.get();
    DrawItem item;
    item.mesh = d.mesh.get();
    item.material = mat;
    item.world = Mat4(1.0f);
    item.depth = length(depthAt - eye);
    item.order = mat->params.drawOrder;
    item.pipeline = pipelineFor(*mat, d.mesh->primitive);
    item.key = (uint64_t)item.pipeline.id << 48 | (uint64_t)(mat->id & 0xffffff) << 24 | (uint64_t)((uintptr_t)item.mesh >> 4 & 0xffffff);
    item.first = first;
    item.count = count;
    item.bulk = true;
    bool behind = (mat->params.alpha == assets::AlphaMode::BLEND || mat->params.alpha == assets::AlphaMode::ADDITIVE) && mat->params.drawOrder >= 0;
    (behind ? transparent_ : opaque_).push_back(item);
    stats_.drawables++;
}

void Renderer::addItem(const scene::Drawable &d, const Mat4 &world, Vec3 eye) {
    if (!d.mesh || !d.mesh->indexCount) {
        return;
    }
    MaterialInstance *mat = d.material ? d.material.get() : defaultMaterial_.get();
    DrawItem item;
    item.mesh = d.mesh.get();
    item.material = mat;
    item.world = world;
    Vec3 c = transformPoint(world, d.mesh->bounds.center);
    item.depth = length(c - eye);
    item.order = mat->params.drawOrder;
    item.pipeline = pipelineFor(*mat, d.mesh->primitive);
    item.key = (uint64_t)item.pipeline.id << 48 | (uint64_t)(mat->id & 0xffffff) << 24 | (uint64_t)((uintptr_t)item.mesh >> 4 & 0xffffff);
    bool behind = (mat->params.alpha == assets::AlphaMode::BLEND || mat->params.alpha == assets::AlphaMode::ADDITIVE) && mat->params.drawOrder >= 0;
    (behind ? transparent_ : opaque_).push_back(item);
    stats_.drawables++;
}

void Renderer::cullInstancer(scene::Instancer &node, const Mat4 &world, const Frustum &frustum, bool inside, Vec3 eye) {
    const auto &levels = node.levels();
    if (levels.empty()) {
        return;
    }
    Sphere one;
    for (const auto &l : levels) {
        one.extend(l.bounds);
    }
    const float worldScale = maxScale(world);
    instanceScratch_.clear();
    Vec3 kept(0.0f);
    auto keep = [&](int level, const Mat4 &w) {
        instanceScratch_.push_back({level, w});
        kept += Vec3(w[3]);
    };
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
        keep(level, w);
    }
    // the placements by cell: a cell out of reach or of the view is skipped whole; in the view, its placements are not tested;
    // in one level of detail, the level of its placements is not chosen
    const float reach = levels.back().maxDistance / std::max(options_.lodScale, 1e-6f);
    const float extent = length(one.center) + one.radius;
    const std::vector<scene::Placement> &placements = node.placements();
    for (const scene::Instancer::Cell &cell : node.cells()) {
        const uint32_t n = cell.end - cell.begin;
        stats_.instancesVisited += n;
        const Vec3 boxCenter = (cell.box.min + cell.box.max) * 0.5f;
        const float boxRadius = length(cell.box.max - boxCenter);
        const Sphere s{transformPoint(world, boxCenter), (boxRadius + extent * cell.maxScale) * worldScale};
        const float d = length(s.center - eye);
        if (d - s.radius > reach) {
            stats_.instancesCulled += n;
            continue;
        }
        Frustum::Result test = inside ? Frustum::Inside : frustum.test(s);
        if (test == Frustum::Outside) {
            stats_.instancesCulled += n;
            continue;
        }
        const int nearLevel = node.select(std::max(0.0f, d - s.radius) * options_.lodScale);
        const int farLevel = node.select((d + s.radius) * options_.lodScale);
        if (nearLevel < 0) {
            stats_.instancesCulled += n;
            continue;
        }
        for (uint32_t i = cell.begin; i < cell.end; i++) {
            const scene::Placement &p = placements[i];
            // before its matrix: its sphere from its position and its scale
            const Vec3 at = transformPoint(world, p.position);
            if (test != Frustum::Inside && frustum.test({at, extent * std::max(p.scale, p.scaleUp) * worldScale}) == Frustum::Outside) {
                stats_.instancesCulled++;
                continue;
            }
            const int level = nearLevel == farLevel ? nearLevel : node.select(length(at - eye) * options_.lodScale);
            if (level < 0) {
                stats_.instancesCulled++;
                continue;
            }
            keep(level, world * node.matrixOf(p));
        }
    }
    if (instanceScratch_.empty()) {
        return;
    }
    // one item for each part of each level, with the matrices of its instances; their mean position for the sort
    const Vec3 depthAt = kept / (float)instanceScratch_.size();
    for (int level = 0; level < (int)levels.size(); level++) {
        for (const scene::InstancePart &part : levels[level].parts) {
            const uint32_t first = (uint32_t)bulk_.size();
            for (const auto &[l, w] : instanceScratch_) {
                if (l == level) {
                    bulk_.push_back(w * part.local);
                }
            }
            if (bulk_.size() > first) {
                addBulk(part.drawable, first, (uint32_t)bulk_.size() - first, depthAt, eye);
            }
        }
    }
}

void Renderer::cull(scene::Node &node, const Mat4 &parent, const Frustum &frustum, bool inside, uint32_t state, Vec3 eye) {
    if (!(node.mask() & state)) {
        return;
    }
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
    if (node.type() == scene::NodeType::BILLBOARD) {
        auto &bb = static_cast<scene::Billboard &>(node);
        world = world * bb.rotationFor(transformPoint(inverse(world), eye));
    }
    if (node.type() == scene::NodeType::GEODE || node.type() == scene::NodeType::BILLBOARD) {
        for (const scene::Drawable &d : static_cast<scene::Geode &>(node).drawables()) {
            addItem(d, world, eye);
        }
    } else if (node.type() == scene::NodeType::INSTANCER) {
        cullInstancer(static_cast<scene::Instancer &>(node), world, frustum, inside, eye);
    }
    const auto &children = node.children();
    switch (node.type()) {
    case scene::NodeType::SWITCH: {
        auto &sw = static_cast<scene::Switch &>(node);
        for (size_t i = 0; i < children.size(); i++) {
            if (sw.isActive((int)i)) {
                cull(*children[i], world, frustum, inside, state, eye);
            }
        }
        break;
    }
    case scene::NodeType::LOD: {
        auto &lod = static_cast<scene::LOD &>(node);
        Vec3 c = transformPoint(world, lod.center());
        int i = lod.select(length(c - eye) * options_.lodScale);
        if (i >= 0) {
            cull(*children[i], world, frustum, inside, state, eye);
        }
        break;
    }
    default:
        for (const scene::NodePtr &c : children) {
            cull(*c, world, frustum, inside, state, eye);
        }
    }
}

void Renderer::render(scene::Node &root, const Camera &cam, int x, int y, int width, int height) {
    stats_ = {};
    opaque_.clear();
    bulk_.clear();
    transparent_.clear();
    if (!shader_) {
        return;
    }

    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    auto t0 = clock::now();
    Mat4 viewProj = cam.projection * cam.view;
    if (!options_.freezeCulling || !frozen_) {
        cullViewProj_ = viewProj;
        cullEye_ = cam.position;
    }
    frozen_ = options_.freezeCulling;
    cull(root, Mat4(1.0f), Frustum::fromMatrix(cullViewProj_), !options_.frustumCulling, cam.mask, cullEye_);
    auto t1 = clock::now();
    if (options_.sortByState) {
        std::sort(opaque_.begin(), opaque_.end(), [](const DrawItem &a, const DrawItem &b) {
            return a.order != b.order ? a.order < b.order : a.key < b.key;
        });
    } else {
        std::stable_sort(opaque_.begin(), opaque_.end(), [](const DrawItem &a, const DrawItem &b) {
            return a.order < b.order;
        });
    }
    for (DrawItem &it : transparent_) {
        // an item of instances keeps the depth of their mean position
        if (!it.bulk) {
            it.depth = length(transformPoint(it.world, it.mesh->bounds.center) - cam.position);
        }
    }
    std::sort(transparent_.begin(), transparent_.end(), [](const DrawItem &a, const DrawItem &b) {
        return a.order != b.order ? a.order < b.order : a.depth > b.depth;
    });
    auto t2 = clock::now();
    stats_.opaqueItems = (uint32_t)opaque_.size();
    stats_.transparentItems = (uint32_t)transparent_.size();

    Mat4 lightViewProj(1.0f);
    float texel = 0;
    casters_.clear();
    bool shadows = options_.shadows && options_.lighting && shadowPipeline_ && shadowView(cam, lightViewProj, texel);
    if (shadows) {
        size_t o = opaque_.size();
        size_t t = transparent_.size();
        RenderStats keep = stats_;
        cull(root, Mat4(1.0f), Frustum::fromMatrix(lightViewProj), false, cam.mask & options_.shadowMask, cullEye_);
        stats_ = keep;
        auto casts = [](const DrawItem &it) {
            const assets::Material &m = it.material->params;
            bool solid = m.alpha == assets::AlphaMode::NONE || m.alpha == assets::AlphaMode::MASK || (m.alpha == assets::AlphaMode::BLEND && m.blendDepthWrite);
            return m.castShadows && solid && m.depthWrite && !m.depthEqual && it.mesh->primitive == assets::Primitive::TRIANGLES;
        };
        for (size_t i = o; i < opaque_.size(); i++) {
            if (casts(opaque_[i])) {
                casters_.push_back(opaque_[i]);
            }
        }
        for (size_t i = t; i < transparent_.size(); i++) {
            if (casts(transparent_[i])) {
                casters_.push_back(transparent_[i]);
            }
        }
        opaque_.resize(o);
        transparent_.resize(t);
        std::sort(casters_.begin(), casters_.end(), [](const DrawItem &a, const DrawItem &b) {
            return a.mesh < b.mesh;
        });
        stats_.shadowCasters = (uint32_t)casters_.size();
        int size = std::max(64, options_.shadowMapSize);
        if (!shadowMap_ || shadowMapSize_ != size) {
            device_.destroyRenderTarget(shadowMap_);
            gpu::RenderTargetDesc rd;
            rd.width = rd.height = size;
            rd.shadow = true;
            shadowMap_ = device_.createRenderTarget(rd);
            shadowMapSize_ = size;
        }
        shadows = (bool)shadowMap_;
    }
    auto t2s = clock::now();

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
    fb.time[0] = env_.time;
    fb.options[0] = options_.textures && !wireframe_ ? 1.0f : 0.0f;
    fb.options[1] = options_.lighting ? 1.0f : 0.0f;
    fb.options[2] = options_.flatShading ? 1.0f : 0.0f;
    fb.options[3] = options_.rampGouraud ? 1.0f : 0.0f;
    copyMat(fb.lightViewProj, lightViewProj);
    fb.shadowParams[0] = shadows ? 1.0f : 0.0f;
    fb.shadowParams[1] = options_.shadowNormalOffset * texel;
    fb.shadowParams[2] = options_.shadowBias;
    fb.shadowParams[3] = options_.shadowStrength;
    fb.shadowParams2[0] = 1.0f / (float)std::max(1, shadowMapSize_);
    device_.updateBuffer(frameUniforms_, 0, &fb, sizeof fb);

    size_t count = 0;
    for (auto *list : {&opaque_, &transparent_, &casters_}) {
        for (const DrawItem &it : *list) {
            count += it.count;
        }
    }
    instanceData_.resize(std::max<size_t>(count, 1) * 16);
    size_t k = 0;
    for (auto *list : {&opaque_, &transparent_, &casters_}) {
        for (DrawItem &it : *list) {
            it.offset = (uint32_t)k;
            if (it.bulk) {
                std::memcpy(&instanceData_[16 * k], dataOf(bulk_[it.first]), 64 * (size_t)it.count);
            } else {
                std::memcpy(&instanceData_[16 * k], dataOf(it.world), 64);
            }
            k += it.count;
        }
    }
    size_t bytes = instanceData_.size() * sizeof(float);
    if (bytes > instanceCapacity_) {
        device_.destroyBuffer(instanceBuffer_);
        instanceCapacity_ = std::max(bytes, instanceCapacity_ * 2);
        instanceBuffer_ = device_.createBuffer(gpu::BufferUsage::VERTEX, nullptr, instanceCapacity_, true);
    }
    device_.updateBuffer(instanceBuffer_, 0, instanceData_.data(), bytes);
    auto t3 = clock::now();

    if (shadows) {
        drawShadows();
    }
    gpu::ClearValues clear;
    clear.r = env_.clearColor.x;
    clear.g = env_.clearColor.y;
    clear.b = env_.clearColor.z;
    clear.a = env_.clearColor.w;
    clear.color = options_.clear;
    clear.depth = options_.clear;
    device_.beginPass(target_, x, y, width, height, clear);
    device_.bindUniformBuffer(FRAME_SLOT, frameUniforms_, 0, sizeof(FrameBlock));
    if (shadowMap_) {
        device_.bindTexture(1, device_.renderTargetTexture(shadowMap_));
    }
    const GpuMesh *lastMesh = nullptr;
    const MaterialInstance *lastMaterial = nullptr;
    for (auto *list : {&opaque_, &transparent_}) {
        const std::vector<DrawItem> &items = *list;
        for (size_t i = 0; i < items.size();) {
            const DrawItem &it = items[i];
            size_t j = i + 1;
            uint32_t instances = it.count;
            while (options_.instancing && j < items.size() && items[j].mesh == it.mesh && items[j].material == it.material && items[j].pipeline == it.pipeline) {
                instances += items[j].count;
                j++;
            }
            device_.bindPipeline(it.pipeline);
            if (it.material != lastMaterial) {
                device_.bindUniformBuffer(MATERIAL_SLOT, it.material->uniforms, 0, sizeof(MaterialBlock));
                device_.bindTexture(0, it.material->texture ? it.material->texture : white_);
                if (it.material->indexTexture) {
                    device_.bindTexture(2, it.material->indexTexture);
                }
                lastMaterial = it.material;
            }
            if (it.mesh != lastMesh) {
                device_.bindVertexBuffer(0, it.mesh->vertices);
                device_.bindIndexBuffer(it.mesh->indices, gpu::IndexType::U32);
                lastMesh = it.mesh;
            }
            device_.bindVertexBuffer(1, instanceBuffer_, (size_t)it.offset * kInstanceStride);
            device_.drawIndexed(it.mesh->indexCount, 0, 0, instances);
            i = j;
        }
    }
    device_.endPass();
    stats_.gpu = device_.takeStats();
    auto t4 = clock::now();
    stats_.cullMs = ms(t0, t1);
    stats_.sortMs = ms(t1, t2);
    stats_.shadowMs = ms(t2, t2s);
    stats_.uploadMs = ms(t2, t3);
    stats_.drawMs = ms(t3, t4);
}

// a sphere about the view up to shadowDistance, its center snapped to the texels so that the shadows do not shimmer
bool Renderer::shadowView(const Camera &cam, Mat4 &lightViewProj, float &texel) {
    if (length(env_.lightDirection) < 1e-6f) {
        return false;
    }
    Vec3 dir = normalize(env_.lightDirection);
    float d = std::min(options_.shadowDistance, cam.zfar);
    if (d <= cam.znear) {
        return false;
    }
    Vec4 c = cam.projection * Vec4(0, 0, -d, 1);
    float zFar = c.z / c.w;
    Mat4 inv = inverse(cam.projection * cam.view);
    Vec3 corners[8];
    Vec3 center(0.0f);
    int k = 0;
    for (float x : {-1.0f, 1.0f}) {
        for (float y : {-1.0f, 1.0f}) {
            for (float z : {-1.0f, zFar}) {
                Vec4 p = inv * Vec4(x, y, z, 1);
                corners[k] = Vec3(p) / p.w;
                center += corners[k++];
            }
        }
    }
    center /= 8.0f;
    float r = 0;
    for (Vec3 p : corners) {
        r = std::max(r, length(p - center));
    }
    r = std::ceil(r);
    int size = std::max(64, options_.shadowMapSize);
    texel = 2 * r / (float)size;
    Vec3 up = std::abs(dot(dir, axes::kUp)) > 0.99f ? axes::kForward : axes::kUp;
    Mat4 view = glm::lookAt(Vec3(0.0f), dir, up);
    Vec3 lc = transformPoint(view, center);
    lc.x = std::floor(lc.x / texel) * texel;
    lc.y = std::floor(lc.y / texel) * texel;
    float reach = std::max(0.0f, options_.shadowCasterReach);
    Mat4 proj = glm::ortho(lc.x - r, lc.x + r, lc.y - r, lc.y + r, -lc.z - r - reach, -lc.z + r);
    lightViewProj = proj * view;
    return true;
}

void Renderer::drawShadows() {
    gpu::ClearValues clear;
    clear.color = false;
    device_.beginPass(shadowMap_, 0, 0, shadowMapSize_, shadowMapSize_, clear);
    device_.bindUniformBuffer(FRAME_SLOT, frameUniforms_, 0, sizeof(FrameBlock));
    device_.bindPipeline(shadowPipeline_);
    const GpuMesh *lastMesh = nullptr;
    for (size_t i = 0; i < casters_.size();) {
        const DrawItem &it = casters_[i];
        size_t j = i + 1;
        uint32_t instances = it.count;
        while (options_.instancing && j < casters_.size() && casters_[j].mesh == it.mesh) {
            instances += casters_[j].count;
            j++;
        }
        if (it.mesh != lastMesh) {
            device_.bindVertexBuffer(0, it.mesh->vertices);
            device_.bindIndexBuffer(it.mesh->indices, gpu::IndexType::U32);
            lastMesh = it.mesh;
        }
        device_.bindVertexBuffer(1, instanceBuffer_, (size_t)it.offset * kInstanceStride);
        device_.drawIndexed(it.mesh->indexCount, 0, 0, instances);
        i = j;
    }
    device_.endPass();
}

} // namespace r4::render
