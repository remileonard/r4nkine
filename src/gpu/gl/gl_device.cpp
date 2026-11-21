// gl_device.cpp - the OpenGL 3.3 backend of r4::gpu; every state is cached to skip the redundant calls.
#include "gl_api.h"

#include "r4/gpu/device.h"

#include <cstring>
#include <deque>

namespace r4::gl {

bool Api::load(void *(*getProc)(const char *), const char **missing) {
#define R4_GL_LOAD(ret, name, args)                                     \
    name = reinterpret_cast<ret(R4_GLAPI *) args>(getProc("gl" #name)); \
    if (!name) {                                                        \
        if (missing)                                                    \
            *missing = "gl" #name;                                      \
        return false;                                                   \
    }
    R4_GL_FUNCTIONS(R4_GL_LOAD)
#undef R4_GL_LOAD
    return true;
}

} // namespace r4::gl

namespace r4::gpu {
namespace {

using namespace r4::gl;

// a table of objects with handles: id - 1 is the index, freed slots reused
template <class T> struct Pool {
    std::vector<T> items;
    std::vector<bool> used;
    std::vector<uint32_t> freeList;
    uint32_t add(T v) {
        uint32_t i;
        if (!freeList.empty()) {
            i = freeList.back();
            freeList.pop_back();
            items[i] = std::move(v);
            used[i] = true;
        } else {
            i = (uint32_t)items.size();
            items.push_back(std::move(v));
            used.push_back(true);
        }
        return i + 1;
    }
    T *get(uint32_t id) {
        return id && id <= items.size() && used[id - 1] ? &items[id - 1] : nullptr;
    }
    void remove(uint32_t id) {
        if (get(id)) {
            used[id - 1] = false;
            items[id - 1] = T{};
            freeList.push_back(id - 1);
        }
    }
};

struct GLBuffer {
    GLuint name = 0;
    GLenum target = 0;
    size_t size = 0;
};
struct GLTexture {
    GLuint name = 0;
    GLenum format = 0; // of its pixels: RED, RG, RGB, RGBA
    int bpp = 4;
    bool owned = true; // false: the color of a render target, freed with it
};
struct GLRenderTarget {
    RenderTargetDesc desc;
    GLuint fbo = 0;     // the texture (and the depth when single sampled)
    GLuint msaaFbo = 0; // multisampled: where the passes draw
    GLuint colorRb = 0;
    GLuint depthRb = 0;
    uint32_t texture = 0;  // a handle in the textures
    bool external = false; // its color texture belongs to another library
};
struct GLShader {
    GLuint program = 0;
};

constexpr int kMaxStreams = 4;
constexpr int kMaxTextures = 8;
constexpr int kMaxUniformSlots = 8;

struct GLPipeline {
    PipelineDesc desc;
    GLuint program = 0;
    GLuint vao = 0;
    // what the VAO points at
    uint32_t streamBuffer[kMaxStreams] = {};
    size_t streamOffset[kMaxStreams] = {};
    uint32_t indexBuffer = 0;
};

GLenum attribType(AttribType t) {
    switch (t) {
    case AttribType::UBYTE:
        return UNSIGNED_BYTE;
    case AttribType::SHORT:
        return SHORT;
    default:
        return FLOAT;
    }
}

GLenum compareOp(CompareOp c) {
    switch (c) {
    case CompareOp::NEVER:
        return NEVER;
    case CompareOp::LESS:
        return LESS;
    case CompareOp::EQUAL:
        return EQUAL;
    case CompareOp::GREATER:
        return GREATER;
    case CompareOp::ALWAYS:
        return ALWAYS;
    default:
        return LEQUAL;
    }
}

GLenum primitive(Primitive p) {
    switch (p) {
    case Primitive::LINES:
        return LINES;
    case Primitive::POINTS:
        return POINTS;
    case Primitive::TRIANGLE_STRIP:
        return TRIANGLE_STRIP;
    default:
        return TRIANGLES;
    }
}

GLint wrapMode(Wrap w) {
    switch (w) {
    case Wrap::CLAMP:
        return CLAMP_TO_EDGE;
    case Wrap::MIRROR:
        return MIRRORED_REPEAT;
    default:
        return REPEAT;
    }
}

class GLDevice final : public Device {
public:
    bool init(void *(*getProc)(const char *), std::string *errorOut) {
        const char *missing = nullptr;
        if (!gl.load(getProc, &missing)) {
            if (errorOut) {
                *errorOut = std::string("OpenGL 3.3 function missing: ") + (missing ? missing : "?");
            }
            return false;
        }
        GLint v = 0;
        gl.GetIntegerv(UNIFORM_BUFFER_OFFSET_ALIGNMENT, &v);
        if (v > 0) {
            lim.uniformBufferAlignment = (uint32_t)v;
        }
        gl.GetIntegerv(MAX_UNIFORM_BLOCK_SIZE, &v);
        if (v > 0) {
            lim.maxUniformBlockSize = (uint32_t)v;
        }
        gl.GetIntegerv(MAX_TEXTURE_SIZE, &v);
        if (v > 0) {
            lim.maxTextureSize = (uint32_t)v;
        }
        v = 1;
        gl.GetIntegerv(MAX_SAMPLES, &v);
        lim.maxSamples = v > 0 ? v : 1;
        while (gl.GetError() != NO_ERROR_) {
        }
        GLfloat aniso = 1;
        gl.GetFloatv(MAX_TEXTURE_MAX_ANISOTROPY, &aniso);
        lim.maxAnisotropy = gl.GetError() == NO_ERROR_ && aniso > 1 ? aniso : 1;
        gl.GetIntegerv(FRAMEBUFFER_BINDING, &v);
        windowFbo = (GLuint)v;
        const char *renderer = (const char *)gl.GetString(RENDERER);
        const char *version = (const char *)gl.GetString(VERSION);
        nameStr = std::string("OpenGL ") + (version ? version : "?") + " (" + (renderer ? renderer : "?") + ")";
        return true;
    }

    ~GLDevice() override {
        releaseReadbacks();
        for (const GpuFrame &f : gpuFrames) {
            for (const GpuInterval &i : f) {
                gl.DeleteQueries(1, &i.query);
            }
        }
        for (const GpuInterval &i : gpuOpen) {
            gl.DeleteQueries(1, &i.query);
        }
        if (!gpuFree.empty()) {
            gl.DeleteQueries((GLsizei)gpuFree.size(), gpuFree.data());
        }
        for (size_t i = 0; i < pipelines.items.size(); i++) {
            if (pipelines.used[i]) {
                gl.DeleteVertexArrays(1, &pipelines.items[i].vao);
            }
        }
        for (size_t i = 0; i < buffers.items.size(); i++) {
            if (buffers.used[i]) {
                gl.DeleteBuffers(1, &buffers.items[i].name);
            }
        }
        for (size_t i = 0; i < textures.items.size(); i++) {
            if (textures.used[i]) {
                gl.DeleteTextures(1, &textures.items[i].name);
            }
        }
        for (size_t i = 0; i < shaders.items.size(); i++) {
            if (shaders.used[i]) {
                gl.DeleteProgram(shaders.items[i].program);
            }
        }
    }

    const char *name() const override {
        return nameStr.c_str();
    }
    Limits limits() const override {
        return lim;
    }
    const std::string &lastError() const override {
        return error;
    }

    BufferHandle createBuffer(BufferUsage usage, const void *data, size_t size, bool dynamic) override {
        GLBuffer b;
        b.target = usage == BufferUsage::INDEX ? ELEMENT_ARRAY_BUFFER : usage == BufferUsage::UNIFORM ? UNIFORM_BUFFER : ARRAY_BUFFER;
        b.size = size;
        gl.GenBuffers(1, &b.name);
        bindVAO(0);
        gl.BindBuffer(b.target, b.name);
        gl.BufferData(b.target, (GLsizeiptr)size, data, dynamic ? DYNAMIC_DRAW : STATIC_DRAW);
        gl.BindBuffer(b.target, 0);
        return {buffers.add(b)};
    }

    void updateBuffer(BufferHandle h, size_t offset, const void *data, size_t size) override {
        GLBuffer *b = buffers.get(h.id);
        if (!b || offset + size > b->size) {
            return;
        }
        bindVAO(0);
        gl.BindBuffer(b->target, b->name);
        gl.BufferSubData(b->target, (GLintptr)offset, (GLsizeiptr)size, data);
        gl.BindBuffer(b->target, 0);
    }

    void destroyBuffer(BufferHandle h) override {
        if (GLBuffer *b = buffers.get(h.id)) {
            gl.DeleteBuffers(1, &b->name);
            for (auto &u : uniformBound) {
                if (u.buffer == h.id) {
                    u = {};
                }
            }
            buffers.remove(h.id);
            for (size_t i = 0; i < pipelines.items.size(); i++) {
                GLPipeline &p = pipelines.items[i];
                for (int s = 0; s < kMaxStreams; s++) {
                    if (p.streamBuffer[s] == h.id) {
                        p.streamBuffer[s] = 0;
                    }
                }
                if (p.indexBuffer == h.id) {
                    p.indexBuffer = 0;
                }
            }
        }
    }

    TextureHandle createTexture(const TextureDesc &d, const void *pixels) override {
        GLTexture t;
        gl.GenTextures(1, &t.name);
        gl.ActiveTexture(TEXTURE0);
        gl.BindTexture(TEXTURE_2D, t.name);
        textureBound[0] = 0;
        GLint ifmt = RGBA8;
        GLenum fmt = RGBA;
        int bpp = 4;
        switch (d.format) {
        case TextureFormat::R8:
            ifmt = R8;
            fmt = RED;
            bpp = 1;
            break;
        case TextureFormat::RG8:
            ifmt = RG8;
            fmt = RG;
            bpp = 2;
            break;
        case TextureFormat::RGB8:
            ifmt = RGB8;
            fmt = RGB;
            bpp = 3;
            break;
        default:
            break;
        }
        gl.PixelStorei(UNPACK_ALIGNMENT, (d.width * bpp) % 4 == 0 ? 4 : 1);
        gl.TexImage2D(TEXTURE_2D, 0, ifmt, d.width, d.height, 0, fmt, UNSIGNED_BYTE, pixels);
        gl.PixelStorei(UNPACK_ALIGNMENT, 4);
        GLint minf = d.minFilter == Filter::LINEAR ? (d.mipmaps ? LINEAR_MIPMAP_LINEAR : LINEAR) : (d.mipmaps ? NEAREST_MIPMAP_NEAREST : NEAREST);
        gl.TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, minf);
        gl.TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, d.magFilter == Filter::LINEAR ? LINEAR : NEAREST);
        gl.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, wrapMode(d.wrapS));
        gl.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, wrapMode(d.wrapT));
        if (d.anisotropy > 1 && lim.maxAnisotropy > 1) {
            gl.TexParameterf(TEXTURE_2D, TEXTURE_MAX_ANISOTROPY, std::min(d.anisotropy, lim.maxAnisotropy));
        }
        if (d.mipmaps && pixels) {
            gl.GenerateMipmap(TEXTURE_2D);
        }
        gl.BindTexture(TEXTURE_2D, 0);
        t.format = fmt;
        t.bpp = bpp;
        return {textures.add(t)};
    }

