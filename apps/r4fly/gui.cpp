// gui.cpp - the panels of r4fly, in the look of the GUI of perfly (r4::ui):
// on the left the settings (camera, light, environment, rendering, culling,
// optimisations), on the right the scene graph, the selected node and the
// performances.
#include "app.h"

#include <algorithm>
#include <cstdio>

namespace r4fly {

namespace {

constexpr float kLeftW = 290, kRightW = 360, kPerfH = 400;

const char *typeName(scene::NodeType t) {
    switch (t) {
    case scene::NodeType::Group: return "Group";
    case scene::NodeType::Geode: return "Geode";
    case scene::NodeType::Switch: return "Switch";
    case scene::NodeType::LOD: return "LOD";
    case scene::NodeType::Billboard: return "Billboard";
    case scene::NodeType::Instancer: return "Instancer";
    }
    return "?";
}

const char *typeTag(scene::NodeType t) {
    switch (t) {
    case scene::NodeType::Group: return "GRP";
    case scene::NodeType::Geode: return "GEO";
    case scene::NodeType::Switch: return "SW";
    case scene::NodeType::LOD: return "LOD";
    case scene::NodeType::Billboard: return "BB";
    case scene::NodeType::Instancer: return "INST";
    }
    return "?";
}

ui::Color typeColor(scene::NodeType t) {
    switch (t) {
    case scene::NodeType::Geode: return ui::Color::rgb(0.55f, 0.85f, 0.55f);
    case scene::NodeType::Switch: return ui::Color::rgb(0.95f, 0.75f, 0.35f);
    case scene::NodeType::LOD: return ui::Color::rgb(0.5f, 0.75f, 1.0f);
    case scene::NodeType::Billboard: return ui::Color::rgb(0.9f, 0.6f, 0.9f);
    case scene::NodeType::Instancer: return ui::Color::rgb(1.0f, 0.55f, 0.5f);
    default: return ui::Color::rgb(0.85f, 0.85f, 0.88f);
    }
}

std::string fmt(const char *f, double a, double b = 0, double c = 0) {
    char buf[128];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

float average(const std::deque<float> &v, size_t last) {
    if (v.empty())
        return 0;
    size_t n = std::min(last, v.size());
    float s = 0;
    for (size_t i = v.size() - n; i < v.size(); i++)
        s += v[i];
    return s / n;
}

std::string baseName(const std::string &p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// --- the settings -----------------------------------------------------------------

void settingsPanel(App &app, ui::Context &g, float height) {
    g.beginPanel("settings", {0, 0, kLeftW, height}, "r4fly");
    Trackball &b = app.ball;
    render::RenderOptions &o = app.renderer->options();
    render::Environment &env = app.renderer->environment();
    Vec3 e = b.eye();
    g.message(fmt("X%.1f Y%.1f Z%.1f", e.x, e.y, e.z).append(fmt("  H %.0f P %.0f", b.heading, b.pitch)).c_str());

    g.columns(2);
    if (g.button("Quit"))
        app.quit = true;
    if (g.button("GUI Off (F1)"))
        app.showGui = false;
    if (g.button("Frame All (R)"))
        app.resetView();
    if (g.button("Snap Screen"))
        app.snapRequest = "r4fly.ppm";
    g.toggle("Scene Graph", app.showGraph);
    g.toggle("Stats", app.showPerf);
    g.columns(1);

    if (g.section("Camera", app.openCamera)) {
        g.slider("Field of View", b.fov, 10, 120, "%.1f");
        g.slider("Heading", b.heading, -180, 180, "%.1f");
        g.slider("Pitch", b.pitch, -89, 89, "%.1f");
        g.slider("Distance", b.distance, b.radius * 0.01f, b.radius * 50, "%.3g", true);
        g.toggle("Automatic Clip Planes", b.autoClip);
        g.slider("Near Clip", b.znear, b.radius * 1e-4f, b.radius * 10, "%.4g", true, !b.autoClip);
        g.slider("Far Clip", b.zfar, b.radius * 0.1f, b.radius * 1000, "%.4g", true, !b.autoClip);
        g.columns(4);
        if (g.button("Front")) { b.heading = 0; b.pitch = 0; }
        if (g.button("Side")) { b.heading = 90; b.pitch = 0; }
        if (g.button("Top")) { b.heading = 0; b.pitch = 89; }
        if (g.button("3/4")) { b.heading = 30; b.pitch = 25; }
        g.columns(1);
        if (g.button("Recenter"))
            b.pan = {};
    }
    if (g.section("Lighting", app.openLight)) {
        g.columns(2);
        g.toggle("Lighting", o.lighting);
        g.toggle("Headlight", app.headlight);
        g.columns(1);
        g.slider("Sun Azimuth", app.lightAzimuth, -180, 180, "%.0f", false, !app.headlight);
        g.slider("Sun Elevation", app.lightElevation, -89, 89, "%.0f", false, !app.headlight);
        g.color("Light Color", env.lightColor);
        g.color("Ambient", env.ambient);
    }
    if (g.section("Environment", app.openEnv)) {
        Vec3 bg{env.clearColor.x, env.clearColor.y, env.clearColor.z};
        if (g.color("Background", bg))
            env.clearColor = {bg.x, bg.y, bg.z, 1};
        int fog = env.fog ? 1 : 0;
        if (g.menu("Fog", fog, {"Off", "Linear"}))
            env.fog = fog != 0;
        if (env.fog) {
            float r = app.ball.radius;
            g.slider("Near Fog", env.fogStart, r * 0.01f, r * 50, "%.3g", true);
            g.slider("Far Fog", env.fogEnd, r * 0.01f, r * 100, "%.3g", true);
            g.color("Fog Color", env.fogColor);
            if (g.button("Fog Color = Background"))
                env.fogColor = bg;
        }
    }
    if (g.section("Rendering", app.openRender)) {
        int style = app.renderer->wireframe() ? 1 : 0;
        if (g.menu("Style", style, {"Filled", "Lines"}))
            app.renderer->setWireframe(style == 1);
        g.toggle("Textures", o.textures);
        // the state bits of the camera (flight: 1 body, 2 gear, 4 far,
        // 8 shadow, the high byte the weapons)
        g.bits("State Mask", app.camera.mask, 16);
        g.columns(2);
        if (g.button("All States"))
            app.camera.mask = ~0u;
        if (g.button("No State"))
            app.camera.mask = 0;
        g.columns(1);
    }
    if (g.section("Culling", app.openCull)) {
        int mode = o.frustumCulling ? 1 : 0;
        if (g.menu("Cull Mode", mode, {"None", "View Frustum"}))
            o.frustumCulling = mode == 1;
        g.toggle("Freeze Culling", o.freezeCulling);
        g.slider("LOD Scale", o.lodScale, 0.05f, 20, "%.2f", true);
    }
    if (g.section("Optimisations", app.openOptim)) {
        g.toggle("Sort by State", o.sortByState);
        g.toggle("Instancing", o.instancing);
        bool vs = app.vsync;
        if (g.toggle("Vertical Sync", vs)) {
            app.vsync = vs;
            SDL_GL_SetSwapInterval(vs ? 1 : 0);
        }
    }
    g.space(4);
    for (const LoadedFile &f : app.files)
        g.message(fmt("%.0f tris, %.0f lines, %.0f points", (double)f.triangles, (double)f.lines, (double)f.points)
                      .insert(0, baseName(f.path) + ": ")
                      .c_str());
    if (app.forest)
        g.message(fmt("forest: %.0f instances, %.0f levels", (double)app.forest->instances().size(),
                      (double)app.forest->levels().size())
                      .c_str());
    g.endPanel();
}

// --- the scene graph ------------------------------------------------------------

void nodeTree(App &app, ui::Context &g, scene::Node &n, const Mat4 &parent, int depth) {
    Mat4 world = parent * n.matrix();
    if (app.selected == &n)
        app.selectedWorld = world;
    auto it = app.openNodes.find(&n);
    if (it == app.openNodes.end())
        it = app.openNodes.emplace(&n, depth < 2).first;
    bool open = it->second;
    bool visible = app.hiddenMasks.count(&n) == 0;
    std::string label = n.name().empty() ? std::string("(") + typeName(n.type()) + ")" : n.name();
    if (n.type() == scene::NodeType::Instancer)
        label += fmt("  [%.0f]", (double)static_cast<scene::Instancer &>(n).instances().size());
    else if (n.type() == scene::NodeType::Geode || n.type() == scene::NodeType::Billboard)
        label += fmt("  [%.0f]", (double)static_cast<scene::Geode &>(n).drawables().size());
    ui::Context::TreeRow row;
    row.id = &n;
    row.depth = depth;
    row.label = label.c_str();
    row.tag = typeColor(n.type());
    row.tagText = typeTag(n.type());
    row.hasChildren = !n.children().empty();
    row.selected = app.selected == &n;
    bool wasVisible = visible;
    if (g.treeRow(row, open, visible))
        app.selected = app.selected == &n ? nullptr : &n;
    it->second = open;
    if (visible != wasVisible) {
        // hidden: the mask of the node set to 0, the old one kept
        if (!visible) {
            app.hiddenMasks[&n] = n.mask();
            n.setMask(0);
        } else {
            n.setMask(app.hiddenMasks[&n]);
            app.hiddenMasks.erase(&n);
        }
    }
    if (open)
        for (const auto &c : n.children())
            nodeTree(app, g, *c, world, depth + 1);
}

void nodePanel(App &app, ui::Context &g, ui::Rect r) {
    g.beginPanel("node", r, "Selected Node");
    scene::Node *n = app.selected;
    if (!n) {
        g.message("Click a node in the tree.");
        g.endPanel();
        return;
    }
    g.message((std::string(typeName(n->type())) + "  " + n->name()).c_str());
    g.toggle("Show Bounds", app.showSelectedBounds);
    uint32_t mask = n->mask(), filter = n->stateFilter();
    if (g.bits("Mask", mask, 16))
        n->setMask(mask);
    if (g.bits("State Filter", filter, 16))
        n->setStateFilter(filter);
    const Mat4 &m = n->matrix();
    Vec3 t = m.translation();
    Sphere bs = n->bounds();
    std::vector<std::pair<std::string, std::string>> rows = {
        {"Translation", fmt("%.4g %.4g %.4g", t.x, t.y, t.z)},
        {"Scale", fmt("%.4g", m.maxScale())},
        {"Bounds center", fmt("%.4g %.4g %.4g", bs.center.x, bs.center.y, bs.center.z)},
        {"Bounds radius", fmt("%.4g", bs.radius)},
        {"Children", fmt("%.0f", (double)n->children().size())},
    };
    switch (n->type()) {
    case scene::NodeType::LOD: {
        std::string s;
        for (float v : static_cast<scene::LOD &>(*n).ranges())
            s += fmt("%.4g ", v);
        rows.push_back({"Ranges", s});
        break;
    }
    case scene::NodeType::Instancer: {
        auto &in = static_cast<scene::Instancer &>(*n);
        rows.push_back({"Instances", fmt("%.0f", (double)in.instances().size())});
        for (size_t i = 0; i < in.levels().size(); i++)
            rows.push_back({fmt("Level %.0f", (double)i),
                            fmt("%.0f parts, to %.4g", (double)in.levels()[i].parts.size(),
                                in.levels()[i].maxDistance)});
        break;
    }
    default:
        break;
    }
    g.table(rows);
    if (n->type() == scene::NodeType::Switch) {
        auto &sw = static_cast<scene::Switch &>(*n);
        float v = (float)sw.value();
        if (g.slider("Switch Value (-1 all, -2 none)", v, -2, (float)n->children().size() - 1, "%.0f"))
            sw.setValue((int)std::lround(v));
    }
    if (n->type() == scene::NodeType::Billboard) {
        auto &bb = static_cast<scene::Billboard &>(*n);
        int mode = bb.mode() == scene::Billboard::Mode::Point;
        if (g.radio("Mode", mode, {"Axis", "Point"}))
            bb.setMode(mode ? scene::Billboard::Mode::Point : scene::Billboard::Mode::Axis);
    }
    if (n->type() == scene::NodeType::Geode || n->type() == scene::NodeType::Billboard) {
        std::vector<std::pair<std::string, std::string>> d;
        for (const scene::Drawable &dr : static_cast<scene::Geode &>(*n).drawables()) {
            std::string what;
            if (dr.mesh) {
                uint32_t per = dr.mesh->primitive == assets::Primitive::Triangles ? 3
                               : dr.mesh->primitive == assets::Primitive::Lines ? 2 : 1;
                const char *kind = per == 3 ? " tris" : per == 2 ? " lines" : " points";
                what = fmt("%.0f", (double)(dr.mesh->indexCount / per)) + kind;
            }
            if (dr.material) {
                const assets::Material &p = dr.material->params;
                if (dr.material->texture)
                    what += " tex";
                if (p.alpha == assets::AlphaMode::Blend)
                    what += " blend";
                if (p.decal)
                    what += " decal";
                if (p.shading == assets::Shading::Unlit)
                    what += " unlit";
            }
            std::string name = dr.mesh && !dr.mesh->name.empty() ? dr.mesh->name : "mesh";
            if (dr.material && !dr.material->params.name.empty())
                name += " / " + dr.material->params.name;
            d.push_back({name, what});
            if (d.size() >= 40) {
                d.push_back({"...", ""});
                break;
            }
        }
        g.label("Drawables", ui::Font::Bold);
        g.table(d);
    }
    g.endPanel();
}

// --- the performances -------------------------------------------------------------

void perfPanel(App &app, ui::Context &g, ui::Rect r) {
    g.beginPanel("perf", r, "Stats");
    const render::RenderStats &st = app.renderer->stats();
    float fms = average(app.frameMs, 60);
    g.message(fmt("%.1f fps   %.2f ms per frame", fms > 0 ? 1000 / fms : 0, fms).c_str());
    auto plot = [&](const std::deque<float> &v, float minScale, ui::Color c, const char *name) {
        // the last 240 frames, the newest on the right
        float buf[240] = {};
        int n = (int)std::min<size_t>(v.size(), 240);
        for (int i = 0; i < n; i++)
            buf[240 - n + i] = v[v.size() - n + i];
        float avg = average(v, 60), peak = 0;
        for (int i = 0; i < 240; i++)
            peak = std::max(peak, buf[i]);
        float scale = minScale;
        while (scale < peak * 1.1f)
            scale *= 2;
        char overlay[64];
        std::snprintf(overlay, sizeof overlay, "%s %.3f ms", name, avg);
        g.plot(buf, 240, scale, c, overlay);
    };
    plot(app.frameMs, 16.667f, ui::Color::rgb(0.4f, 1, 0.4f), "frame");
    plot(app.cullMs, 0.5f, ui::Color::rgb(0.4f, 0.8f, 1), "cull");
    plot(app.drawMs, 1.0f, ui::Color::rgb(1, 0.6f, 0.3f), "draw");
    double items = st.opaqueItems + st.transparentItems;
    g.table({
        {"Draw calls", fmt("%.0f", st.gpu.drawCalls)},
        {"Triangles", fmt("%.0f", st.gpu.triangles)},
        {"Pipeline / texture changes", fmt("%.0f / %.0f", st.gpu.pipelineChanges, st.gpu.textureChanges)},
        {"Items opaque / transparent", fmt("%.0f / %.0f", st.opaqueItems, st.transparentItems)},
        {"Items per draw", fmt("%.1f", st.gpu.drawCalls ? items / st.gpu.drawCalls : 0.0)},
        {"Nodes visited / culled", fmt("%.0f / %.0f", st.nodesVisited, st.nodesCulled)},
        {"Instances visited / culled", fmt("%.0f / %.0f", st.instancesVisited, st.instancesCulled)},
        {"CPU cull / sort", fmt("%.3f / %.3f ms", st.cullMs, st.sortMs)},
        {"CPU upload / draw", fmt("%.3f / %.3f ms", st.uploadMs, st.drawMs)},
    });
    g.toggle("Print Once a Second", app.printStats);
    g.endPanel();
}

} // namespace

void guiFrame(App &app, int width, int height) {
    ui::Context &g = *app.ui;
    g.beginFrame(width, height, app.input);
    if (app.showGui) {
        float h = (float)height;
        settingsPanel(app, g, h);
        float x = width - kRightW;
        float perfH = app.showPerf ? std::min(kPerfH, h * 0.5f) : 0;
        if (app.showGraph) {
            float gh = h - perfH;
            float treeH = std::floor(gh * 0.55f);
            g.beginPanel("graph", {x, 0, kRightW, treeH}, "Scene Graph");
            nodeTree(app, g, *app.models, Mat4(), 0);
            g.endPanel();
            nodePanel(app, g, {x, treeH, kRightW, gh - treeH});
        }
        if (app.showPerf)
            perfPanel(app, g, {x, h - perfH, kRightW, perfH});
    }
    g.endFrame();
}

ui::Rect sceneViewport(const App &app, int width, int height) {
    float left = app.showGui ? kLeftW : 0;
    float right = app.showGui && (app.showGraph || app.showPerf) ? kRightW : 0;
    if (left + right > width * 0.7f)
        left = right = 0; // a small window: the scene under the panels
    return {left, 0, std::max(1.0f, width - left - right), (float)height};
}

bool guiWantsMouse(const App &app) { return app.showGui && app.ui->wantsMouse(); }

std::shared_ptr<scene::Geode> makeBoundsMarker(render::Renderer &renderer) {
    // three circles of radius 1, about X, Y and Z
    assets::Mesh m;
    m.name = "bounds";
    m.primitive = assets::Primitive::Lines;
    const int n = 64;
    for (int axis = 0; axis < 3; axis++)
        for (int i = 0; i < n; i++) {
            float a = 2 * kPi * i / n, c = std::cos(a), s = std::sin(a);
            assets::Vertex v;
            v.position = axis == 0 ? Vec3{0, c, s} : axis == 1 ? Vec3{c, 0, s} : Vec3{c, s, 0};
            v.color = 0xff20e0ffu; // yellow
            m.vertices.push_back(v);
            m.indices.push_back(axis * n + i);
            m.indices.push_back(axis * n + (i + 1) % n);
        }
    m.computeBounds();
    assets::Material mat;
    mat.name = "bounds";
    mat.shading = assets::Shading::Unlit;
    mat.vertexColors = true;
    auto g = scene::make<scene::Geode>();
    g->setName("bounds marker");
    g->addDrawable({renderer.uploadMesh(m), renderer.createMaterial(mat)});
    return g;
}

} // namespace r4fly
