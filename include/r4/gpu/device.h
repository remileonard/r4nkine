// r4/gpu/device.h - the interface of the backends.
//
// Nothing above r4::gpu calls a graphics API: the renderer creates its
// resources through a Device and records draws through it. The model is that
// of the explicit APIs (Vulkan, Metal, D3D12): resources are opaque handles,
// the states are baked into pipelines made in advance, and the uniforms live
// in uniform buffers bound by slot. OpenGL 3.3 emulates it easily.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r4::gpu {

// handles: 0 is none
struct BufferHandle {
    uint32_t id = 0;
    explicit operator bool() const {
        return id != 0;
    }
};
struct TextureHandle {
    uint32_t id = 0;
    explicit operator bool() const {
        return id != 0;
    }
};
struct ShaderHandle {
    uint32_t id = 0;
    explicit operator bool() const {
        return id != 0;
    }
};
// an image to draw into, instead of the window: the scene before the post
// process, a screen of a virtual cockpit
struct RenderTargetHandle {
    uint32_t id = 0;
    explicit operator bool() const {
        return id != 0;
    }
};
struct PipelineHandle {
    uint32_t id = 0;
    explicit operator bool() const {
        return id != 0;
    }
    bool operator==(PipelineHandle o) const {
        return id == o.id;
    }
    bool operator!=(PipelineHandle o) const {
        return id != o.id;
    }
};

enum class BufferUsage {
    VERTEX,
    INDEX,
    UNIFORM
};
enum class IndexType {
    U16,
    U32
};

enum class TextureFormat {
    R8,
    RG8,
    RGB8,
    RGBA8
};
enum class Filter {
    NEAREST,
    LINEAR
};
enum class Wrap {
    REPEAT,
    CLAMP,
    MIRROR
};

struct TextureDesc {
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    Filter minFilter = Filter::LINEAR;
    Filter magFilter = Filter::LINEAR;
    bool mipmaps = true;
    Wrap wrapS = Wrap::REPEAT;
    Wrap wrapT = Wrap::REPEAT;
    // anisotropic filtering: up to this many samples along the slope (1: off),
    // limited by Limits::maxAnisotropy
    float anisotropy = 1;
};

enum class AttribType {
    FLOAT,
    UBYTE,
    SHORT
};

struct VertexAttrib {
    uint32_t location = 0;   // the shader input
    uint32_t components = 3; // 1 to 4
    AttribType type = AttribType::FLOAT;
    bool normalized = false;
    uint32_t offset = 0;
};

// the attributes read from one vertex buffer slot
struct VertexStream {
    uint32_t stride = 0;
    bool perInstance = false; // one element per instance, not per vertex
    std::vector<VertexAttrib> attribs;
};

struct VertexLayout {
    std::vector<VertexStream> streams; // indexed by buffer slot
};

enum class Primitive {
    TRIANGLES,
    LINES,
    POINTS,
    TRIANGLE_STRIP
};
enum class CullMode {
    NONE,
    BACK,
    FRONT
};
enum class BlendMode {
    NONE,
    ALPHA,
    ADDITIVE,
    PREMULTIPLIED
};
enum class CompareOp {
    NEVER,
    LESS,
    LESS_EQUAL,
    EQUAL,
    GREATER,
    ALWAYS
};

// the sources of a shader program. The GL backend takes GLSL 330; the other
// backends will take the same sources cross-compiled (SPIR-V, MSL...).
// Uniform blocks are bound by name: their slot is listed here, so that the
// sources need no binding qualifier (absent from GLSL 330). Samplers too.
struct ShaderDesc {
    std::string vertex;
    std::string fragment;
    std::vector<std::pair<std::string, uint32_t>> uniformBlocks; // name, slot
    std::vector<std::pair<std::string, uint32_t>> samplers;      // name, texture slot
    std::string debugName;
};

struct PipelineDesc {
    ShaderHandle shader;
    VertexLayout layout;
    Primitive primitive = Primitive::TRIANGLES;
    CullMode cull = CullMode::BACK;
    bool frontCCW = true;
    BlendMode blend = BlendMode::NONE;
    bool depthTest = true;
    bool depthWrite = true;
    CompareOp depthCompare = CompareOp::LESS_EQUAL;
    bool wireframe = false;
    float polygonOffsetFactor = 0; // decals, outlines
    float polygonOffsetUnits = 0;
};

