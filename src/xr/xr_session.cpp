// xr_session.cpp - the OpenXR session of r4::xr: instance, system, GL session, reference space, one swapchain for each eye.
#include "r4/xr/xr.h"

#include "r4/axes.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#endif

namespace r4::xr {

namespace {

const Eye kNoEye;
const Controller kNoController;

#if defined(_WIN32)

// the GL formats of the swapchains, in the order of preference: sRGB first, because the renderer writes the colors of the screen
enum GLFormat : int64_t {
    SRGB8_ALPHA8 = 0x8C43,
    RGBA8 = 0x8058
};

std::string resultText(XrInstance instance, XrResult r) {
    char text[XR_MAX_RESULT_STRING_SIZE] = {};
    if (instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(instance, r, text))) {
        std::snprintf(text, sizeof text, "XrResult %d", (int)r);
    }
    return text;
}

Mat4 poseMatrix(const XrPosef &p) {
    glm::quat q(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
    return translate({p.position.x, p.position.y, p.position.z}) * glm::mat4_cast(q);
}

void setButton(Button &b, bool down) {
    b.pressed = down && !b.down;
    b.released = !down && b.down;
    b.down = down;
}

// the actions of the controllers, each one on the two hands
enum class Action {
    GRIP_POSE,
    AIM_POSE,
    TRIGGER,
    SQUEEZE,
    STICK,
    PRIMARY,
    SECONDARY,
    STICK_CLICK,
    MENU,
    HAPTIC,
    COUNT
};
constexpr int kActions = (int)Action::COUNT;

struct ActionInfo {
    const char *name;
    const char *label;
    XrActionType type;
};
const ActionInfo kActionInfo[kActions] = {
    {"grip_pose", "Grip pose", XR_ACTION_TYPE_POSE_INPUT}, {"aim_pose", "Aim pose", XR_ACTION_TYPE_POSE_INPUT}, {"trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT}, {"squeeze", "Squeeze", XR_ACTION_TYPE_FLOAT_INPUT}, {"stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT}, {"primary", "Primary button", XR_ACTION_TYPE_BOOLEAN_INPUT}, {"secondary", "Secondary button", XR_ACTION_TYPE_BOOLEAN_INPUT}, {"stick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT}, {"menu", "Menu button", XR_ACTION_TYPE_BOOLEAN_INPUT}, {"haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT},
};

// a suggested binding: the action, the hands ("" both, "left", "right"), the path after /user/hand/<hand>
struct BindingInfo {
    Action action;
    const char *hand;
    const char *path;
};
struct ProfileInfo {
    const char *profile;
    std::vector<BindingInfo> bindings;
};

const std::vector<ProfileInfo> &profiles() {
    static const std::vector<ProfileInfo> list = {
        {"/interaction_profiles/khr/simple_controller",
         {
             {Action::GRIP_POSE, "", "/input/grip/pose"},
             {Action::AIM_POSE, "", "/input/aim/pose"},
             {Action::TRIGGER, "", "/input/select/click"},
             {Action::MENU, "", "/input/menu/click"},
             {Action::HAPTIC, "", "/output/haptic"},
         }},
        {"/interaction_profiles/oculus/touch_controller",
         {
             {Action::GRIP_POSE, "", "/input/grip/pose"},
             {Action::AIM_POSE, "", "/input/aim/pose"},
             {Action::TRIGGER, "", "/input/trigger/value"},
             {Action::SQUEEZE, "", "/input/squeeze/value"},
             {Action::STICK, "", "/input/thumbstick"},
             {Action::STICK_CLICK, "", "/input/thumbstick/click"},
             {Action::PRIMARY, "left", "/input/x/click"},
             {Action::SECONDARY, "left", "/input/y/click"},
             {Action::PRIMARY, "right", "/input/a/click"},
             {Action::SECONDARY, "right", "/input/b/click"},
             {Action::MENU, "left", "/input/menu/click"},
             {Action::HAPTIC, "", "/output/haptic"},
         }},
        {"/interaction_profiles/valve/index_controller",
         {
             {Action::GRIP_POSE, "", "/input/grip/pose"},
             {Action::AIM_POSE, "", "/input/aim/pose"},
             {Action::TRIGGER, "", "/input/trigger/value"},
             {Action::SQUEEZE, "", "/input/squeeze/value"},
             {Action::STICK, "", "/input/thumbstick"},
             {Action::STICK_CLICK, "", "/input/thumbstick/click"},
             {Action::PRIMARY, "", "/input/a/click"},
             {Action::SECONDARY, "", "/input/b/click"},
             {Action::HAPTIC, "", "/output/haptic"},
         }},
        {"/interaction_profiles/htc/vive_controller",
         {
             {Action::GRIP_POSE, "", "/input/grip/pose"},
             {Action::AIM_POSE, "", "/input/aim/pose"},
             {Action::TRIGGER, "", "/input/trigger/value"},
             {Action::SQUEEZE, "", "/input/squeeze/click"},
             {Action::STICK, "", "/input/trackpad"},
             {Action::PRIMARY, "", "/input/trackpad/click"},
             {Action::MENU, "", "/input/menu/click"},
             {Action::HAPTIC, "", "/output/haptic"},
         }},
        {"/interaction_profiles/microsoft/motion_controller",
         {
             {Action::GRIP_POSE, "", "/input/grip/pose"},
             {Action::AIM_POSE, "", "/input/aim/pose"},
             {Action::TRIGGER, "", "/input/trigger/value"},
             {Action::SQUEEZE, "", "/input/squeeze/click"},
             {Action::STICK, "", "/input/thumbstick"},
             {Action::STICK_CLICK, "", "/input/thumbstick/click"},
             {Action::PRIMARY, "", "/input/trackpad/click"},
             {Action::MENU, "", "/input/menu/click"},
             {Action::HAPTIC, "", "/output/haptic"},
         }},
    };
    return list;
}

// the asymmetric frustum of an eye, in the OpenGL clip space
Mat4 projectionOf(const XrFovf &fov, float n, float f) {
    return glm::frustum(n * std::tan(fov.angleLeft), n * std::tan(fov.angleRight), n * std::tan(fov.angleDown), n * std::tan(fov.angleUp), n, f);
}

#endif

} // namespace

#if defined(_WIN32)

struct Session::Impl {
    struct EyeChain {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        std::vector<gpu::RenderTargetHandle> images; // the images of the swapchain, wrapped
        Eye eye;
    };

