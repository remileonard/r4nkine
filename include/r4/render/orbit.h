// r4/render/orbit.h - a camera turning about a point, as the trackball of perfly; a quaternion, read and written as heading, pitch and roll.
#pragma once

#include "r4/axes.h"
#include "r4/math.h"
#include "r4/render/renderer.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace r4::render {

class Orbit {
public:
    Vec3 target{0, 0, 0};
    float distance = 10;

    Orbit() {
        setAngles(30, 25, 0);
    }

    // degrees: heading about axes::kUp, pitch of the eye above the target, roll about the line of sight
    void setAngles(float heading, float pitch, float roll) {
        q_ = glm::angleAxis(radians(heading), axes::kUp) * glm::angleAxis(radians(-pitch), axes::kRight) * glm::angleAxis(radians(roll), axes::kForward);
    }
    void angles(float &heading, float &pitch, float &roll) const {
        Vec3 f = forward();
        Vec3 u = up();
        pitch = -axes::pitchOf(f);
        heading = axes::headingOf(f);
        glm::quat base = glm::angleAxis(radians(heading), axes::kUp) * glm::angleAxis(radians(-pitch), axes::kRight);
        Vec3 u0 = base * axes::kUp;
        Vec3 r0 = base * axes::kRight;
        roll = degrees(std::atan2(dot(u, r0), dot(u, u0)));
    }

    // dx about the up of the screen, dy about its right, in degrees
    void turn(float dx, float dy) {
        q_ = glm::normalize(q_ * glm::angleAxis(radians(-dx), axes::kUp) * glm::angleAxis(radians(-dy), axes::kRight));
    }
    // in world units
    void pan(float right, float up) {
        target += this->right() * right + this->up() * up;
    }

    Vec3 forward() const {
        return q_ * axes::kForward;
    }
    Vec3 right() const {
        return q_ * axes::kRight;
    }
    Vec3 up() const {
        return q_ * axes::kUp;
    }
    Vec3 eye() const {
        return target - forward() * distance;
    }

    void apply(Camera &camera, float fovyDeg, float aspect, float znear, float zfar) const {
        camera.lookAt(eye(), target, up());
        camera.perspective(fovyDeg, aspect, znear, zfar);
    }

private:
    glm::quat q_;
};

} // namespace r4::render