struct RenderTargetDesc {
    int width = 0;
    int height = 0;
    int samples = 1;                // > 1: multisampled (MSAA), resolved into the texture
    bool depth = true;              // a depth (and stencil) buffer
    Filter filter = Filter::LINEAR; // how its texture is sampled
    // a shadow map: depth only (no color, no samples), its texture the depth,
    // sampled compared (sampler2DShadow, linear filter: 2 x 2 PCF)
    bool shadow = false;
};

struct ClearValues {
    bool color = true;
    bool depth = true;
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;
    float depthValue = 1;
};

struct Limits {
    uint32_t uniformBufferAlignment = 256;
    uint32_t maxUniformBlockSize = 16384;
    uint32_t maxTextureSize = 2048;
    int maxSamples = 1;      // of the render targets
    float maxAnisotropy = 1; // 1: no anisotropic filtering (GL 4.6 or the extension)
};

struct FrameStats {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t pipelineChanges = 0;
    uint32_t textureChanges = 0;
};

class Device {
public:
    virtual ~Device() = default;

    virtual const char *name() const = 0;
    virtual Limits limits() const = 0;

    virtual BufferHandle createBuffer(BufferUsage usage, const void *data, size_t size, bool dynamic) = 0;
    // replaces bytes of a dynamic buffer
    virtual void updateBuffer(BufferHandle b, size_t offset, const void *data, size_t size) = 0;
    virtual void destroyBuffer(BufferHandle b) = 0;

    // pixels: rows from the bottom, tightly packed; may be null
    virtual TextureHandle createTexture(const TextureDesc &desc, const void *pixels) = 0;
    virtual void destroyTexture(TextureHandle t) = 0;
    // replaces a rectangle of a texture (rows from the bottom, the format of
    // the texture); its mipmaps are not made again
    virtual void updateTexture(TextureHandle t, int x, int y, int width, int height, const void *pixels) = 0;

    virtual RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc) = 0;
    virtual void destroyRenderTarget(RenderTargetHandle r) = 0;
    // its color as a texture to sample (the depth of a shadow map); a
    // multisampled target is resolved into it by resolveRenderTarget, after its passes
    virtual TextureHandle renderTargetTexture(RenderTargetHandle r) = 0;
    virtual void resolveRenderTarget(RenderTargetHandle r) = 0;

    // null handle on error, the log in lastError()
    virtual ShaderHandle createShader(const ShaderDesc &desc) = 0;
    virtual void destroyShader(ShaderHandle s) = 0;
    virtual PipelineHandle createPipeline(const PipelineDesc &desc) = 0;
    virtual void destroyPipeline(PipelineHandle p) = 0;
    virtual const std::string &lastError() const = 0;

    // a pass into a render target, or the window when the handle is null
    virtual void beginPass(RenderTargetHandle target, int x, int y, int width, int height, const ClearValues &clear) = 0;
    // a pass on the window
    void beginPass(int x, int y, int width, int height, const ClearValues &clear) {
        beginPass(RenderTargetHandle{}, x, y, width, height, clear);
    }
    virtual void endPass() = 0;
    // the draws of the pass are clipped to a rectangle (from the bottom left),
    // until clearScissor() or the end of the pass
    virtual void setScissor(int x, int y, int width, int height) = 0;
    virtual void clearScissor() = 0;

    virtual void bindPipeline(PipelineHandle p) = 0;
    virtual void bindVertexBuffer(uint32_t slot, BufferHandle b, size_t offset = 0) = 0;
    virtual void bindIndexBuffer(BufferHandle b, IndexType type) = 0;
    virtual void bindUniformBuffer(uint32_t slot, BufferHandle b, size_t offset, size_t size) = 0;
    virtual void bindTexture(uint32_t slot, TextureHandle t) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0, uint32_t instances = 1) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t baseVertex = 0, uint32_t instances = 1) = 0;

    // reads the pixels of the last pass, RGBA8 from the bottom row
    virtual bool readPixels(int x, int y, int width, int height, void *rgba) = 0;

    // the statistics since the last call, then reset
    virtual FrameStats takeStats() = 0;

    // around the drawing of the device inside a host that also draws with the
    // same API (a game in fixed-pipeline OpenGL): the state of the host is
    // saved before and given back after
    virtual void beginHostInterop() {
    }
    virtual void endHostInterop() {
    }
};

// the OpenGL 3.3 backend, in the GL context current on this thread (a
// compatibility context works too). getProc: the function loader of the
// window system (SDL_GL_GetProcAddress, glfwGetProcAddress...).
std::unique_ptr<Device> createGLDevice(void *(*getProc)(const char *name), std::string *error = nullptr);

} // namespace r4::gpu
