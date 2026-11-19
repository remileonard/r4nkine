#include "r4/math.h"

namespace r4 {

Mat4 inverse(const Mat4 &a) {
    float det = glm::determinant(a);
    if (std::fabs(det) < 1e-30f) {
        return Mat4(1.0f);
    }
    return glm::inverse(a);
}

Mat4 normalMatrix(const Mat4 &a) {
    glm::mat3 u(a);
    if (std::fabs(glm::determinant(u)) < 1e-30f) {
        return Mat4(1.0f);
    }
    return Mat4(glm::transpose(glm::inverse(u)));
}

void Sphere::extend(const Sphere &s) {
    if (s.empty()) {
        return;
    }
    if (empty()) {
        *this = s;
        return;
    }
    Vec3 d = s.center - center;
    float dist = length(d);
    if (dist + s.radius <= radius) {
        return; // s is inside
    }
    if (dist + radius <= s.radius) {
        *this = s; // this is inside s
        return;
    }
    float r = (dist + radius + s.radius) * 0.5f;
    center = center + d * ((r - radius) / dist);
    radius = r;
}

Sphere sphereOf(const Box &b) {
    if (b.empty()) {
        return {};
    }
    return {b.center(), length(b.max - b.min) * 0.5f};
}

Frustum Frustum::fromMatrix(const Mat4 &vp) {
    // Gribb & Hartmann: the rows of the matrix
    auto row = [&](int i) {
        return Vec4{vp[0][i], vp[1][i], vp[2][i], vp[3][i]};
    };
    Vec4 r0 = row(0);
    Vec4 r1 = row(1);
    Vec4 r2 = row(2);
    Vec4 r3 = row(3);
    auto add = [](Vec4 a, Vec4 b, float s) {
        return Vec4{a.x + s * b.x, a.y + s * b.y, a.z + s * b.z, a.w + s * b.w};
    };
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
    if (s.empty()) {
        return Outside;
    }
    Result r = Inside;
    for (const Vec4 &p : planes) {
        float d = p.x * s.center.x + p.y * s.center.y + p.z * s.center.z + p.w;
        if (d < -s.radius) {
            return Outside;
        }
        if (d < s.radius) {
            r = Intersects;
        }
    }
    return r;
}

} // namespace r4