    gpu::Device &device;
    Settings settings;
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    bool stage = false;
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    SessionState state = SessionState::IDLE;
    bool running = false;
    bool exit = false;
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    bool frameBegun = false;
    bool drawn = false;
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction actions[kActions] = {};
    XrPath handPaths[kHands] = {};
    XrSpace gripSpaces[kHands] = {};
    XrSpace aimSpaces[kHands] = {};
    Controller controllers[kHands];
    std::vector<XrView> views;
    std::vector<EyeChain> eyes;
    std::string runtime;
    std::string systemName;
    std::string error;

    explicit Impl(gpu::Device &d, const Settings &s) : device(d), settings(s) {
    }

    ~Impl() {
        for (EyeChain &e : eyes) {
            for (gpu::RenderTargetHandle h : e.images) {
                device.destroyRenderTarget(h);
            }
            device.destroyRenderTarget(e.eye.target);
            if (e.swapchain != XR_NULL_HANDLE) {
                xrDestroySwapchain(e.swapchain);
            }
        }
        for (int h = 0; h < kHands; h++) {
            if (gripSpaces[h] != XR_NULL_HANDLE) {
                xrDestroySpace(gripSpaces[h]);
            }
            if (aimSpaces[h] != XR_NULL_HANDLE) {
                xrDestroySpace(aimSpaces[h]);
            }
        }
        if (actionSet != XR_NULL_HANDLE) {
            xrDestroyActionSet(actionSet);
        }
        if (space != XR_NULL_HANDLE) {
            xrDestroySpace(space);
        }
        if (session != XR_NULL_HANDLE) {
            xrDestroySession(session);
        }
        if (instance != XR_NULL_HANDLE) {
            xrDestroyInstance(instance);
        }
    }

