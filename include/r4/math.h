// r4/math.h - the vectors, matrices and bounds of R4nkine.
//
// The vectors and matrices are those of glm: Vec3 is glm::vec3, Mat4 is
// glm::mat4. Matrices are column-major (m[column][row]), as OpenGL, Vulkan
// and the shaders expect them; vectors are columns: p' = M * p. The clip
// space is that of OpenGL (z from -1 to 1).
//
// R4nkine needs glm with GLM_FORCE_CTOR_INIT (vectors at zero, matrices at
// the identity when built without values): the target r4nkine defines it
// for its users, and this header defines it when glm is not included yet.
#pragma once

#ifndef GLM_FORCE_CTOR_INIT
#define GLM_FORCE_CTOR_INIT
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#if GLM_CONFIG_CTOR_INIT == GLM_CTOR_INIT_DISABLE
#error "R4nkine needs glm with GLM_FORCE_CTOR_INIT: define it before including glm"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace r4 {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;

constexpr float kPi = 3.14159265358979323846f;
inline float radians(float deg) {
    return deg * (kPi / 180.0f);
}
inline float degrees(float rad) {
    return rad * (180.0f / kPi);
}

// glm's, found by their arguments too: dot, cross, length, distance
using glm::cross;
using glm::dot;
using glm::length;
// a view and a projection (OpenGL clip space), as glm makes them
inline Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    return glm::lookAt(eye, center, up);
}
inline Mat4 perspective(float fovy, float aspect, float znear, float zfar) {
    return glm::perspective(fovy, aspect, znear, zfar);
}

// a unit vector; the zero vector stays zero (glm would give NaN)
inline Vec3 normalize(Vec3 a) {
    float l = glm::length(a);
    return l > 0 ? a / l : Vec3(0);
}
inline Vec3 min(Vec3 a, Vec3 b) {
    return glm::min(a, b);
}
inline Vec3 max(Vec3 a, Vec3 b) {
    return glm::max(a, b);
}

// the matrices of a transform
inline Mat4 translate(Vec3 t) {
    return glm::translate(Mat4(1.0f), t);
}
inline Mat4 scale(Vec3 s) {
    return glm::scale(Mat4(1.0f), s);
}
// a rotation of angle radians around an axis, counterclockwise
inline Mat4 rotate(float angle, Vec3 axis) {
    return glm::rotate(Mat4(1.0f), angle, normalize(axis));
}

inline Vec3 transformPoint(const Mat4 &m, Vec3 p) {
    return Vec3(m * Vec4(p, 1.0f));
}
inline Vec3 transformVector(const Mat4 &m, Vec3 v) {
    return Vec3(m * Vec4(v, 0.0f));
}
inline Vec3 translationOf(const Mat4 &m) {
    return Vec3(m[3]);
}
// the largest scale of the upper 3x3: how much a radius grows
inline float maxScale(const Mat4 &m) {
    return std::sqrt(std::max(glm::dot(Vec3(m[0]), Vec3(m[0])), std::max(glm::dot(Vec3(m[1]), Vec3(m[1])), glm::dot(Vec3(m[2]), Vec3(m[2])))));
}
inline const float *dataOf(const Mat4 &m) {
    return &m[0][0];
}

// the general inverse; the identity when singular
Mat4 inverse(const Mat4 &a);
// the matrix of the normals: the inverse transpose of the upper 3x3, in a Mat4
Mat4 normalMatrix(const Mat4 &a);

// an axis-aligned box; empty when min > max
struct Box {
    Vec3 min{std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max()};
    bool empty() const {
        return min.x > max.x;
    }
    void extend(Vec3 p) {
        min = r4::min(min, p);
        max = r4::max(max, p);
    }
    void extend(const Box &b) {
        if (!b.empty()) {
            extend(b.min);
            extend(b.max);
        }
    }
    Vec3 center() const {
        return (min + max) * 0.5f;
    }
};

// a bounding sphere; empty when radius < 0
struct Sphere {
    Vec3 center;
    float radius = -1;
    bool empty() const {
        return radius < 0;
    }
    // the smallest sphere around this one and s (approximately: exact for two spheres)
    void extend(const Sphere &s);
    Sphere transformed(const Mat4 &m) const {
        if (empty()) {
            return *this;
        }
        return {transformPoint(m, center), radius * maxScale(m)};
    }
};
Sphere sphereOf(const Box &b);

// the six planes of a view volume, normals pointing inside: a point p is
// inside when dot(n, p) + d >= 0 for all of them
struct Frustum {
    Vec4 planes[6]; // left, right, bottom, top, near, far
    static Frustum fromMatrix(const Mat4 &viewProj);
    enum Result {
        Outside,
        Intersects,
        Inside
    };
    Result test(const Sphere &s) const;
};

} // namespace r4
