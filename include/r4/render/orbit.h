// r4/render/orbit.h - a camera turning about a point, as the trackball of
// perfly: the mouse turns the view in the axes of the screen.
//
// Its orientation is a quaternion, so it turns freely; heading (yaw about
// Z), pitch (the elevation of the eye) and roll (about the line of sight)
// are read from it and written to it for the sliders. The camera looks
// along its +Y, with +X to the right and +Z up, as everything in R4nkine.
#pragma once

#include "r4/math.h"
#include "r4/render/renderer.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace r4::render {

class Orbit {
public:
    Vec3 target{0, 0, 0}; // the point it turns about
    float distance = 10;

    Orbit() { setAngles(30, 25, 0); }

    // heading: degrees about Z; pitch: degrees of the eye above the target;
    // roll: degrees about the line of sight
    void setAngles(float heading, float pitch, float roll) {
        q_ = glm::angleAxis(radians(heading), Vec3{0, 0, 1}) * glm::angleAxis(radians(-pitch), Vec3{1, 0, 0}) *
             glm::angleAxis(radians(roll), Vec3{0, 1, 0});
    }
    void angles(float &heading, float &pitch, float &roll) const {
        Vec3 f = forward(), u = up();
        pitch = degrees(std::asin(std::max(-1.0f, std::min(1.0f, -f.z))));
        heading = degrees(std::atan2(-f.x, f.y));
        // the up and the right without roll, for that heading and pitch
        glm::quat base =
            glm::angleAxis(radians(heading), Vec3{0, 0, 1}) * glm::angleAxis(radians(-pitch), Vec3{1, 0, 0});
        Vec3 u0 = base * Vec3{0, 0, 1}, r0 = base * Vec3{1, 0, 0};
        roll = degrees(std::atan2(dot(u, r0), dot(u, u0)));
    }

    // the mouse: dx turns about the up of the screen (yaw), dy about its
    // right (pitch); degrees
    void turn(float dx, float dy) {
        q_ = glm::normalize(q_ * glm::angleAxis(radians(-dx), Vec3{0, 0, 1}) *
                            glm::angleAxis(radians(-dy), Vec3{1, 0, 0}));
    }
    // the target moved in the plane of the screen, by world units
    void pan(float right, float up) { target += this->right() * right + this->up() * up; }

    Vec3 forward() const { return q_ * Vec3{0, 1, 0}; }
    Vec3 right() const { return q_ * Vec3{1, 0, 0}; }
    Vec3 up() const { return q_ * Vec3{0, 0, 1}; }
    Vec3 eye() const { return target - forward() * distance; }

    void apply(Camera &camera, float fovyDeg, float aspect, float znear, float zfar) const {
        camera.lookAt(eye(), target, up());
        camera.perspective(fovyDeg, aspect, znear, zfar);
    }

private:
    glm::quat q_;
};

} // namespace r4::render