    void updateTexture(TextureHandle h, int x, int y, int width, int height, const void *pixels) override {
        GLTexture *t = textures.get(h.id);
        if (!t || width <= 0 || height <= 0) {
            return;
        }
        gl.ActiveTexture(TEXTURE0);
        gl.BindTexture(TEXTURE_2D, t->name);
        textureBound[0] = 0;
        gl.PixelStorei(UNPACK_ALIGNMENT, (width * t->bpp) % 4 == 0 ? 4 : 1);
        gl.TexSubImage2D(TEXTURE_2D, 0, x, y, width, height, t->format, UNSIGNED_BYTE, pixels);
        gl.PixelStorei(UNPACK_ALIGNMENT, 4);
        gl.BindTexture(TEXTURE_2D, 0);
    }

    RenderTargetHandle createRenderTarget(const RenderTargetDesc &d) override {
        if (d.width <= 0 || d.height <= 0) {
            return {};
        }
        GLRenderTarget r;
        r.desc = d;
        if (d.shadow) {
            return createShadowTarget(r);
        }
        if (d.externalColor) {
            return createExternalTarget(r);
        }
        r.desc.samples = std::max(1, std::min(d.samples, lim.maxSamples));
        TextureDesc td;
        td.width = d.width;
        td.height = d.height;
        td.format = TextureFormat::RGBA8;
        td.minFilter = td.magFilter = d.filter;
        td.mipmaps = false;
        td.wrapS = td.wrapT = Wrap::CLAMP;
        r.texture = createTexture(td, nullptr).id;
        textures.get(r.texture)->owned = false;
        gl.GenFramebuffers(1, &r.fbo);
        gl.BindFramebuffer(FRAMEBUFFER, r.fbo);
        gl.FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, textures.get(r.texture)->name, 0);
        bool ok = true;
        if (r.desc.samples > 1) {
            gl.GenFramebuffers(1, &r.msaaFbo);
            gl.BindFramebuffer(FRAMEBUFFER, r.msaaFbo);
            gl.GenRenderbuffers(1, &r.colorRb);
            gl.BindRenderbuffer(RENDERBUFFER, r.colorRb);
            gl.RenderbufferStorageMultisample(RENDERBUFFER, r.desc.samples, RGBA8, d.width, d.height);
            gl.FramebufferRenderbuffer(FRAMEBUFFER, COLOR_ATTACHMENT0, RENDERBUFFER, r.colorRb);
        }
        if (d.depth) {
            gl.GenRenderbuffers(1, &r.depthRb);
            gl.BindRenderbuffer(RENDERBUFFER, r.depthRb);
            gl.RenderbufferStorageMultisample(RENDERBUFFER, r.desc.samples > 1 ? r.desc.samples : 0, DEPTH24_STENCIL8, d.width, d.height);
            gl.FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_STENCIL_ATTACHMENT, RENDERBUFFER, r.depthRb);
        }
        ok = gl.CheckFramebufferStatus(FRAMEBUFFER) == FRAMEBUFFER_COMPLETE;
        if (r.msaaFbo) {
            gl.BindFramebuffer(FRAMEBUFFER, r.fbo);
            ok = ok && gl.CheckFramebufferStatus(FRAMEBUFFER) == FRAMEBUFFER_COMPLETE;
        }
        gl.BindRenderbuffer(RENDERBUFFER, 0);
        gl.BindFramebuffer(FRAMEBUFFER, windowFbo);
        boundFbo = windowFbo;
        uint32_t id = targets.add(r);
        if (!ok) {
            error = "render target incomplete";
            destroyRenderTarget({id});
            return {};
        }
        return {id};
    }

    RenderTargetHandle createExternalTarget(GLRenderTarget &r) {
        r.desc.samples = 1;
        r.external = true;
        GLTexture t;
        t.name = (GLuint)r.desc.externalColor;
        t.format = RGBA;
        t.owned = false;
        r.texture = textures.add(t);
        gl.GenFramebuffers(1, &r.fbo);
        gl.BindFramebuffer(FRAMEBUFFER, r.fbo);
        gl.FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, t.name, 0);
        if (r.desc.depth) {
            gl.GenRenderbuffers(1, &r.depthRb);
            gl.BindRenderbuffer(RENDERBUFFER, r.depthRb);
            gl.RenderbufferStorageMultisample(RENDERBUFFER, 0, DEPTH24_STENCIL8, r.desc.width, r.desc.height);
            gl.FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_STENCIL_ATTACHMENT, RENDERBUFFER, r.depthRb);
            gl.BindRenderbuffer(RENDERBUFFER, 0);
        }
        bool ok = gl.CheckFramebufferStatus(FRAMEBUFFER) == FRAMEBUFFER_COMPLETE;
        gl.BindFramebuffer(FRAMEBUFFER, windowFbo);
        boundFbo = windowFbo;
        uint32_t id = targets.add(r);
        if (!ok) {
            error = "external render target incomplete";
            destroyRenderTarget({id});
            return {};
        }
        return {id};
    }

    RenderTargetHandle createShadowTarget(GLRenderTarget &r) {
        r.desc.samples = 1;
        GLTexture t;
        gl.GenTextures(1, &t.name);
        gl.ActiveTexture(TEXTURE0);
        gl.BindTexture(TEXTURE_2D, t.name);
        textureBound[0] = 0;
        gl.TexImage2D(TEXTURE_2D, 0, DEPTH_COMPONENT24, r.desc.width, r.desc.height, 0, DEPTH_COMPONENT, UNSIGNED_INT, nullptr);
        GLint f = r.desc.filter == Filter::LINEAR ? LINEAR : NEAREST;
        gl.TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, f);
        gl.TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, f);
        gl.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, CLAMP_TO_EDGE);
        gl.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, CLAMP_TO_EDGE);
        gl.TexParameteri(TEXTURE_2D, TEXTURE_COMPARE_MODE, COMPARE_REF_TO_TEXTURE);
        gl.TexParameteri(TEXTURE_2D, TEXTURE_COMPARE_FUNC, LEQUAL);
        gl.BindTexture(TEXTURE_2D, 0);
        t.format = DEPTH_COMPONENT;
        t.owned = false;
        r.texture = textures.add(t);
        gl.GenFramebuffers(1, &r.fbo);
        gl.BindFramebuffer(FRAMEBUFFER, r.fbo);
        gl.FramebufferTexture2D(FRAMEBUFFER, DEPTH_ATTACHMENT, TEXTURE_2D, t.name, 0);
        gl.DrawBuffer(NONE_);
        gl.ReadBuffer(NONE_);
        bool ok = gl.CheckFramebufferStatus(FRAMEBUFFER) == FRAMEBUFFER_COMPLETE;
        gl.BindFramebuffer(FRAMEBUFFER, windowFbo);
        boundFbo = windowFbo;
        uint32_t id = targets.add(r);
        if (!ok) {
            error = "shadow target incomplete";
            destroyRenderTarget({id});
            return {};
        }
        return {id};
    }

    void destroyRenderTarget(RenderTargetHandle h) override {
        GLRenderTarget *r = targets.get(h.id);
        if (!r) {
            return;
        }
        if (r->fbo) {
            gl.DeleteFramebuffers(1, &r->fbo);
        }
        if (r->msaaFbo) {
            gl.DeleteFramebuffers(1, &r->msaaFbo);
        }
        if (r->colorRb) {
            gl.DeleteRenderbuffers(1, &r->colorRb);
        }
        if (r->depthRb) {
            gl.DeleteRenderbuffers(1, &r->depthRb);
        }
        if (GLTexture *t = textures.get(r->texture); t && !r->external) {
            t->owned = true;
        }
        destroyTexture({r->texture});
        if (boundFbo == r->fbo || boundFbo == r->msaaFbo) {
            gl.BindFramebuffer(FRAMEBUFFER, windowFbo);
            boundFbo = windowFbo;
        }
        targets.remove(h.id);
    }

    TextureHandle renderTargetTexture(RenderTargetHandle h) override {
        GLRenderTarget *r = targets.get(h.id);
        return {r ? r->texture : 0};
    }

    void resolveRenderTarget(RenderTargetHandle h) override {
        GLRenderTarget *r = targets.get(h.id);
        if (!r || !r->msaaFbo) {
            return;
        }
        gl.BindFramebuffer(READ_FRAMEBUFFER, r->msaaFbo);
        gl.BindFramebuffer(DRAW_FRAMEBUFFER, r->fbo);
        setCap(SCISSOR_TEST, false, scissorEnabled);
        gl.BlitFramebuffer(0, 0, r->desc.width, r->desc.height, 0, 0, r->desc.width, r->desc.height, COLOR_BUFFER_BIT, NEAREST);
        gl.BindFramebuffer(FRAMEBUFFER, boundFbo);
    }

    void copyRenderTarget(RenderTargetHandle src, RenderTargetHandle dst) override {
        GLRenderTarget *s = targets.get(src.id);
        GLRenderTarget *d = targets.get(dst.id);
        if (!s || !d || d->msaaFbo || s->desc.shadow || d->desc.shadow) {
            return;
        }
        // a multisampled blit needs the same format at both ends: resolved first into the texture of src
        resolveRenderTarget(src);
        gl.BindFramebuffer(READ_FRAMEBUFFER, s->fbo);
        gl.BindFramebuffer(DRAW_FRAMEBUFFER, d->fbo);
        setCap(SCISSOR_TEST, false, scissorEnabled);
        bool same = s->desc.width == d->desc.width && s->desc.height == d->desc.height;
        gl.BlitFramebuffer(0, 0, s->desc.width, s->desc.height, 0, 0, d->desc.width, d->desc.height, COLOR_BUFFER_BIT, same ? NEAREST : LINEAR);
        gl.BindFramebuffer(FRAMEBUFFER, boundFbo);
    }

    void destroyTexture(TextureHandle h) override {
        if (GLTexture *t = textures.get(h.id); t && t->owned) {
            gl.DeleteTextures(1, &t->name);
            for (auto &b : textureBound) {
                if (b == h.id) {
                    b = 0;
                }
            }
            textures.remove(h.id);
        }
    }

    GLuint compile(GLenum type, const std::string &src, const std::string &what) {
        GLuint s = gl.CreateShader(type);
        const char *text = src.c_str();
        gl.ShaderSource(s, 1, &text, nullptr);
        gl.CompileShader(s);
        GLint ok = 0;
        gl.GetShaderiv(s, COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            gl.GetShaderiv(s, INFO_LOG_LENGTH, &len);
            std::string log(len > 0 ? len : 1, '\0');
            gl.GetShaderInfoLog(s, (GLsizei)log.size(), nullptr, log.data());
            error = what + ": " + log;
            gl.DeleteShader(s);
            return 0;
        }
        return s;
    }

    ShaderHandle createShader(const ShaderDesc &d) override {
        std::string what = d.debugName.empty() ? std::string("shader") : d.debugName;
        GLuint vs = compile(VERTEX_SHADER, d.vertex, what + " (vertex)");
        if (!vs) {
            return {};
        }
        GLuint fs = compile(FRAGMENT_SHADER, d.fragment, what + " (fragment)");
        if (!fs) {
            gl.DeleteShader(vs);
            return {};
        }
        GLuint p = gl.CreateProgram();
        gl.AttachShader(p, vs);
        gl.AttachShader(p, fs);
        gl.LinkProgram(p);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        GLint ok = 0;
        gl.GetProgramiv(p, LINK_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            gl.GetProgramiv(p, INFO_LOG_LENGTH, &len);
            std::string log(len > 0 ? len : 1, '\0');
            gl.GetProgramInfoLog(p, (GLsizei)log.size(), nullptr, log.data());
            error = what + " (link): " + log;
            gl.DeleteProgram(p);
            return {};
        }
        for (auto &[blockName, slot] : d.uniformBlocks) {
            GLuint index = gl.GetUniformBlockIndex(p, blockName.c_str());
            if (index != INVALID_INDEX) {
                gl.UniformBlockBinding(p, index, slot);
            }
        }
        if (!d.samplers.empty()) {
            gl.UseProgram(p);
            for (auto &[samplerName, slot] : d.samplers) {
                GLint loc = gl.GetUniformLocation(p, samplerName.c_str());
                if (loc >= 0) {
                    gl.Uniform1i(loc, (GLint)slot);
                }
            }
            // after a host interop the bound program is unknown: p stays bound and known
            if (currentProgram == (GLuint)-1) {
                currentProgram = p;
            } else {
                gl.UseProgram(currentProgram);
            }
        }
        return {shaders.add({p})};
    }

    void destroyShader(ShaderHandle h) override {
        if (GLShader *s = shaders.get(h.id)) {
            if (currentProgram == s->program) {
                gl.UseProgram(0);
                currentProgram = 0;
            }
            gl.DeleteProgram(s->program);
            shaders.remove(h.id);
        }
    }

    PipelineHandle createPipeline(const PipelineDesc &d) override {
        GLShader *s = shaders.get(d.shader.id);
        if (!s) {
            error = "createPipeline: no shader";
            return {};
        }
        if (d.layout.streams.size() > (size_t)kMaxStreams) {
            error = "createPipeline: too many vertex streams";
            return {};
        }
        GLPipeline p;
        p.desc = d;
        p.program = s->program;
        gl.GenVertexArrays(1, &p.vao);
        bindVAO(p.vao);
        for (const VertexStream &st : d.layout.streams) {
            for (const VertexAttrib &a : st.attribs) {
                gl.EnableVertexAttribArray(a.location);
                gl.VertexAttribDivisor(a.location, st.perInstance ? 1 : 0);
            }
        }
        bindVAO(0);
        return {pipelines.add(p)};
    }

    void destroyPipeline(PipelineHandle h) override {
        if (GLPipeline *p = pipelines.get(h.id)) {
            if (currentVAO == p->vao) {
                bindVAO(0);
            }
            gl.DeleteVertexArrays(1, &p->vao);
            if (current == h.id) {
                current = 0;
            }
            pipelines.remove(h.id);
        }
    }

    void beginPass(RenderTargetHandle target, int x, int y, int width, int height, const ClearValues &c) override {
        GLuint fbo = windowFbo;
        if (GLRenderTarget *r = targets.get(target.id)) {
            fbo = r->msaaFbo ? r->msaaFbo : r->fbo;
        }
        if (fbo != boundFbo) {
            gl.BindFramebuffer(FRAMEBUFFER, fbo);
            boundFbo = fbo;
        }
        gl.Viewport(x, y, width, height);
        setCap(SCISSOR_TEST, false, scissorEnabled);
        GLbitfield mask = 0;
        if (c.color) {
            gl.ClearColor(c.r, c.g, c.b, c.a);
            mask |= COLOR_BUFFER_BIT;
        }
        if (c.depth) {
            if (!depthWrite) {
                gl.DepthMask(TRUE_);
                depthWrite = true;
            }
            gl.ClearDepth(c.depthValue);
            mask |= DEPTH_BUFFER_BIT;
        }
        if (mask) {
            gl.Clear(mask);
        }
    }

    void bindForHost(RenderTargetHandle target) override {
        GLuint fbo = 0;
        if (GLRenderTarget *r = targets.get(target.id)) {
            fbo = r->msaaFbo ? r->msaaFbo : r->fbo;
        }
        gl.BindFramebuffer(FRAMEBUFFER, fbo);
        boundFbo = fbo;
    }

    void endPass() override {
        setCap(SCISSOR_TEST, false, scissorEnabled);
    }

    void setScissor(int x, int y, int width, int height) override {
        setCap(SCISSOR_TEST, true, scissorEnabled);
        gl.Scissor(x, y, width < 0 ? 0 : width, height < 0 ? 0 : height);
    }
    void clearScissor() override {
        setCap(SCISSOR_TEST, false, scissorEnabled);
    }

    void setCap(GLenum cap, bool on, int &cache) {
        if (cache != (int)on) {
            on ? gl.Enable(cap) : gl.Disable(cap);
            cache = on;
        }
    }

    void bindPipeline(PipelineHandle h) override {
        GLPipeline *p = pipelines.get(h.id);
        if (!p || current == h.id) {
            return;
        }
        current = h.id;
        stats.pipelineChanges++;
        const PipelineDesc &d = p->desc;
        if (currentProgram != p->program) {
            gl.UseProgram(p->program);
            currentProgram = p->program;
        }
        bindVAO(p->vao);
        setCap(CULL_FACE, d.cull != CullMode::NONE, cullEnabled);
        if (d.cull != CullMode::NONE) {
            GLenum mode = d.cull == CullMode::BACK ? BACK : FRONT;
            if (cullMode != mode) {
                gl.CullFace(mode);
                cullMode = mode;
            }
        }
        GLenum ff = d.frontCCW ? CCW : CW;
        if (frontFace != ff) {
            gl.FrontFace(ff);
            frontFace = ff;
        }
        setCap(DEPTH_TEST, d.depthTest, depthEnabled);
        if (depthWrite != d.depthWrite) {
            gl.DepthMask(GLboolean(d.depthWrite));
            depthWrite = d.depthWrite;
        }
        GLenum func = compareOp(d.depthCompare);
        if (depthFunc != func) {
            gl.DepthFunc(func);
            depthFunc = func;
        }
        setCap(BLEND, d.blend != BlendMode::NONE, blendEnabled);
        if (d.blend != BlendMode::NONE) {
            GLenum s = SRC_ALPHA;
            GLenum t = ONE_MINUS_SRC_ALPHA;
            if (d.blend == BlendMode::ADDITIVE) {
                t = ONE;
            } else if (d.blend == BlendMode::PREMULTIPLIED) {
                s = ONE;
            }
            if (blendSrc != s || blendDst != t) {
                gl.BlendFunc(s, t);
                blendSrc = s;
                blendDst = t;
            }
        }
        GLenum pm = d.wireframe ? LINE : FILL;
        if (polygonMode != pm) {
            gl.PolygonMode(FRONT_AND_BACK, pm);
            polygonMode = pm;
        }
        bool offset = d.polygonOffsetFactor != 0 || d.polygonOffsetUnits != 0;
        setCap(POLYGON_OFFSET_FILL, offset, offsetEnabled);
        if (offset) {
            gl.PolygonOffset(d.polygonOffsetFactor, d.polygonOffsetUnits);
        }
    }

    void bindVertexBuffer(uint32_t slot, BufferHandle b, size_t offset) override {
        if (slot < (uint32_t)kMaxStreams) {
            streams[slot] = b.id;
            streamOffsets[slot] = offset;
        }
    }

    void bindIndexBuffer(BufferHandle b, IndexType type) override {
        indexBuffer = b.id;
        indexType = type;
    }

    void bindUniformBuffer(uint32_t slot, BufferHandle h, size_t offset, size_t size) override {
        if (slot >= (uint32_t)kMaxUniformSlots) {
            return;
        }
        UniformBinding &u = uniformBound[slot];
        if (u.buffer == h.id && u.offset == offset && u.size == size) {
            return;
        }
        GLBuffer *b = buffers.get(h.id);
        if (!b) {
            return;
        }
        gl.BindBufferRange(UNIFORM_BUFFER, slot, b->name, (GLintptr)offset, (GLsizeiptr)size);
        u = {h.id, offset, size};
    }

    void bindTexture(uint32_t slot, TextureHandle h) override {
        if (slot >= (uint32_t)kMaxTextures || textureBound[slot] == h.id) {
            return;
        }
        GLTexture *t = textures.get(h.id);
        gl.ActiveTexture(TEXTURE0 + slot);
        gl.BindTexture(TEXTURE_2D, t ? t->name : 0);
        textureBound[slot] = t ? h.id : 0;
        stats.textureChanges++;
    }

    // OpenGL 3.3 has no separate vertex format: the attributes are pointed again when the buffers bound to the VAO changed
    bool prepareDraw(bool indexed) {
        GLPipeline *p = pipelines.get(current);
        if (!p) {
            return false;
        }
        bindVAO(p->vao);
        const VertexLayout &layout = p->desc.layout;
        for (size_t s = 0; s < layout.streams.size(); s++) {
            if (p->streamBuffer[s] == streams[s] && p->streamOffset[s] == streamOffsets[s]) {
                continue;
            }
            GLBuffer *b = buffers.get(streams[s]);
            if (!b) {
                return false;
            }
            gl.BindBuffer(ARRAY_BUFFER, b->name);
            const VertexStream &st = layout.streams[s];
            for (const VertexAttrib &a : st.attribs) {
                gl.VertexAttribPointer(a.location, (GLint)a.components, attribType(a.type), GLboolean(a.normalized), (GLsizei)st.stride, reinterpret_cast<const void *>(streamOffsets[s] + a.offset));
            }
            p->streamBuffer[s] = streams[s];
            p->streamOffset[s] = streamOffsets[s];
        }
        if (indexed && p->indexBuffer != indexBuffer) {
            GLBuffer *b = buffers.get(indexBuffer);
            if (!b) {
                return false;
            }
            gl.BindBuffer(ELEMENT_ARRAY_BUFFER, b->name);
            p->indexBuffer = indexBuffer;
        }
        return true;
    }

    void countDraw(uint32_t count, uint32_t instances) {
        GLPipeline *p = pipelines.get(current);
        stats.drawCalls++;
        if (p && p->desc.primitive == Primitive::TRIANGLES) {
            stats.triangles += count / 3 * instances;
        } else if (p && p->desc.primitive == Primitive::TRIANGLE_STRIP && count > 2) {
            stats.triangles += (count - 2) * instances;
        }
    }

    void draw(uint32_t vertexCount, uint32_t firstVertex, uint32_t instances) override {
        if (!vertexCount || !instances || !prepareDraw(false)) {
            return;
        }
        GLPipeline *p = pipelines.get(current);
        gl.DrawArraysInstanced(primitive(p->desc.primitive), (GLint)firstVertex, (GLsizei)vertexCount, (GLsizei)instances);
        countDraw(vertexCount, instances);
    }

    void drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t baseVertex, uint32_t instances) override {
        if (!indexCount || !instances || !prepareDraw(true)) {
            return;
        }
        GLPipeline *p = pipelines.get(current);
        size_t size = indexType == IndexType::U16 ? 2 : 4;
        gl.DrawElementsInstancedBaseVertex(primitive(p->desc.primitive), (GLsizei)indexCount, indexType == IndexType::U16 ? UNSIGNED_SHORT : UNSIGNED_INT, reinterpret_cast<const void *>(firstIndex * size), (GLsizei)instances, baseVertex);
        countDraw(indexCount, instances);
    }

    bool readPixels(int x, int y, int width, int height, void *rgba) override {
        gl.PixelStorei(PACK_ALIGNMENT, 1);
        gl.ReadPixels(x, y, width, height, RGBA, UNSIGNED_BYTE, rgba);
        gl.PixelStorei(PACK_ALIGNMENT, 4);
        return gl.GetError() == NO_ERROR_;
    }

    bool readPixelsAsync(int x, int y, int width, int height, void *rgba) override {
        const size_t size = (size_t)width * height * 4;
        if (size != readbackSize) {
            releaseReadbacks();
            for (Readback &r : readbacks) {
                gl.GenBuffers(1, &r.buffer);
                gl.BindBuffer(PIXEL_PACK_BUFFER, r.buffer);
                gl.BufferData(PIXEL_PACK_BUFFER, (GLsizeiptr)size, nullptr, STREAM_READ);
            }
            readbackSize = size;
        }
        Readback &start = readbacks[readbackNext];
        if (start.fence) {
            gl.DeleteSync(start.fence);
        }
        gl.BindBuffer(PIXEL_PACK_BUFFER, start.buffer);
        gl.PixelStorei(PACK_ALIGNMENT, 1);
        gl.ReadPixels(x, y, width, height, RGBA, UNSIGNED_BYTE, nullptr);
        gl.PixelStorei(PACK_ALIGNMENT, 4);
        start.fence = gl.FenceSync(SYNC_GPU_COMMANDS_COMPLETE, 0);
        start.order = ++readbackOrder;
        readbackNext = (readbackNext + 1) % kReadbacks;
        // the newest copy that is done, other than the one just started; the older ones are not needed any more
        Readback *done = nullptr;
        for (Readback &r : readbacks) {
            if (&r == &start || !r.fence || (done && r.order < done->order)) {
                continue;
            }
            const GLenum state = gl.ClientWaitSync(r.fence, 0, 0);
            if (state == ALREADY_SIGNALED || state == CONDITION_SATISFIED) {
                done = &r;
            }
        }
        bool read = false;
        if (done) {
            gl.BindBuffer(PIXEL_PACK_BUFFER, done->buffer);
            if (const void *p = gl.MapBufferRange(PIXEL_PACK_BUFFER, 0, (GLsizeiptr)size, MAP_READ_BIT)) {
                std::memcpy(rgba, p, size);
                read = true;
            }
            gl.UnmapBuffer(PIXEL_PACK_BUFFER);
            for (Readback &r : readbacks) {
                if (r.fence && r.order <= done->order) {
                    gl.DeleteSync(r.fence);
                    r.fence = nullptr;
                }
            }
        }
        gl.BindBuffer(PIXEL_PACK_BUFFER, 0);
        return read;
    }

    void beginGpuTimer(int slot) override {
        if (gpuActive || slot < 0 || slot >= kGpuTimers) {
            return;
        }
        GLuint query = 0;
        if (gpuFree.empty()) {
            gl.GenQueries(1, &query);
        } else {
            query = gpuFree.back();
            gpuFree.pop_back();
        }
        gl.BeginQuery(TIME_ELAPSED, query);
        gpuOpen.push_back({slot, query});
        gpuActive = true;
    }

    void endGpuTimer() override {
        if (gpuActive) {
            gl.EndQuery(TIME_ELAPSED);
            gpuActive = false;
        }
    }

    void endGpuFrame() override {
        endGpuTimer();
        gpuFrames.push_back(std::move(gpuOpen));
        gpuOpen.clear();
        // the frames that the GPU completed, oldest first; a frame left too long is dropped
        while (!gpuFrames.empty()) {
            GpuFrame &f = gpuFrames.front();
            bool done = true;
            for (const GpuInterval &i : f) {
                GLint available = 0;
                gl.GetQueryObjectiv(i.query, QUERY_RESULT_AVAILABLE, &available);
                done = done && available != 0;
            }
            if (!done && gpuFrames.size() <= kGpuFramesKept) {
                break;
            }
            if (done) {
                double ms[kGpuTimers] = {};
                for (const GpuInterval &i : f) {
                    GLuint64 ns = 0;
                    gl.GetQueryObjectui64v(i.query, QUERY_RESULT, &ns);
                    ms[i.slot] += (double)ns / 1e6;
                }
                std::memcpy(gpuLast, ms, sizeof ms);
                gpuKnown = true;
            }
            for (const GpuInterval &i : f) {
                gpuFree.push_back(i.query);
            }
            gpuFrames.pop_front();
        }
    }

    bool gpuTimes(double ms[kGpuTimers]) const override {
        if (gpuKnown) {
            std::memcpy(ms, gpuLast, sizeof gpuLast);
        }
        return gpuKnown;
    }

    FrameStats takeStats() override {
        FrameStats s = stats;
        stats = {};
        return s;
    }

    // the host draws with the same context: its state is saved and the caches of the device forgotten
    void beginHostInterop() override {
        GLint v;
        gl.GetIntegerv(CURRENT_PROGRAM, &v);
        host.program = v;
        gl.GetIntegerv(VERTEX_ARRAY_BINDING, &v);
        host.vao = v;
        gl.GetIntegerv(ARRAY_BUFFER_BINDING, &v);
        host.arrayBuffer = v;
        gl.GetIntegerv(ACTIVE_TEXTURE, &v);
        host.activeTexture = v;
        for (int i = 0; i < kMaxTextures; i++) {
            gl.ActiveTexture(TEXTURE0 + i);
            gl.GetIntegerv(TEXTURE_BINDING_2D, &v);
            host.textures[i] = v;
        }
        host.cull = gl.IsEnabled(CULL_FACE);
        host.depth = gl.IsEnabled(DEPTH_TEST);
        host.blend = gl.IsEnabled(BLEND);
        host.offset = gl.IsEnabled(POLYGON_OFFSET_FILL);
        host.scissor = gl.IsEnabled(SCISSOR_TEST);
        gl.GetIntegerv(CULL_FACE_MODE, &v);
        host.cullMode = v;
        gl.GetIntegerv(FRONT_FACE, &v);
        host.frontFace = v;
        gl.GetIntegerv(DEPTH_WRITEMASK, &v);
        host.depthMask = v;
        gl.GetIntegerv(DEPTH_FUNC, &v);
        host.depthFunc = v;
        gl.GetIntegerv(BLEND_SRC, &v);
        host.blendSrc = v;
        gl.GetIntegerv(BLEND_DST, &v);
        host.blendDst = v;
        GLint pm[2];
        gl.GetIntegerv(POLYGON_MODE, pm);
        host.polygonMode = pm[0];
        gl.GetIntegerv(VIEWPORT, host.viewport);
        gl.GetIntegerv(FRAMEBUFFER_BINDING, &v);
        host.fbo = v;
        boundFbo = (GLuint)v;
        windowFbo = (GLuint)v;
        forgetCaches();
    }

    void endHostInterop() override {
        gl.UseProgram(host.program);
        gl.BindVertexArray(host.vao);
        gl.BindBuffer(ARRAY_BUFFER, host.arrayBuffer);
        for (int i = 0; i < kMaxTextures; i++) {
            gl.ActiveTexture(TEXTURE0 + i);
            gl.BindTexture(TEXTURE_2D, host.textures[i]);
        }
        gl.ActiveTexture(host.activeTexture);
        host.cull ? gl.Enable(CULL_FACE) : gl.Disable(CULL_FACE);
        host.depth ? gl.Enable(DEPTH_TEST) : gl.Disable(DEPTH_TEST);
        host.blend ? gl.Enable(BLEND) : gl.Disable(BLEND);
        host.offset ? gl.Enable(POLYGON_OFFSET_FILL) : gl.Disable(POLYGON_OFFSET_FILL);
        host.scissor ? gl.Enable(SCISSOR_TEST) : gl.Disable(SCISSOR_TEST);
        gl.CullFace(host.cullMode);
        gl.FrontFace(host.frontFace);
        gl.DepthMask(GLboolean(host.depthMask));
        gl.DepthFunc(host.depthFunc);
        gl.BlendFunc(host.blendSrc, host.blendDst);
        gl.PolygonMode(FRONT_AND_BACK, host.polygonMode);
        gl.Viewport(host.viewport[0], host.viewport[1], host.viewport[2], host.viewport[3]);
        gl.BindFramebuffer(FRAMEBUFFER, (GLuint)host.fbo);
        boundFbo = (GLuint)host.fbo;
        forgetCaches();
    }

