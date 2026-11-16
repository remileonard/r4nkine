// r4/math.h - the vectors, matrices and bounds of R4nkine.
//
// Matrices are column-major (m[column][row]), as OpenGL, Vulkan and the
// shaders expect them; vectors are columns: p' = M * p.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace r4 {

constexpr float kPi = 3.14159265358979323846f;
inline float radians(float deg) { return deg * (kPi / 180.0f); }
inline float degrees(float rad) { return rad * (180.0f / kPi); }

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    float &operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    float &operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline Vec3 operator/(Vec3 a, float s) { return a * (1.0f / s); }
inline Vec3 &operator+=(Vec3 &a, Vec3 b) { return a = a + b; }
inline Vec3 &operator-=(Vec3 &a, Vec3 b) { return a = a - b; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return l > 0 ? a / l : Vec3{0, 0, 0};
}
inline Vec3 min(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 max(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    static Mat4 identity() { return {}; }
    const float *data() const { return &m[0][0]; }
    Vec3 translation() const { return {m[3][0], m[3][1], m[3][2]}; }

    static Mat4 translate(Vec3 t) {
        Mat4 r;
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }
    static Mat4 scale(Vec3 s) {
        Mat4 r;
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        return r;
    }
    // a rotation of angle radians around a unit axis, counterclockwise
    static Mat4 rotate(float angle, Vec3 axis) {
        Vec3 a = normalize(axis);
        float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
        Mat4 r;
        r.m[0][0] = t * a.x * a.x + c;
        r.m[0][1] = t * a.x * a.y + s * a.z;
        r.m[0][2] = t * a.x * a.z - s * a.y;
        r.m[1][0] = t * a.x * a.y - s * a.z;
        r.m[1][1] = t * a.y * a.y + c;
        r.m[1][2] = t * a.y * a.z + s * a.x;
        r.m[2][0] = t * a.x * a.z + s * a.y;
        r.m[2][1] = t * a.y * a.z - s * a.x;
        r.m[2][2] = t * a.z * a.z + c;
        return r;
    }
    // OpenGL clip space: z from -1 to 1
    static Mat4 perspective(float fovy, float aspect, float znear, float zfar) {
        float f = 1.0f / std::tan(fovy * 0.5f);
        Mat4 r;
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = (zfar + znear) / (znear - zfar);
        r.m[2][3] = -1;
        r.m[3][2] = 2 * zfar * znear / (znear - zfar);
        r.m[3][3] = 0;
        return r;
    }
    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = normalize(center - eye), s = normalize(cross(f, up)), u = cross(s, f);
        Mat4 r;
        r.m[0][0] = s.x;
        r.m[1][0] = s.y;
        r.m[2][0] = s.z;
        r.m[0][1] = u.x;
        r.m[1][1] = u.y;
        r.m[2][1] = u.z;
        r.m[0][2] = -f.x;
        r.m[1][2] = -f.y;
        r.m[2][2] = -f.z;
        r.m[3][0] = -dot(s, eye);
        r.m[3][1] = -dot(u, eye);
        r.m[3][2] = dot(f, eye);
        return r;
    }

    Vec4 operator*(Vec4 v) const {
        Vec4 r;
        for (int i = 0; i < 4; i++)
            r[i] = m[0][i] * v.x + m[1][i] * v.y + m[2][i] * v.z + m[3][i] * v.w;
        return r;
    }
    Vec3 transformPoint(Vec3 p) const {
        Vec4 r = (*this) * Vec4{p.x, p.y, p.z, 1};
        return {r.x, r.y, r.z};
    }
    Vec3 transformVector(Vec3 v) const {
        Vec4 r = (*this) * Vec4{v.x, v.y, v.z, 0};
        return {r.x, r.y, r.z};
    }
    // the largest scale of the upper 3x3: how much a radius grows
    float maxScale() const {
        float sx = m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2];
        float sy = m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2];
        float sz = m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2];
        return std::sqrt(std::max(sx, std::max(sy, sz)));
    }
};

inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            r.m[c][i] = a.m[0][i] * b.m[c][0] + a.m[1][i] * b.m[c][1] + a.m[2][i] * b.m[c][2] +
                        a.m[3][i] * b.m[c][3];
    return r;
}

// the general inverse (cofactors); the identity when singular
Mat4 inverse(const Mat4 &a);
// the matrix of the normals: the inverse transpose of the upper 3x3, in a Mat4
Mat4 normalMatrix(const Mat4 &a);

// an axis-aligned box; empty when min > max
struct Box {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
             -std::numeric_limits<float>::max()};
    bool empty() const { return min.x > max.x; }
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
    Vec3 center() const { return (min + max) * 0.5f; }
};

// a bounding sphere; empty when radius < 0
struct Sphere {
    Vec3 center;
    float radius = -1;
    bool empty() const { return radius < 0; }
    // the smallest sphere around this one and s (approximately: exact for two spheres)
    void extend(const Sphere &s);
    Sphere transformed(const Mat4 &m) const {
        if (empty())
            return *this;
        return {m.transformPoint(center), radius * m.maxScale()};
    }
};
Sphere sphereOf(const Box &b);

// the six planes of a view volume, normals pointing inside: a point p is
// inside when dot(n, p) + d >= 0 for all of them
struct Frustum {
    Vec4 planes[6]; // left, right, bottom, top, near, far
    static Frustum fromMatrix(const Mat4 &viewProj);
    enum Result { Outside, Intersects, Inside };
    Result test(const Sphere &s) const;
};

} // namespace r4