    bool fail(const char *what, XrResult r) {
        error = std::string(what) + ": " + resultText(instance, r);
        return false;
    }

    bool init() {
        uint32_t count = 0;
        XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
        if (XR_FAILED(r)) {
            return fail("no OpenXR runtime", r);
        }
        std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
        xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
        bool gl = std::any_of(extensions.begin(), extensions.end(), [](const XrExtensionProperties &e) {
            return std::strcmp(e.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0;
        });
        if (!gl) {
            error = "the OpenXR runtime has no OpenGL (" XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ")";
            return false;
        }
        const char *enabled[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
        XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
        std::snprintf(ici.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "%s", settings.appName.c_str());
        std::snprintf(ici.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "%s", "r4nkine");
        ici.applicationInfo.applicationVersion = 1;
        ici.applicationInfo.engineVersion = 1;
        ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        ici.enabledExtensionCount = 1;
        ici.enabledExtensionNames = enabled;
        r = xrCreateInstance(&ici, &instance);
        if (XR_FAILED(r)) {
            return fail("xrCreateInstance", r);
        }
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &ip))) {
            char v[64];
            std::snprintf(v, sizeof v, " %u.%u.%u", XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
            runtime = std::string(ip.runtimeName) + v;
        }

        XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        r = xrGetSystem(instance, &sgi, &system);
        if (r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            error = "no headset connected to " + runtime;
            return false;
        }
        if (XR_FAILED(r)) {
            return fail("xrGetSystem", r);
        }
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystemProperties(instance, system, &sp))) {
            systemName = sp.systemName;
        }

