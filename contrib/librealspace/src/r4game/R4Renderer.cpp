//
//  R4Renderer.cpp
//  libRealSpace
//
#include "R4Renderer.h"
#include "R4Screen.h"

#include "R4Bridge.h"
#include "R4World.h"

#include "realspace/RSArea.h"
#include "realspace/RSEntity.h"

#include <glm/gtc/type_ptr.hpp>

#include <SDL.h>

#include <cstring>

namespace {

constexpr float kNear = 1.5f;

r4::Mat4 glMatrix(GLenum which) {
    float m[16];
    glGetFloatv(which, m);
    return glm::make_mat4(m);
}

// the state SCRenderer::drawModel leaves to the game
void leaveModelState() {
    glPolygonMode(GL_FRONT, GL_FILL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    glDepthFunc(GL_LESS);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
}

} // namespace

R4Renderer::R4Renderer() = default;

R4Renderer::~R4Renderer() {
    release();
}

R4Renderer *R4Renderer::current() {
    if (!SCRenderer::hasInstance()) {
        return nullptr;
    }
    return dynamic_cast<R4Renderer *>(&SCRenderer::instance());
}

void R4Renderer::release() {
    items_.clear();
    places_.clear();
    root_.reset();
    models_.clear();
    terrain_.reset();
    overlay_.reset();
    area_ = nullptr;
    R4Screen *screen = R4Screen::current();
    if (renderer_ && screen) {
        screen->device().beginHostInterop();
        renderer_.reset();
        screen->device().endHostInterop();
    }
    renderer_.reset();
}

void R4Renderer::init(int width, int height) {
    SCRenderer::init(width, height);
    R4Screen *screen = R4Screen::current();
    if (!screen || !screen->hasDevice()) {
        std::printf("[R4Renderer] no R4Screen: the 3D stays on SCRenderer\n");
        return;
    }
    screen->device().beginHostInterop();
    renderer_ = std::make_unique<r4::render::Renderer>(screen->device());
    screen->device().endHostInterop();
    if (!renderer_->error().empty()) {
        std::printf("[R4Renderer] %s\n", renderer_->error().c_str());
    }
    root_ = r4::scene::make<r4::scene::Group>();
    root_->setName("frame (eye space)");
}

R4Renderer::View R4Renderer::currentView() {
    View v{};
    glGetFloatv(GL_PROJECTION_MATRIX, v.projection);
    glGetIntegerv(GL_VIEWPORT, v.viewport);
    v.fog = glIsEnabled(GL_FOG) == GL_TRUE;
    glGetFloatv(GL_FOG_START, &v.fogStart);
    glGetFloatv(GL_FOG_END, &v.fogEnd);
    glGetFloatv(GL_FOG_COLOR, v.fogColor);
    return v;
}

bool R4Renderer::sameView(const View &a, const View &b) {
    return std::memcmp(&a, &b, sizeof(View)) == 0;
}

void R4Renderer::queue(const r4::scene::NodePtr &node) {
    View v = currentView();
    if (!items_.empty() && !sameView(v, view_)) {
        flush();
    }
    if (items_.empty()) {
        view_ = v;
        r4::Mat4 view = glm::make_mat4(camera.getViewMatrix()->ToGL());
        r4::Vec3 towards{object_light_direction.x, object_light_direction.y, object_light_direction.z};
        lightEye_ = r4::normalize(r4::transformVector(view, -towards));
    }
    items_.push_back({node, glMatrix(GL_MODELVIEW_MATRIX)});
}

void R4Renderer::flush() {
    R4Screen *screen = R4Screen::current();
    if (items_.empty() || !renderer_ || !screen) {
        items_.clear();
        return;
    }
    r4::render::Camera cam;
    cam.position = r4::Vec3(0.0f);
    cam.view = r4::Mat4(1.0f);
    cam.projection = glm::make_mat4(view_.projection);
    cam.znear = kNear;
    cam.zfar = (float)max_view_distance * 1.2f;

    r4::render::Environment &env = renderer_->environment();
    env.fog = view_.fog;
    env.fogStart = view_.fogStart;
    env.fogEnd = view_.fogEnd;
    env.fogColor = {view_.fogColor[0], view_.fogColor[1], view_.fogColor[2]};
    env.lightDirection = lightEye_;
    env.time = (float)SDL_GetTicks() / 1000.0f;
    r4::render::RenderOptions &options = renderer_->options();
    options.clear = false;
    options.textures = show_textured;
    options.rampGouraud = object_gouraud;

    root_->removeAllChildren();
    while (places_.size() < items_.size()) {
        places_.push_back(r4::scene::make<r4::scene::Group>());
    }
    for (size_t i = 0; i < items_.size(); i++) {
        const std::shared_ptr<r4::scene::Group> &place = places_[i];
        place->removeAllChildren();
        place->addChild(items_[i].node);
        place->setMatrix(items_[i].eye);
        root_->addChild(place);
    }
    r4::gpu::Device &device = screen->device();
    device.beginHostInterop();
    renderer_->setTarget(screen->frameTarget());
    renderer_->render(*root_, cam, view_.viewport[0], view_.viewport[1], view_.viewport[2], view_.viewport[3]);
    device.endHostInterop();
    for (size_t i = 0; i < items_.size(); i++) {
        places_[i]->removeAllChildren();
    }
    root_->removeAllChildren();
    items_.clear();
}

r4::scene::NodePtr R4Renderer::modelOf(RSEntity *object) {
    auto it = models_.find(object);
    if (it != models_.end()) {
        return it->second;
    }
    r4::scene::NodePtr node;
    r4bridge::Lighting lighting;
    lighting.ambient = object_light_ambient;
    lighting.light = object_light_share;
    auto model = r4bridge::toModel(object, palette, terrain_ranges, lighting, r4bridge::Axes::REAL_SPACE);
    if (model) {
        R4Screen *screen = R4Screen::current();
        screen->device().beginHostInterop();
        node = renderer_->upload(*model);
        screen->device().endHostInterop();
    }
    models_.emplace(object, node);
    return node;
}

void R4Renderer::useArea(RSArea *area) {
    if (area == area_) {
        return;
    }
    flush();
    area_ = area;
    terrain_.reset();
    overlay_.reset();
    // a new mission can free its entities and make others at the same addresses
    models_.clear();
    if (!area) {
        return;
    }
    r4bridge::TerrainLight light;
    light.ranges = &terrain_ranges;
    light.lighting.ambient = object_light_ambient;
    light.lighting.light = object_light_share;
    auto terrain = r4bridge::terrainModel(area, palette, {0, 40000, 100000, 1e30f}, light);
    auto overlay = r4bridge::overlayModel(area, palette);
    R4Screen *screen = R4Screen::current();
    screen->device().beginHostInterop();
    if (terrain) {
        terrain_ = renderer_->upload(*terrain);
    }
    if (overlay) {
        overlay_ = renderer_->upload(*overlay);
    }
    screen->device().endHostInterop();
}

void R4Renderer::clear(void) {
    flush();
    SCRenderer::clear();
}

void R4Renderer::setObjectLight(int ambient_weight, int light_weight, int c0, int c1, int c2) {
    const float ambient = object_light_ambient;
    const float share = object_light_share;
    SCRenderer::setObjectLight(ambient_weight, light_weight, c0, c1, c2);
    if (ambient != object_light_ambient || share != object_light_share) {
        flush();
        models_.clear();
    }
}

void R4Renderer::drawModel(RSEntity *object, size_t lodLevel) {
    if (!renderer_) {
        SCRenderer::drawModel(object, lodLevel);
        return;
    }
    if (!initialized || !object || object->vertices.empty()) {
        return;
    }
    r4::scene::NodePtr node = modelOf(object);
    if (node) {
        queue(node);
    }
    leaveModelState();
}

void R4Renderer::renderWorldSolid(RSArea *area, int LOD, int verticesPerBlock) {
    if (!renderer_) {
        SCRenderer::renderWorldSolid(area, LOD, verticesPerBlock);
        return;
    }
    flush();
    useArea(area);
    GLfloat fogColor[4] = {0.89f, 0.89f, 0.98f, 1.0f};
    if (this->show_fog) {
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fogColor);
        glFogf(GL_FOG_START, this->max_view_distance * 0.40f);
        glFogf(GL_FOG_END, this->max_view_distance * 0.93f);
        glHint(GL_FOG_HINT, GL_DONT_CARE);
        glEnable(GL_FOG);
    } else {
        glDisable(GL_FOG);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    this->renderWorldSkyAndGround();
    if (this->show_clouds) {
        SCRenderer::renderClouds(area);
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    if (terrain_) {
        queue(terrain_);
    }
    if (overlay_) {
        queue(overlay_);
    }
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glFrontFace(GL_CCW);
}

void R4Renderer::drawParticle(Vector3D pos, float alpha) {
    flush();
    SCRenderer::drawParticle(pos, alpha);
}

void R4Renderer::drawLine(Vector3D start, Vector3D end, Vector3D color, Vector3D orientation) {
    flush();
    SCRenderer::drawLine(start, end, color, orientation);
}

void R4Renderer::drawLine(Vector3D start, Vector3D end, Vector3D color, Vector3D orientation, Vector3D position) {
    flush();
    SCRenderer::drawLine(start, end, color, orientation, position);
}

void R4Renderer::drawLine(Vector3D start, Vector3D end, Vector3D color) {
    flush();
    SCRenderer::drawLine(start, end, color);
}

void R4Renderer::drawPoint(Vector3D point, Vector3D color, Vector3D pos, Vector3D orientation) {
    flush();
    SCRenderer::drawPoint(point, color, pos, orientation);
}

void R4Renderer::drawSprite(Vector3D pos, Texture *tex, float zoom) {
    flush();
    SCRenderer::drawSprite(pos, tex, zoom);
}

void R4Renderer::drawBillboard(Vector3D pos, Texture *tex, float size, float alpha) {
    flush();
    SCRenderer::drawBillboard(pos, tex, size, alpha);
}

void R4Renderer::drawTriangles(const EffectVertex *vertices, size_t count, unsigned int texture, BlendMode blend, bool depthWrite) {
    flush();
    SCRenderer::drawTriangles(vertices, count, texture, blend, depthWrite);
}

void R4Renderer::drawTracer(Vector3D pos, Vector3D velocityDir, float length, float pixelWidth, Vector3D color, float alpha) {
    flush();
    SCRenderer::drawTracer(pos, velocityDir, length, pixelWidth, color, alpha);
}

void R4Renderer::displayModel(RSEntity *object, size_t lodLevel) {
    flush();
    SCRenderer::displayModel(object, lodLevel);
}

void R4Renderer::drawTexturedQuad(Vector3D pos, Vector3D orientation, std::vector<Vector3D> quad, Texture *tex) {
    flush();
    SCRenderer::drawTexturedQuad(pos, orientation, quad, tex);
}

void R4Renderer::renderEllipsoid(float cx, float cy, float cz, float rx, float ry, float rz, int rings, int slices, float r, float g, float b, float baseAlpha) {
    flush();
    SCRenderer::renderEllipsoid(cx, cy, cz, rx, ry, rz, rings, slices, r, g, b, baseAlpha);
}

void R4Renderer::renderClouds(RSArea *area) {
    flush();
    SCRenderer::renderClouds(area);
}

void R4Renderer::renderBlock(RSArea *area, int LOD, int i, bool renderTexture, const std::unordered_set<int> *skipRight, const std::unordered_set<int> *skipBottom) {
    flush();
    SCRenderer::renderBlock(area, LOD, i, renderTexture, skipRight, skipBottom);
}

void R4Renderer::renderWorldByID(RSArea *area, int LOD, int verticesPerBlock, int blockId) {
    flush();
    SCRenderer::renderWorldByID(area, LOD, verticesPerBlock, blockId);
}

void R4Renderer::renderMapOverlay(RSArea *area) {
    flush();
    SCRenderer::renderMapOverlay(area);
}

void R4Renderer::renderLineCube(Vector3D position, int32_t size) {
    flush();
    SCRenderer::renderLineCube(position, size);
}

void R4Renderer::renderBBox(Vector3D position, Point3D min, Point3D max) {
    flush();
    SCRenderer::renderBBox(position, min, max);
}

void R4Renderer::renderWorldToTexture(RSArea *area) {
    flush();
    SCRenderer::renderWorldToTexture(area);
}

void R4Renderer::initRenderToTexture() {
    flush();
    SCRenderer::initRenderToTexture();
}

void R4Renderer::getRenderToTexture() {
    flush();
    SCRenderer::getRenderToTexture();
}
