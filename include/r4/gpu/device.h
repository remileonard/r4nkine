// r4/gpu/device.h - the interface of the backends, after the explicit APIs: opaque handles, states baked into pipelines, uniforms in buffers bound by slot.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r4::gpu {

// 0 is none
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
// an image to draw into instead of the window
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
    // 1: off; limited by Limits::maxAnisotropy
    float anisotropy = 1;
};

enum class AttribType {
    FLOAT,
    UBYTE,
    SHORT
};

struct VertexAttrib {
    uint32_t location = 0;
    uint32_t components = 3; // 1 to 4
    AttribType type = AttribType::FLOAT;
    bool normalized = false;
    uint32_t offset = 0;
};

struct VertexStream {
    uint32_t stride = 0;
    bool perInstance = false; // one element per instance
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

// GLSL 330; the uniform blocks and samplers bound by name to the slots listed here
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
    float polygonOffsetFactor = 0;
    float polygonOffsetUnits = 0;
};

struct RenderTargetDesc {
    int width = 0;
    int height = 0;
    int samples = 1;   // > 1: MSAA, resolved into the texture
    bool depth = true; // and stencil
    Filter filter = Filter::LINEAR;
    // depth only, its texture sampled compared (sampler2DShadow)
    bool shadow = false;
    // not 0: the color is this texture of the API (a GL texture name), made by another library, as an image of an OpenXR swapchain.
    // The target draws into it and does not free it. samples is then 1.
    uint64_t externalColor = 0;
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
    int maxSamples = 1;
    float maxAnisotropy = 1; // 1: none
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
    virtual void updateBuffer(BufferHandle b, size_t offset, const void *data, size_t size) = 0;
    virtual void destroyBuffer(BufferHandle b) = 0;

    // rows from the bottom, tightly packed; may be null
    virtual TextureHandle createTexture(const TextureDesc &desc, const void *pixels) = 0;
    virtual void destroyTexture(TextureHandle t) = 0;
    // rows from the bottom, the format of the texture; the mipmaps are not made again
    virtual void updateTexture(TextureHandle t, int x, int y, int width, int height, const void *pixels) = 0;

    virtual RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc) = 0;
    virtual void destroyRenderTarget(RenderTargetHandle r) = 0;
    // its color, or its depth for a shadow map; a multisampled target is resolved into it by resolveRenderTarget
    virtual TextureHandle renderTargetTexture(RenderTargetHandle r) = 0;
    virtual void resolveRenderTarget(RenderTargetHandle r) = 0;
    // the color of src (resolved first when it is multisampled) into dst, scaled to its size; dst is single sampled
    virtual void copyRenderTarget(RenderTargetHandle src, RenderTargetHandle dst) = 0;

    // null handle on error, the log in lastError()
    virtual ShaderHandle createShader(const ShaderDesc &desc) = 0;
    virtual void destroyShader(ShaderHandle s) = 0;
    virtual PipelineHandle createPipeline(const PipelineDesc &desc) = 0;
    virtual void destroyPipeline(PipelineHandle p) = 0;
    virtual const std::string &lastError() const = 0;

    // into a render target, or the window when the handle is null
    virtual void beginPass(RenderTargetHandle target, int x, int y, int width, int height, const ClearValues &clear) = 0;
    void beginPass(int x, int y, int width, int height, const ClearValues &clear) {
        beginPass(RenderTargetHandle{}, x, y, width, height, clear);
    }
    virtual void endPass() = 0;
    // from the bottom left, until clearScissor() or the end of the pass
    virtual void setScissor(int x, int y, int width, int height) = 0;
    virtual void clearScissor() = 0;

    virtual void bindPipeline(PipelineHandle p) = 0;
    virtual void bindVertexBuffer(uint32_t slot, BufferHandle b, size_t offset = 0) = 0;
    virtual void bindIndexBuffer(BufferHandle b, IndexType type) = 0;
    virtual void bindUniformBuffer(uint32_t slot, BufferHandle b, size_t offset, size_t size) = 0;
    virtual void bindTexture(uint32_t slot, TextureHandle t) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0, uint32_t instances = 1) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t baseVertex = 0, uint32_t instances = 1) = 0;

    // RGBA8 from the bottom row
    virtual bool readPixels(int x, int y, int width, int height, void *rgba) = 0;
    // as readPixels, without waiting for the GPU: the copy of the pass starts now, and rgba receives the last copy that the GPU has done,
    // usually the one of the frame before; false when no copy is done yet. One reader at a time.
    virtual bool readPixelsAsync(int x, int y, int width, int height, void *rgba) {
        return readPixels(x, y, width, height, rgba);
    }

    // since the last call, then reset
    virtual FrameStats takeStats() = 0;

    // GPU times in slots: the commands between beginGpuTimer and endGpuTimer (one interval at a time, not nested) add to their slot.
    // endGpuFrame closes the frame. gpuTimes gives the milliseconds of each slot in the last frame that the GPU completed, without
    // waiting for the GPU; false while no frame is completed, or without timers.
    static constexpr int kGpuTimers = 8;
    virtual void beginGpuTimer(int slot) {
        (void)slot;
    }
    virtual void endGpuTimer() {
    }
    virtual void endGpuFrame() {
    }
    virtual bool gpuTimes(double ms[kGpuTimers]) const {
        (void)ms;
        return false;
    }

    // the state of a host drawing with the same API, saved before and given back after
    virtual void beginHostInterop() {
    }
    virtual void endHostInterop() {
    }
    // the host draws into target as into its window, until it binds another framebuffer; a null target is the window
    virtual void bindForHost(RenderTargetHandle target) {
        (void)target;
    }
};

// in the GL context current on this thread; getProc: the function loader of the window system
std::unique_ptr<Device> createGLDevice(void *(*getProc)(const char *name), std::string *error = nullptr);

} // namespace r4::gpu
