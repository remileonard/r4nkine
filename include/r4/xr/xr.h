// r4/xr/xr.h - virtual reality with OpenXR: a session on the GL context of the device, the two eyes drawn by the renderer and given to the headset.
//
//   auto xr = xr::Session::create(device, {}, &error);
//   while (xr->pollEvents()) {
//       if (xr->beginFrame()) {
//           for (int i = 0; i < xr->eyeCount(); i++) {
//               renderer.setTarget(xr->eye(i).target);
//               renderer.render(root, xr->eye(i).camera, 0, 0, xr->eye(i).width, xr->eye(i).height);
//           }
//           renderer.setTarget({});
//       }
//       xr->endFrame();
//   }
//
// The OpenGL binding is done on Windows (WGL). On the other systems, create() gives an error.
#pragma once

#include "r4/gpu/device.h"
#include "r4/math.h"
#include "r4/render/renderer.h"

#include <memory>
#include <string>

namespace r4::xr {

struct Settings {
    std::string appName = "r4nkine";
    // the MSAA of the targets of the eyes, resolved into the images of the headset
    int samples = 4;
    // multiplies the size that the runtime recommends for each eye
    float renderScale = 1;
    float znear = 0.05f;
    float zfar = 1000;
    // the LOCAL space even when the runtime has a STAGE: its origin is the head when the session starts, as in a cockpit
    bool seated = false;
};

// a flat screen in front of the eyes, as a quad layer of OpenXR: the menus of a game, a film
struct Screen {
    // its color; sourceWidth x sourceHeight is the size of the target, and of the swapchain of the screen
    gpu::RenderTargetHandle source;
    int sourceWidth = 0;
    int sourceHeight = 0;
    // its center and its axes in the tracking space (OpenXR: Y up, -Z forward); the screen faces +Z
    Mat4 pose;
    // in meters
    float width = 2;
    float height = 1.5f;
};

// an eye of the frame: draw its camera into its target, from (0, 0) to (width, height)
struct Eye {
    render::Camera camera;
    gpu::RenderTargetHandle target;
    int width = 0;
    int height = 0;
    Mat4 world; // the eye in the world: X right, Y up, -Z the line of sight
};

enum class Hand {
    LEFT,
    RIGHT
};
constexpr int kHands = 2;

struct Button {
    bool down = false;
    bool pressed = false;  // down since this frame
    bool released = false; // up since this frame
};

// a controller at the last beginFrame. The bindings are suggested for the profiles of Khronos (simple), Oculus Touch, Valve Index, HTC Vive,
// Windows Mixed Reality and Pico 4. A button that the controller does not have stays up.
struct Controller {
    // the runtime gives its position: the poses below are valid
    bool active = false;
    // in the world, with the axes of OpenXR: X right, Y up, -Z forward
    Mat4 grip;            // the palm, about the handle
    Mat4 aim;             // the ray that points, from the tip of the controller
    float trigger = 0;    // 0 to 1
    float squeeze = 0;    // the grip button, 0 to 1
    Vec2 stick;           // the thumbstick (or trackpad of the Vive), -1 to 1, +Y forward
    Button triggerButton; // trigger beyond 0.5
    Button squeezeButton; // squeeze beyond 0.5
    Button primary;       // A on the right hand, X on the left (the trackpad click of the Vive)
    Button secondary;     // B on the right hand, Y on the left
    Button stickClick;
    Button menu;
    std::string profile; // the interaction profile that the runtime uses, as "/interaction_profiles/oculus/touch_controller"
};

enum class SessionState {
    IDLE,
    READY,
    SYNCHRONIZED,
    VISIBLE,
    FOCUSED,
    STOPPING,
    LOSS_PENDING,
    EXITING
};

class Session {
public:
    // in the GL context current on this thread, the one of device; null on error (no runtime, no headset), the reason in error
    static std::unique_ptr<Session> create(gpu::Device &device, const Settings &settings, std::string *error = nullptr);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    // the events of the runtime; false when the session is finished and the application must stop its VR
    bool pollEvents();
    // frames are expected: call beginFrame and endFrame in each loop
    bool running() const;
    // the headset shows the application and gives it the input
    bool focused() const;
    SessionState state() const;

    // waits until the time of the next frame and starts it; true when the eyes must be drawn. Always call endFrame after it.
    bool beginFrame();
    int eyeCount() const;
    const Eye &eye(int index) const;
    // gives the targets of the eyes to the headset, and the screen of showScreen over them. With eyes false, the screen only.
    void endFrame(bool eyes = true);
    // shows this screen in the frame that beginFrame started; endFrame copies its source
    void showScreen(const Screen &screen);

    // the input is read in beginFrame, when the session is focused
    const Controller &controller(Hand hand) const;
    // a pulse of the motor of the controller: amplitude 0 to 1; frequency 0 lets the runtime choose
    void vibrate(Hand hand, float amplitude, float seconds, float frequency = 0);

    // the tracking space (OpenXR: Y up, meters) in the world of R4nkine. The default turns it to Z up, its floor at the origin.
    // To walk or to fly, move this matrix.
    void setOrigin(const Mat4 &tracking) {
        origin_ = tracking;
    }
    const Mat4 &origin() const {
        return origin_;
    }
    // the head in the world, between the eyes, at the last beginFrame
    const Mat4 &head() const {
        return head_;
    }
    // the tracking space has its origin on the floor (STAGE); else at the first position of the head (LOCAL, or Settings::seated)
    bool floorLevel() const;
    const std::string &runtimeName() const;
    const std::string &systemName() const;

    struct Impl;

private:
    explicit Session(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    Mat4 origin_;
    Mat4 head_;
};

} // namespace r4::xr
