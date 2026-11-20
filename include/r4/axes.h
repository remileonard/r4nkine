// r4/axes.h - the axes of R4nkine: right-handed, X right, Y forward, Z up, in meters, as Performer and OpenFlight.
// The Y-up data (OBJ, glTF, OpenXR, libRealSpace) are turned once at their root by fromYUpMatrix().
#pragma once

#include "r4/math.h"

#include <algorithm>
#include <cmath>

namespace r4::axes {

inline const Vec3 kRight{1, 0, 0};
inline const Vec3 kForward{0, 1, 0};
inline const Vec3 kUp{0, 0, 1};

// Y-up right-handed: X right, Y up, -Z forward; a turn of 90 degrees about X
inline Vec3 fromYUp(Vec3 v) {
    return {v.x, -v.z, v.y};
}
inline Vec3 toYUp(Vec3 v) {
    return {v.x, v.z, -v.y};
}
inline Mat4 fromYUpMatrix() {
    return rotate(radians(90.0f), kRight);
}
inline Mat4 toYUpMatrix() {
    return rotate(radians(-90.0f), kRight);
}
inline Mat4 transformFromYUp(const Mat4 &m) {
    return fromYUpMatrix() * m * toYUpMatrix();
}

inline float heightOf(Vec3 p) {
    return dot(p, kUp);
}
// degrees about kUp: 0 along kForward, positive to the left
inline float headingOf(Vec3 d) {
    return degrees(std::atan2(-dot(d, kRight), dot(d, kForward)));
}
// degrees, positive above the horizon
inline float pitchOf(Vec3 d) {
    float l = length(d);
    return l > 0 ? degrees(std::asin(std::clamp(dot(d, kUp) / l, -1.0f, 1.0f))) : 0.0f;
}

} // namespace r4::axes
