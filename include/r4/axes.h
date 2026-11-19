// r4/axes.h - the axes of R4nkine, named in one place.
//
// R4nkine is right-handed and Z-up, as IRIS Performer, OpenFlight, Vega,
// OpenSceneGraph, VulkanSceneGraph and the flight simulators:
//
//     X right, Y forward, Z up; meters.
//
// The rule: one convention inside the engine, a conversion at each border.
// A loader of a Y-up format (OBJ, glTF, AC3D, the gobj and YAODL of the SGI
// demos) turns its root by fromYUpMatrix(); a game whose own code is Y-up
// (libRealSpace) turns its world once, at its root, the same way; an XR
// runtime (OpenXR is Y-up) turns its tracking space. Nothing else converts.
//
// The code of R4nkine names these axes instead of writing {0, 0, 1}: the
// places that depend on the convention are the users of this header.
#pragma once

#include "r4/math.h"

#include <algorithm>
#include <cmath>

namespace r4::axes {

inline const Vec3 kRight{1, 0, 0};
inline const Vec3 kForward{0, 1, 0};
inline const Vec3 kUp{0, 0, 1};

// --- the Y-up right-handed axes (OpenGL's camera, OBJ, glTF, OpenXR):
// X right, Y up, -Z forward. A turn of 90 degrees about X: the handedness
// and the winding of the faces are kept.
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
// a transform written in Y-up axes (a pose of OpenXR, a matrix of a Y-up
// game), in R4nkine's
inline Mat4 transformFromYUp(const Mat4 &m) {
    return fromYUpMatrix() * m * toYUpMatrix();
}

// --- heights and angles in these axes
inline float heightOf(Vec3 p) {
    return dot(p, kUp);
}
// the heading of a direction, degrees about kUp: 0 along kForward, positive
// to the left (counterclockwise seen from above)
inline float headingOf(Vec3 d) {
    return degrees(std::atan2(-dot(d, kRight), dot(d, kForward)));
}
// the pitch of a direction, degrees: positive above the horizon
inline float pitchOf(Vec3 d) {
    float l = length(d);
    return l > 0 ? degrees(std::asin(std::clamp(dot(d, kUp) / l, -1.0f, 1.0f))) : 0.0f;
}

} // namespace r4::axes