        uint32_t blendCount = 0;
        xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blendCount, nullptr);
        std::vector<XrEnvironmentBlendMode> blends(blendCount);
        xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blendCount, &blendCount, blends.data());
        if (!blends.empty()) {
            blend = blends[0];
        }

        // the runtime asks for this call before the session
        PFN_xrGetOpenGLGraphicsRequirementsKHR requirements = nullptr;
        r = xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction *>(&requirements));
        if (XR_FAILED(r) || !requirements) {
            return fail("xrGetOpenGLGraphicsRequirementsKHR", r);
        }
        XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
        r = requirements(instance, system, &req);
        if (XR_FAILED(r)) {
            return fail("xrGetOpenGLGraphicsRequirementsKHR", r);
        }

        XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
        binding.hDC = wglGetCurrentDC();
        binding.hGLRC = wglGetCurrentContext();
        if (!binding.hDC || !binding.hGLRC) {
            error = "no current WGL context";
            return false;
        }
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
        sci.next = &binding;
        sci.systemId = system;
        r = xrCreateSession(instance, &sci, &session);
        if (XR_FAILED(r)) {
            return fail("xrCreateSession", r);
        }

        uint32_t spaceCount = 0;
        xrEnumerateReferenceSpaces(session, 0, &spaceCount, nullptr);
        std::vector<XrReferenceSpaceType> spaces(spaceCount);
        xrEnumerateReferenceSpaces(session, spaceCount, &spaceCount, spaces.data());
        stage = std::find(spaces.begin(), spaces.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != spaces.end();
        XrReferenceSpaceCreateInfo rci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        rci.referenceSpaceType = stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
        rci.poseInReferenceSpace.orientation.w = 1;
        r = xrCreateReferenceSpace(session, &rci, &space);
        if (XR_FAILED(r)) {
            return fail("xrCreateReferenceSpace", r);
        }
        return createSwapchains() && createActions();
    }

    XrPath path(const std::string &text) {
        XrPath p = XR_NULL_PATH;
        xrStringToPath(instance, text.c_str(), &p);
        return p;
    }

    // the action set, the suggested bindings of each profile (a profile that the runtime refuses is left out), the spaces of the hands
    bool createActions() {
        XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::snprintf(si.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "%s", "controllers");
        std::snprintf(si.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "%s", "Controllers");
        XrResult r = xrCreateActionSet(instance, &si, &actionSet);
        if (XR_FAILED(r)) {
            return fail("xrCreateActionSet", r);
        }
        handPaths[(int)Hand::LEFT] = path("/user/hand/left");
        handPaths[(int)Hand::RIGHT] = path("/user/hand/right");
        for (int a = 0; a < kActions; a++) {
            XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
            std::snprintf(ai.actionName, XR_MAX_ACTION_NAME_SIZE, "%s", kActionInfo[a].name);
            std::snprintf(ai.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "%s", kActionInfo[a].label);
            ai.actionType = kActionInfo[a].type;
            ai.countSubactionPaths = kHands;
            ai.subactionPaths = handPaths;
            r = xrCreateAction(actionSet, &ai, &actions[a]);
            if (XR_FAILED(r)) {
                return fail("xrCreateAction", r);
            }
        }
        for (const ProfileInfo &p : profiles()) {
            std::vector<XrActionSuggestedBinding> bindings;
            for (const BindingInfo &b : p.bindings) {
                for (const char *hand : {"left", "right"}) {
                    if (b.hand[0] && std::strcmp(b.hand, hand) != 0) {
                        continue;
                    }
                    bindings.push_back({actions[(int)b.action], path(std::string("/user/hand/") + hand + b.path)});
                }
            }
            XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            sb.interactionProfile = path(p.profile);
            sb.countSuggestedBindings = (uint32_t)bindings.size();
            sb.suggestedBindings = bindings.data();
            xrSuggestInteractionProfileBindings(instance, &sb);
        }
        for (int h = 0; h < kHands; h++) {
            XrActionSpaceCreateInfo ci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            ci.subactionPath = handPaths[h];
            ci.poseInActionSpace.orientation.w = 1;
            ci.action = actions[(int)Action::GRIP_POSE];
            r = xrCreateActionSpace(session, &ci, &gripSpaces[h]);
            if (XR_FAILED(r)) {
                return fail("xrCreateActionSpace", r);
            }
            ci.action = actions[(int)Action::AIM_POSE];
            r = xrCreateActionSpace(session, &ci, &aimSpaces[h]);
            if (XR_FAILED(r)) {
                return fail("xrCreateActionSpace", r);
            }
        }
        XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        at.countActionSets = 1;
        at.actionSets = &actionSet;
        r = xrAttachSessionActionSets(session, &at);
        if (XR_FAILED(r)) {
            return fail("xrAttachSessionActionSets", r);
        }
        return true;
    }

    void updateProfiles() {
        for (int h = 0; h < kHands; h++) {
            XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
            controllers[h].profile.clear();
            if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(session, handPaths[h], &ps)) && ps.interactionProfile != XR_NULL_PATH) {
                char text[XR_MAX_PATH_LENGTH] = {};
                uint32_t n = 0;
                if (XR_SUCCEEDED(xrPathToString(instance, ps.interactionProfile, sizeof text, &n, text))) {
                    controllers[h].profile = text;
                }
            }
        }
    }

    float floatOf(Action a, int h) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = actions[(int)a];
        gi.subactionPath = handPaths[h];
        XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
        return XR_SUCCEEDED(xrGetActionStateFloat(session, &gi, &st)) && st.isActive ? st.currentState : 0.0f;
    }
    bool boolOf(Action a, int h) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = actions[(int)a];
        gi.subactionPath = handPaths[h];
        XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
        return XR_SUCCEEDED(xrGetActionStateBoolean(session, &gi, &st)) && st.isActive && st.currentState;
    }
    Vec2 vec2Of(Action a, int h) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = actions[(int)a];
        gi.subactionPath = handPaths[h];
        XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
        return XR_SUCCEEDED(xrGetActionStateVector2f(session, &gi, &st)) && st.isActive ? Vec2(st.currentState.x, st.currentState.y) : Vec2(0.0f);
    }
    bool locate(XrSpace s, XrTime time, const Mat4 &origin, Mat4 &world) {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const XrSpaceLocationFlags valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (XR_FAILED(xrLocateSpace(s, space, time, &loc)) || (loc.locationFlags & valid) != valid) {
            return false;
        }
        world = origin * poseMatrix(loc.pose);
        return true;
    }

    // the input of the frame; all at rest when the session does not have the focus
    void readInput(const Mat4 &origin) {
        bool focus = state == SessionState::FOCUSED;
        if (focus) {
            XrActiveActionSet active{actionSet, XR_NULL_PATH};
            XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
            si.countActiveActionSets = 1;
            si.activeActionSets = &active;
            focus = xrSyncActions(session, &si) == XR_SUCCESS;
        }
        for (int h = 0; h < kHands; h++) {
            Controller &c = controllers[h];
            c.trigger = focus ? floatOf(Action::TRIGGER, h) : 0.0f;
            c.squeeze = focus ? floatOf(Action::SQUEEZE, h) : 0.0f;
            c.stick = focus ? vec2Of(Action::STICK, h) : Vec2(0.0f);
            setButton(c.triggerButton, c.trigger > 0.5f);
            setButton(c.squeezeButton, c.squeeze > 0.5f);
            setButton(c.primary, focus && boolOf(Action::PRIMARY, h));
            setButton(c.secondary, focus && boolOf(Action::SECONDARY, h));
            setButton(c.stickClick, focus && boolOf(Action::STICK_CLICK, h));
            setButton(c.menu, focus && boolOf(Action::MENU, h));
            c.active = focus && locate(gripSpaces[h], frame.predictedDisplayTime, origin, c.grip);
            if (c.active && !locate(aimSpaces[h], frame.predictedDisplayTime, origin, c.aim)) {
                c.aim = c.grip;
            }
        }
    }

    void vibrate(int h, float amplitude, float seconds, float frequency) {
        if (state != SessionState::FOCUSED) {
            return;
        }
        XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
        hi.action = actions[(int)Action::HAPTIC];
        hi.subactionPath = handPaths[h];
        XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
        v.amplitude = std::clamp(amplitude, 0.0f, 1.0f);
        v.duration = seconds > 0 ? (XrDuration)(seconds * 1e9) : XR_MIN_HAPTIC_DURATION;
        v.frequency = frequency > 0 ? frequency : XR_FREQUENCY_UNSPECIFIED;
        xrApplyHapticFeedback(session, &hi, reinterpret_cast<const XrHapticBaseHeader *>(&v));
    }

    bool createSwapchains() {
        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
        std::vector<XrViewConfigurationView> configs(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        XrResult r = xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, configs.data());
        if (XR_FAILED(r) || viewCount == 0) {
            return fail("xrEnumerateViewConfigurationViews", r);
        }
        uint32_t formatCount = 0;
        xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
        std::vector<int64_t> formats(formatCount);
        xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());
        int64_t format = 0;
        for (int64_t wanted : {SRGB8_ALPHA8, RGBA8}) {
            if (!format && std::find(formats.begin(), formats.end(), wanted) != formats.end()) {
                format = wanted;
            }
        }
        if (!format) {
            error = "the OpenXR runtime has no RGBA8 swapchain";
            return false;
        }
        views.assign(viewCount, {XR_TYPE_VIEW});
        eyes.resize(viewCount);
        for (uint32_t i = 0; i < viewCount; i++) {
            const XrViewConfigurationView &c = configs[i];
            float k = settings.renderScale > 0 ? settings.renderScale : 1;
            int w = std::clamp((int)std::lround(c.recommendedImageRectWidth * k), 16, (int)c.maxImageRectWidth);
            int h = std::clamp((int)std::lround(c.recommendedImageRectHeight * k), 16, (int)c.maxImageRectHeight);
            EyeChain &e = eyes[i];
            XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            ci.format = format;
            ci.sampleCount = 1;
            ci.width = (uint32_t)w;
            ci.height = (uint32_t)h;
            ci.faceCount = 1;
            ci.arraySize = 1;
            ci.mipCount = 1;
            r = xrCreateSwapchain(session, &ci, &e.swapchain);
            if (XR_FAILED(r)) {
                return fail("xrCreateSwapchain", r);
            }
            uint32_t imageCount = 0;
            xrEnumerateSwapchainImages(e.swapchain, 0, &imageCount, nullptr);
            std::vector<XrSwapchainImageOpenGLKHR> images(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
            r = xrEnumerateSwapchainImages(e.swapchain, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data()));
            if (XR_FAILED(r)) {
                return fail("xrEnumerateSwapchainImages", r);
            }
            for (const XrSwapchainImageOpenGLKHR &image : images) {
                gpu::RenderTargetDesc d;
                d.width = w;
                d.height = h;
                d.depth = false;
                d.externalColor = image.image;
                gpu::RenderTargetHandle t = device.createRenderTarget(d);
                if (!t) {
                    error = "swapchain image: " + device.lastError();
                    return false;
                }
                e.images.push_back(t);
            }
            gpu::RenderTargetDesc d;
            d.width = w;
            d.height = h;
            d.samples = std::max(1, settings.samples);
            e.eye.target = device.createRenderTarget(d);
            if (!e.eye.target) {
                error = "eye target: " + device.lastError();
                return false;
            }
            e.eye.width = w;
            e.eye.height = h;
        }
        return true;
    }

    void setState(XrSessionState s) {
        switch (s) {
        case XR_SESSION_STATE_READY: {
            state = SessionState::READY;
            XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
            bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            running = XR_SUCCEEDED(xrBeginSession(session, &bi));
            break;
        }
        case XR_SESSION_STATE_SYNCHRONIZED:
            state = SessionState::SYNCHRONIZED;
            break;
        case XR_SESSION_STATE_VISIBLE:
            state = SessionState::VISIBLE;
            break;
        case XR_SESSION_STATE_FOCUSED:
            state = SessionState::FOCUSED;
            break;
        case XR_SESSION_STATE_STOPPING:
            state = SessionState::STOPPING;
            xrEndSession(session);
            running = false;
            break;
        case XR_SESSION_STATE_LOSS_PENDING:
            state = SessionState::LOSS_PENDING;
            exit = true;
            break;
        case XR_SESSION_STATE_EXITING:
            state = SessionState::EXITING;
            exit = true;
            break;
        default:
            state = SessionState::IDLE;
            break;
        }
    }

    bool pollEvents() {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(instance, &event) == XR_SUCCESS) {
            switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                setState(reinterpret_cast<XrEventDataSessionStateChanged *>(&event)->state);
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                updateProfiles();
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                exit = true;
                break;
            default:
                break;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        return !exit;
    }

    bool beginFrame(const Mat4 &origin, Mat4 &head) {
        drawn = false;
        frameBegun = false;
        if (!running) {
            return false;
        }
        frame = {XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
        if (XR_FAILED(xrWaitFrame(session, &wi, &frame))) {
            return false;
        }
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
        if (XR_FAILED(xrBeginFrame(session, &bi))) {
            return false;
        }
        frameBegun = true;
        readInput(origin);
        if (!frame.shouldRender) {
            return false;
        }
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
        li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        li.displayTime = frame.predictedDisplayTime;
        li.space = space;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        XrResult r = xrLocateViews(session, &li, &vs, (uint32_t)views.size(), &count, views.data());
        const XrViewStateFlags valid = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        if (XR_FAILED(r) || count != views.size() || (vs.viewStateFlags & valid) != valid) {
            return false;
        }
        Vec3 middle(0.0f);
        for (size_t i = 0; i < eyes.size(); i++) {
            Eye &e = eyes[i].eye;
            e.world = origin * poseMatrix(views[i].pose);
            e.camera.view = inverse(e.world);
            e.camera.position = Vec3(e.world[3]);
            e.camera.znear = settings.znear;
            e.camera.zfar = settings.zfar;
            e.camera.projection = projectionOf(views[i].fov, settings.znear, settings.zfar);
            middle += e.camera.position;
        }
        head = eyes[0].eye.world;
        head[3] = Vec4(middle / (float)eyes.size(), 1);
        drawn = true;
        return true;
    }

    void endFrame() {
        if (!frameBegun) {
            return;
        }
        frameBegun = false;
        std::vector<XrCompositionLayerProjectionView> layerViews;
        if (drawn) {
            for (size_t i = 0; i < eyes.size(); i++) {
                EyeChain &e = eyes[i];
                uint32_t index = 0;
                XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                if (XR_FAILED(xrAcquireSwapchainImage(e.swapchain, &ai, &index))) {
                    layerViews.clear();
                    break;
                }
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                bool ok = XR_SUCCEEDED(xrWaitSwapchainImage(e.swapchain, &wi)) && index < e.images.size();
                if (ok) {
                    device.copyRenderTarget(e.eye.target, e.images[index]);
                }
                XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(e.swapchain, &ri);
                if (!ok) {
                    layerViews.clear();
                    break;
                }
                XrCompositionLayerProjectionView v{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                v.pose = views[i].pose;
                v.fov = views[i].fov;
                v.subImage.swapchain = e.swapchain;
                v.subImage.imageRect.offset = {0, 0};
                v.subImage.imageRect.extent = {e.eye.width, e.eye.height};
                v.subImage.imageArrayIndex = 0;
                layerViews.push_back(v);
            }
        }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = space;
        layer.viewCount = (uint32_t)layerViews.size();
        layer.views = layerViews.data();
        const XrCompositionLayerBaseHeader *layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer)};
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
        ei.displayTime = frame.predictedDisplayTime;
        ei.environmentBlendMode = blend;
        ei.layerCount = layerViews.empty() ? 0 : 1;
        ei.layers = layers;
        xrEndFrame(session, &ei);
        drawn = false;
    }
};

