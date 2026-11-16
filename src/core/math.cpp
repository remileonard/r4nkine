#include "r4/math.h"

namespace r4 {

Mat4 inverse(const Mat4 &a) {
    const float *m = a.data();
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    Mat4 r;
    if (std::fabs(det) < 1e-30f)
        return r;
    det = 1.0f / det;
    float *o = &r.m[0][0];
    for (int i = 0; i < 16; i++)
        o[i] = inv[i] * det;
    return r;
}

Mat4 normalMatrix(const Mat4 &a) {
    Mat4 u = a;
    u.m[3][0] = u.m[3][1] = u.m[3][2] = 0;
    u.m[0][3] = u.m[1][3] = u.m[2][3] = 0;
    u.m[3][3] = 1;
    Mat4 i = inverse(u), t;
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            t.m[c][r] = i.m[r][c];
    return t;
}

void Sphere::extend(const Sphere &s) {
    if (s.empty())
        return;
    if (empty()) {
        *this = s;
        return;
    }
    Vec3 d = s.center - center;
    float dist = length(d);
    if (dist + s.radius <= radius)
        return; // s is inside
    if (dist + radius <= s.radius) {
        *this = s; // this is inside s
        return;
    }
    float r = (dist + radius + s.radius) * 0.5f;
    center = center + d * ((r - radius) / dist);
    radius = r;
}

Sphere sphereOf(const Box &b) {
    if (b.empty())
        return {};
    return {b.center(), length(b.max - b.min) * 0.5f};
}

Frustum Frustum::fromMatrix(const Mat4 &vp) {
    // Gribb & Hartmann: the rows of the matrix
    auto row = [&](int i) { return Vec4{vp.m[0][i], vp.m[1][i], vp.m[2][i], vp.m[3][i]}; };
    Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto add = [](Vec4 a, Vec4 b, float s) { return Vec4{a.x + s * b.x, a.y + s * b.y, a.z + s * b.z, a.w + s * b.w}; };
    Frustum f;
    f.planes[0] = add(r3, r0, 1);
    f.planes[1] = add(r3, r0, -1);
    f.planes[2] = add(r3, r1, 1);
    f.planes[3] = add(r3, r1, -1);
    f.planes[4] = add(r3, r2, 1);
    f.planes[5] = add(r3, r2, -1);
    for (Vec4 &p : f.planes) {
        float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (l > 0) {
            p.x /= l;
            p.y /= l;
            p.z /= l;
            p.w /= l;
        }
    }
    return f;
}

Frustum::Result Frustum::test(const Sphere &s) const {
    if (s.empty())
        return Outside;
    Result r = Inside;
    for (const Vec4 &p : planes) {
        float d = p.x * s.center.x + p.y * s.center.y + p.z * s.center.z + p.w;
        if (d < -s.radius)
            return Outside;
        if (d < s.radius)
            r = Intersects;
    }
    return r;
}

} // namespace r4
