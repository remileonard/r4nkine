// gl_api.h - the OpenGL 3.3 functions and constants the GL backend uses,
// loaded at run time: no GL header nor loader library is needed.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define R4_GLAPI __stdcall
#else
#define R4_GLAPI
#endif

namespace r4::gl {

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLdouble = double;
using GLchar = char;
using GLubyte = unsigned char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

enum : GLenum {
    FALSE_ = 0,
    TRUE_ = 1,
    NO_ERROR_ = 0,
    POINTS = 0x0000,
    LINES = 0x0001,
    TRIANGLES = 0x0004,
    TRIANGLE_STRIP = 0x0005,
    NEVER = 0x0200,
    LESS = 0x0201,
    EQUAL = 0x0202,
    LEQUAL = 0x0203,
    GREATER = 0x0204,
    ALWAYS = 0x0207,
    ZERO = 0,
    ONE = 1,
    SRC_ALPHA = 0x0302,
    ONE_MINUS_SRC_ALPHA = 0x0303,
    FRONT = 0x0404,
    BACK = 0x0405,
    FRONT_AND_BACK = 0x0408,
    CULL_FACE = 0x0B44,
    CULL_FACE_MODE = 0x0B45,
    FRONT_FACE = 0x0B46,
    DEPTH_TEST = 0x0B71,
    DEPTH_WRITEMASK = 0x0B72,
    DEPTH_FUNC = 0x0B74,
    BLEND = 0x0BE2,
    BLEND_SRC = 0x0BE1,
    BLEND_DST = 0x0BE0,
    VIEWPORT = 0x0BA2,
    SCISSOR_TEST = 0x0C11,
    POLYGON_MODE = 0x0B40,
    PACK_ALIGNMENT = 0x0D05,
    UNPACK_ALIGNMENT = 0x0CF5,
    TEXTURE_2D = 0x0DE1,
    BYTE = 0x1400,
    UNSIGNED_BYTE = 0x1401,
    SHORT = 0x1402,
    UNSIGNED_SHORT = 0x1403,
    UNSIGNED_INT = 0x1405,
    FLOAT = 0x1406,
    RED = 0x1903,
    RGB = 0x1907,
    RGBA = 0x1908,
    LINE = 0x1B01,
    FILL = 0x1B02,
    VENDOR = 0x1F00,
    RENDERER = 0x1F01,
    VERSION = 0x1F02,
    NEAREST = 0x2600,
    LINEAR = 0x2601,
    LINEAR_MIPMAP_LINEAR = 0x2703,
    NEAREST_MIPMAP_NEAREST = 0x2700,
    TEXTURE_MAG_FILTER = 0x2800,
    TEXTURE_MIN_FILTER = 0x2801,
    TEXTURE_WRAP_S = 0x2802,
    TEXTURE_WRAP_T = 0x2803,
    REPEAT = 0x2901,
    POLYGON_OFFSET_FILL = 0x8037,
    TEXTURE_BINDING_2D = 0x8069,
    CLAMP_TO_EDGE = 0x812F,
    MIRRORED_REPEAT = 0x8370,
    MAX_TEXTURE_SIZE = 0x0D33,
    COLOR_BUFFER_BIT = 0x00004000,
    DEPTH_BUFFER_BIT = 0x00000100,
    CCW = 0x0901,
    CW = 0x0900,
    R8 = 0x8229,
    RG = 0x8227,
    RG8 = 0x822B,
    RGB8 = 0x8051,
    RGBA8 = 0x8058,
    TEXTURE0 = 0x84C0,
    ACTIVE_TEXTURE = 0x84E0,
    ARRAY_BUFFER = 0x8892,
    ELEMENT_ARRAY_BUFFER = 0x8893,
    ARRAY_BUFFER_BINDING = 0x8894,
    STATIC_DRAW = 0x88E4,
    DYNAMIC_DRAW = 0x88E8,
    UNIFORM_BUFFER = 0x8A11,
    UNIFORM_BUFFER_OFFSET_ALIGNMENT = 0x8A34,
    MAX_UNIFORM_BLOCK_SIZE = 0x8A30,
    FRAGMENT_SHADER = 0x8B30,
    VERTEX_SHADER = 0x8B31,
    COMPILE_STATUS = 0x8B81,
    LINK_STATUS = 0x8B82,
    INFO_LOG_LENGTH = 0x8B84,
    CURRENT_PROGRAM = 0x8B8D,
    VERTEX_ARRAY_BINDING = 0x85B5,
    INVALID_INDEX = 0xFFFFFFFFu,
    FRAMEBUFFER = 0x8D40,
    READ_FRAMEBUFFER = 0x8CA8,
    DRAW_FRAMEBUFFER = 0x8CA9,
    FRAMEBUFFER_BINDING = 0x8CA6,
    COLOR_ATTACHMENT0 = 0x8CE0,
    DEPTH_STENCIL_ATTACHMENT = 0x821A,
    RENDERBUFFER = 0x8D41,
    FRAMEBUFFER_COMPLETE = 0x8CD5,
    DEPTH24_STENCIL8 = 0x88F0,
    NONE_ = 0,
    DEPTH_COMPONENT = 0x1902,
    DEPTH_COMPONENT24 = 0x81A6,
    DEPTH_ATTACHMENT = 0x8D00,
    TEXTURE_COMPARE_MODE = 0x884C,
    TEXTURE_COMPARE_FUNC = 0x884D,
    COMPARE_REF_TO_TEXTURE = 0x884E,
    MAX_SAMPLES = 0x8D57,
    STENCIL_BUFFER_BIT = 0x00000400,
};

#define R4_GL_FUNCTIONS(X)                                                                          \
    X(const GLubyte *, GetString, (GLenum name))                                                    \
    X(void, GetIntegerv, (GLenum pname, GLint * data))                                              \
    X(GLenum, GetError, (void))                                                                     \
    X(GLboolean, IsEnabled, (GLenum cap))                                                           \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                    \
    X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h))                                     \
    X(void, Enable, (GLenum cap))                                                                   \
    X(void, Disable, (GLenum cap))                                                                  \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                               \
    X(void, ClearDepth, (GLdouble d))                                                               \
    X(void, Clear, (GLbitfield mask))                                                               \
    X(void, DepthMask, (GLboolean flag))                                                            \
    X(void, DepthFunc, (GLenum func))                                                               \
    X(void, CullFace, (GLenum mode))                                                                \
    X(void, FrontFace, (GLenum mode))                                                               \
    X(void, BlendFunc, (GLenum s, GLenum d))                                                        \
    X(void, PolygonMode, (GLenum face, GLenum mode))                                                \
    X(void, PolygonOffset, (GLfloat factor, GLfloat units))                                         \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p))      \
    X(void, PixelStorei, (GLenum pname, GLint param))                                               \
    X(void, GenBuffers, (GLsizei n, GLuint * b))                                                    \
    X(void, DeleteBuffers, (GLsizei n, const GLuint *b))                                            \
    X(void, BindBuffer, (GLenum target, GLuint b))                                                  \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage))           \
    X(void, BufferSubData, (GLenum target, GLintptr off, GLsizeiptr size, const void *data))        \
    X(void, BindBufferRange, (GLenum target, GLuint index, GLuint b, GLintptr off, GLsizeiptr size)) \
    X(void, GenTextures, (GLsizei n, GLuint * t))                                                   \
    X(void, DeleteTextures, (GLsizei n, const GLuint *t))                                           \
    X(void, BindTexture, (GLenum target, GLuint t))                                                 \
    X(void, ActiveTexture, (GLenum unit))                                                           \
    X(void, TexImage2D, (GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, \
                         GLenum fmt, GLenum type, const void *pixels))                              \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param))                              \
    X(void, TexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,      \
                            GLenum fmt, GLenum type, const void *pixels))                           \
    X(void, GenFramebuffers, (GLsizei n, GLuint * f))                                               \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint *f))                                       \
    X(void, BindFramebuffer, (GLenum target, GLuint f))                                             \
    X(void, FramebufferTexture2D, (GLenum target, GLenum att, GLenum textarget, GLuint t, GLint level)) \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                              \
    X(void, DrawBuffer, (GLenum mode))                                                              \
    X(void, ReadBuffer, (GLenum mode))                                                              \
    X(void, GenRenderbuffers, (GLsizei n, GLuint * r))                                              \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint *r))                                      \
    X(void, BindRenderbuffer, (GLenum target, GLuint r))                                            \
    X(void, RenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum ifmt, GLsizei w, \
                                             GLsizei h))                                            \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum att, GLenum rbtarget, GLuint r))        \
    X(void, BlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,     \
                              GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))               \
    X(void, GenerateMipmap, (GLenum target))                                                        \
    X(GLuint, CreateShader, (GLenum type))                                                          \
    X(void, ShaderSource, (GLuint s, GLsizei n, const GLchar *const *src, const GLint *len))        \
    X(void, CompileShader, (GLuint s))                                                              \
    X(void, GetShaderiv, (GLuint s, GLenum pname, GLint * p))                                       \
    X(void, GetShaderInfoLog, (GLuint s, GLsizei max, GLsizei * len, GLchar * log))                 \
    X(void, DeleteShader, (GLuint s))                                                               \
    X(GLuint, CreateProgram, (void))                                                                \
    X(void, AttachShader, (GLuint p, GLuint s))                                                     \
    X(void, LinkProgram, (GLuint p))                                                                \
    X(void, GetProgramiv, (GLuint p, GLenum pname, GLint * v))                                      \
    X(void, GetProgramInfoLog, (GLuint p, GLsizei max, GLsizei * len, GLchar * log))                \
    X(void, DeleteProgram, (GLuint p))                                                              \
    X(void, UseProgram, (GLuint p))                                                                 \
    X(GLuint, GetUniformBlockIndex, (GLuint p, const GLchar *name))                                 \
    X(void, UniformBlockBinding, (GLuint p, GLuint index, GLuint binding))                          \
    X(GLint, GetUniformLocation, (GLuint p, const GLchar *name))                                    \
    X(void, Uniform1i, (GLint loc, GLint v))                                                        \
    X(void, GenVertexArrays, (GLsizei n, GLuint * a))                                               \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint *a))                                       \
    X(void, BindVertexArray, (GLuint a))                                                            \
    X(void, EnableVertexAttribArray, (GLuint index))                                                \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean norm, GLsizei stride, \
                                  const void *ptr))                                                 \
    X(void, VertexAttribDivisor, (GLuint index, GLuint divisor))                                    \
    X(void, DrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instances))      \
    X(void, DrawElementsInstancedBaseVertex, (GLenum mode, GLsizei count, GLenum type, const void *idx, \
                                              GLsizei instances, GLint base))

struct Api {
#define R4_GL_DECLARE(ret, name, args) ret(R4_GLAPI *name) args = nullptr;
    R4_GL_FUNCTIONS(R4_GL_DECLARE)
#undef R4_GL_DECLARE

    // loads every function; the name of the first missing one in missing
    bool load(void *(*getProc)(const char *), const char **missing);
};

} // namespace r4::gl
