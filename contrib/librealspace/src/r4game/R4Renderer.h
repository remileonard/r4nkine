//
//  R4Renderer.h
//  libRealSpace
//
//  The 3D of the game on R4nkine: the terrain and the objects. The game keeps its matrices, its cameras and its GL calls for the rest.
//  The models are kept in eye space (the GL modelview of each call) and drawn in one pass of the renderer before the next GL draw of the game.
//
#pragma once

#include "engine/SCRenderer.h"

#include "r4/render/renderer.h"
#include "r4/scene/node.h"

#include <memory>
#include <unordered_map>
#include <vector>

class R4Renderer : public SCRenderer {
public:
    R4Renderer();
    ~R4Renderer() override;

    // SCRenderer::instance() when the renderer of the game is a R4Renderer, else null
    static R4Renderer *current();

    // draws the terrain and the models kept since the last flush
    void flush();
    // the sky of the game in GL, the color and depth cleared, fog set as renderWorldSolid
    void drawSky(RSArea *area);
    // a graph in world space (libRealSpace axes) with a camera of the game, in the frame of the screen
    void drawGraph(r4::scene::Node &root, const r4::render::Camera &camera, int x, int y, int width, int height);
    // the node of an entity, made at its first use
    r4::scene::NodePtr modelOf(RSEntity *object);
    // the terrain and the runways of the area, made when the area changes; null without them
    void useArea(RSArea *area);
    const r4::scene::NodePtr &terrain() const {
        return terrain_;
    }
    const r4::scene::NodePtr &overlay() const {
        return overlay_;
    }
    bool active() const {
        return renderer_ != nullptr;
    }
    // frees the GPU objects, before the device of the screen goes
    void release();

    using SCRenderer::drawLine;
    using SCRenderer::drawModel;

    void init(int width, int height) override;
    void clear(void) override;
    void setObjectLight(int ambient_weight, int light_weight, int c0, int c1, int c2) override;
    void drawModel(RSEntity *object, size_t lodLevel) override;
    void renderWorldSolid(RSArea *area, int LOD, int verticesPerBlock) override;

    // the GL draws of the game: the kept models first
    void drawParticle(Vector3D pos, float alpha) override;
    void drawLine(Vector3D start, Vector3D end, Vector3D color, Vector3D orientation) override;
    void drawLine(Vector3D start, Vector3D end, Vector3D color, Vector3D orientation, Vector3D position) override;
    void drawLine(Vector3D start, Vector3D end, Vector3D color) override;
    void drawPoint(Vector3D point, Vector3D color, Vector3D pos, Vector3D orientation) override;
    void drawSprite(Vector3D pos, Texture *tex, float zoom) override;
    void drawBillboard(Vector3D pos, Texture *tex, float size, float alpha = 1.0f) override;
    void drawTriangles(const EffectVertex *vertices, size_t count, unsigned int texture, BlendMode blend, bool depthWrite = false) override;
    void drawTracer(Vector3D pos, Vector3D velocityDir, float length, float pixelWidth, Vector3D color, float alpha) override;
    void displayModel(RSEntity *object, size_t lodLevel) override;
    void drawTexturedQuad(Vector3D pos, Vector3D orientation, std::vector<Vector3D> quad, Texture *tex) override;
    void renderEllipsoid(float cx, float cy, float cz, float rx, float ry, float rz, int rings, int slices, float r, float g, float b, float baseAlpha) override;
    void renderClouds(RSArea *area) override;
    void renderBlock(RSArea *area, int LOD, int i, bool renderTexture, const std::unordered_set<int> *skipRight = nullptr, const std::unordered_set<int> *skipBottom = nullptr) override;
    void renderWorldByID(RSArea *area, int LOD, int verticesPerBlock, int blockId) override;
    void renderMapOverlay(RSArea *area) override;
    void renderLineCube(Vector3D position, int32_t size) override;
    void renderBBox(Vector3D position, Point3D min, Point3D max) override;
    void renderWorldToTexture(RSArea *area) override;
    void initRenderToTexture() override;
    void getRenderToTexture() override;

private:
    // the GL state that a pass of the renderer takes from the game
    struct View {
        float projection[16];
        int viewport[4];
        bool fog;
        float fogStart;
        float fogEnd;
        float fogColor[4];
    };
    struct Item {
        r4::scene::NodePtr node;
        r4::Mat4 eye;
    };
    static View currentView();
    static bool sameView(const View &a, const View &b);
    void queue(const r4::scene::NodePtr &node);
    void setEnvironment(bool fog, float fogStart, float fogEnd, const float fogColor[4], r4::Vec3 lightDirection);

    std::unique_ptr<r4::render::Renderer> renderer_;
    std::unordered_map<RSEntity *, r4::scene::NodePtr> models_;
    RSArea *area_ = nullptr;
    r4::scene::NodePtr terrain_;
    r4::scene::NodePtr overlay_;
    std::vector<Item> items_;
    View view_{};
    r4::Vec3 lightEye_{0, -1, 0};
    std::shared_ptr<r4::scene::Group> root_;
    std::vector<std::shared_ptr<r4::scene::Group>> places_;
};