std::unique_ptr<Session> Session::create(gpu::Device &device, const Settings &settings, std::string *error) {
    auto impl = std::make_unique<Impl>(device, settings);
    if (!impl->init()) {
        if (error) {
            *error = impl->error;
        }
        return nullptr;
    }
    return std::unique_ptr<Session>(new Session(std::move(impl)));
}

bool Session::pollEvents() {
    return impl_->pollEvents();
}
bool Session::running() const {
    return impl_->running;
}
bool Session::focused() const {
    return impl_->state == SessionState::FOCUSED;
}
SessionState Session::state() const {
    return impl_->state;
}
bool Session::beginFrame() {
    return impl_->beginFrame(origin_, head_);
}
int Session::eyeCount() const {
    return (int)impl_->eyes.size();
}
const Eye &Session::eye(int index) const {
    return index >= 0 && index < (int)impl_->eyes.size() ? impl_->eyes[(size_t)index].eye : kNoEye;
}
void Session::endFrame() {
    impl_->endFrame();
}
bool Session::floorLevel() const {
    return impl_->stage;
}
const Controller &Session::controller(Hand hand) const {
    int h = (int)hand;
    return h >= 0 && h < kHands ? impl_->controllers[h] : kNoController;
}
void Session::vibrate(Hand hand, float amplitude, float seconds, float frequency) {
    int h = (int)hand;
    if (h >= 0 && h < kHands) {
        impl_->vibrate(h, amplitude, seconds, frequency);
    }
}
const std::string &Session::runtimeName() const {
    return impl_->runtime;
}
const std::string &Session::systemName() const {
    return impl_->systemName;
}