private:
    void bindVAO(GLuint vao) {
        if (currentVAO != vao) {
            gl.BindVertexArray(vao);
            currentVAO = vao;
        }
    }

    void releaseReadbacks() {
        for (Readback &r : readbacks) {
            if (r.fence) {
                gl.DeleteSync(r.fence);
            }
            if (r.buffer) {
                gl.DeleteBuffers(1, &r.buffer);
            }
            r = {};
        }
        readbackSize = 0;
    }

    void forgetCaches() {
        current = 0;
        currentProgram = (GLuint)-1;
        currentVAO = (GLuint)-1;
        cullEnabled = depthEnabled = blendEnabled = offsetEnabled = scissorEnabled = -1;
        cullMode = frontFace = depthFunc = blendSrc = blendDst = polygonMode = 0;
        depthWrite = true;
        for (auto &t : textureBound) {
            t = (uint32_t)-1;
        }
        for (auto &u : uniformBound) {
            u = {};
        }
        for (size_t i = 0; i < pipelines.items.size(); i++) {
            GLPipeline &p = pipelines.items[i];
            for (int s = 0; s < kMaxStreams; s++) {
                p.streamBuffer[s] = 0;
            }
            p.indexBuffer = 0;
        }
        gl.DepthMask(TRUE_);
    }

    Api gl;
    std::string nameStr;
    std::string error;
    Limits lim;
    FrameStats stats;
    Pool<GLBuffer> buffers;
    Pool<GLTexture> textures;
    Pool<GLShader> shaders;
    Pool<GLPipeline> pipelines;
    Pool<GLRenderTarget> targets;
    GLuint windowFbo = 0;
    GLuint boundFbo = 0;
    // the copies of readPixelsAsync: started in turn, read when their fence is signaled
    static constexpr int kReadbacks = 3;
    struct Readback {
        GLuint buffer = 0;
        GLsync fence = nullptr;
        uint64_t order = 0;
    } readbacks[kReadbacks];
    size_t readbackSize = 0;
    int readbackNext = 0;
    uint64_t readbackOrder = 0;
    // the queries of the GPU timers, by frame until the GPU completes it
    struct GpuInterval {
        int slot = 0;
        GLuint query = 0;
    };
    using GpuFrame = std::vector<GpuInterval>;
    static constexpr size_t kGpuFramesKept = 8;
    GpuFrame gpuOpen;
    std::deque<GpuFrame> gpuFrames;
    std::vector<GLuint> gpuFree;
    double gpuLast[kGpuTimers] = {};
    bool gpuKnown = false;
    bool gpuActive = false;

    uint32_t current = 0;
    GLuint currentProgram = 0;
    GLuint currentVAO = 0;
    int cullEnabled = -1;
    int depthEnabled = -1;
    int blendEnabled = -1;
    int offsetEnabled = -1;
    int scissorEnabled = -1;
    GLenum cullMode = 0;
    GLenum frontFace = 0;
    GLenum depthFunc = 0;
    GLenum blendSrc = 0;
    GLenum blendDst = 0;
    GLenum polygonMode = 0;
    bool depthWrite = true;
    uint32_t textureBound[kMaxTextures] = {};
    struct UniformBinding {
        uint32_t buffer = 0;
        size_t offset = 0;
        size_t size = 0;
    } uniformBound[kMaxUniformSlots];

    uint32_t streams[kMaxStreams] = {};
    size_t streamOffsets[kMaxStreams] = {};
    uint32_t indexBuffer = 0;
    IndexType indexType = IndexType::U32;

    struct HostState {
        GLint program = 0;
        GLint vao = 0;
        GLint arrayBuffer = 0;
        GLint activeTexture = TEXTURE0;
        GLint textures[kMaxTextures] = {};
        bool cull = false;
        bool depth = false;
        bool blend = false;
        bool offset = false;
        bool scissor = false;
        GLint cullMode = BACK;
        GLint frontFace = CCW;
        GLint depthMask = 1;
        GLint depthFunc = LESS;
        GLint blendSrc = ONE;
        GLint blendDst = ZERO;
        GLint polygonMode = FILL;
        GLint viewport[4] = {};
        GLint fbo = 0;
    } host;
};

} // namespace

std::unique_ptr<Device> createGLDevice(void *(*getProc)(const char *), std::string *error) {
    auto d = std::make_unique<GLDevice>();
    if (!d->init(getProc, error)) {
        return nullptr;
    }
    return d;
}

} // namespace r4::gpu