#else

// the OpenGL binding of the other systems (Xlib, Wayland) is not done
struct Session::Impl {
    std::string empty;
};

std::unique_ptr<Session> Session::create(gpu::Device &, const Settings &, std::string *error) {
    if (error) {
        *error = "r4::xr: the OpenGL session of OpenXR is done only on Windows";
    }
    return nullptr;
}

bool Session::pollEvents() {
    return false;
}
bool Session::running() const {
    return false;
}
bool Session::focused() const {
    return false;
}
SessionState Session::state() const {
    return SessionState::IDLE;
}
bool Session::beginFrame() {
    return false;
}
int Session::eyeCount() const {
    return 0;
}
const Eye &Session::eye(int) const {
    return kNoEye;
}
void Session::endFrame() {
}
bool Session::floorLevel() const {
    return false;
}
const Controller &Session::controller(Hand) const {
    return kNoController;
}
void Session::vibrate(Hand, float, float, float) {
}
const std::string &Session::runtimeName() const {
    return impl_->empty;
}
const std::string &Session::systemName() const {
    return impl_->empty;
}

#endif

Session::Session(std::unique_ptr<Impl> impl) : impl_(std::move(impl)), origin_(axes::fromYUpMatrix()) {
}

Session::~Session() = default;

} // namespace r4::xr
