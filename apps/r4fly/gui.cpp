// gui.cpp - the panels of r4fly in the look of perfly: the settings on the left, the scene graph, the selected node and the performances on the right.
#include "app.h"

#include "r4/ui/controls.h"

#include <algorithm>
#include <cstdio>

namespace r4fly {

namespace {

constexpr float kLeftW = 290;
constexpr float kRightW = 360;
constexpr float kPerfH = 400;

const char *typeName(scene::NodeType t) {
    switch (t) {
    case scene::NodeType::GROUP:
        return "Group";
    case scene::NodeType::GEODE:
        return "Geode";
    case scene::NodeType::SWITCH:
        return "Switch";
    case scene::NodeType::LOD:
        return "LOD";
    case scene::NodeType::BILLBOARD:
        return "Billboard";
    case scene::NodeType::INSTANCER:
        return "Instancer";
    case scene::NodeType::TRIGGER:
        return "Trigger";
    }
    return "?";
}

const char *typeTag(scene::NodeType t) {
    switch (t) {
    case scene::NodeType::GROUP:
        return "GRP";
    case scene::NodeType::GEODE:
        return "GEO";
    case scene::NodeType::SWITCH:
        return "SW";
    case scene::NodeType::LOD:
        return "LOD";
    case scene::NodeType::BILLBOARD:
        return "BB";
    case scene::NodeType::INSTANCER:
        return "INST";
    case scene::NodeType::TRIGGER:
        return "TRIG";
    }
    return "?";
}

ui::Color typeColor(scene::NodeType t) {
    switch (t) {
    case scene::NodeType::GEODE:
        return ui::Color::rgb(0.55f, 0.85f, 0.55f);
    case scene::NodeType::SWITCH:
        return ui::Color::rgb(0.95f, 0.75f, 0.35f);
    case scene::NodeType::LOD:
        return ui::Color::rgb(0.5f, 0.75f, 1.0f);
    case scene::NodeType::BILLBOARD:
        return ui::Color::rgb(0.9f, 0.6f, 0.9f);
    case scene::NodeType::INSTANCER:
        return ui::Color::rgb(1.0f, 0.55f, 0.5f);
    default:
        return ui::Color::rgb(0.85f, 0.85f, 0.88f);
    }
}

std::string fmt(const char *f, double a, double b = 0, double c = 0) {
    char buf[128];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

float average(const std::deque<float> &v, size_t last) {
    if (v.empty()) {
        return 0;
    }
    size_t n = std::min(last, v.size());
    float s = 0;
    for (size_t i = v.size() - n; i < v.size(); i++) {
        s += v[i];
    }
    return s / n;
}

std::string baseName(const std::string &p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

void settingsPanel(App &app, ui::Context &g, float height) {
    g.beginPanel("settings", {0, 0, kLeftW, height}, "r4fly");
    Trackball &b = app.ball;
    render::RenderOptions &o = app.renderer->options();
    render::Environment &env = app.renderer->environment();
    Vec3 e = b.eye();
    g.message(fmt("X%.1f Y%.1f Z%.1f", e.x, e.y, e.z).append(fmt("  H %.0f P %.0f R %.0f", b.heading, b.pitch, b.roll)).c_str());

    g.columns(2);
    if (g.button("Quit")) {
        app.quit = true;
    }
    if (g.button("GUI Off (F1)")) {
        app.showGui = false;
    }
    if (g.button("Frame All (R)")) {
        app.resetView();
    }
    if (g.button("Snap Screen")) {
        app.snapRequest = "r4fly.ppm";
    }
    g.toggle("Scene Graph", app.showGraph);
    g.toggle("Stats", app.showPerf);
    g.columns(1);
    if (g.section("Show Tree", app.openTree)) {
        g.toggle("Show Tree", app.showTree);
        g.slider("Show Tree X Pan", app.treePanX, -1, 1, "%.3f");
        g.slider("Show Tree Y Pan", app.treePanY, -1, 1, "%.3f");
        if (app.treeScale > 0) {
            g.slider("Show Tree Scale", app.treeScale, 0.02f, 4, "%.2f", true);
        }
        if (g.slider("Show Tree Depth", app.treeDepth, 1, 20, "%.0f")) {
            app.treeScale = -1;
        }
        if (g.button("Fit the Tree")) {
            app.treePanX = app.treePanY = 0;
            app.treeScale = -1;
        }
    }

    if (g.section("Camera", app.openCamera)) {
        g.slider("Field of View", b.fov, 10, 120, "%.1f");
        bool turned = g.slider("Yaw (Heading)", b.heading, -180, 180, "%.1f");
        turned |= g.slider("Pitch", b.pitch, -89, 89, "%.1f");
        turned |= g.slider("Roll", b.roll, -180, 180, "%.1f");
        if (turned) {
            b.writeAngles();
        }
        int mode = b.turntable ? 1 : 0;
        if (g.menu("Mouse", mode, {"Trackball (screen axes)", "Turntable (no roll)"})) {
            b.turntable = mode == 1;
        }
        g.slider("Distance", b.distance, b.radius * 0.01f, b.radius * 50, "%.3g", true);
        g.toggle("Automatic Clip Planes", b.autoClip);
        g.slider("Near Clip", b.znear, b.radius * 1e-4f, b.radius * 10, "%.4g", true, !b.autoClip);
        g.slider("Far Clip", b.zfar, b.radius * 0.1f, b.radius * 1000, "%.4g", true, !b.autoClip);
        g.columns(4);
        if (g.button("Front")) {
            b.setAngles(0, 0, 0);
        }
        if (g.button("Side")) {
            b.setAngles(90, 0, 0);
        }
        if (g.button("Top")) {
            b.setAngles(0, 89.9f, 0);
        }
        if (g.button("3/4")) {
            b.setAngles(30, 25, 0);
        }
        g.columns(2);
        if (g.button("Level (No Roll)")) {
            b.setAngles(b.heading, b.pitch, 0);
        }
        if (g.button("Recenter")) {
            b.pan = {};
        }
        g.columns(1);
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
        if (g.color("Background", bg)) {
            env.clearColor = {bg.x, bg.y, bg.z, 1};
        }
        int fog = env.fog ? 1 : 0;
        if (g.menu("Fog", fog, {"Off", "Linear"})) {
            env.fog = fog != 0;
        }
        if (env.fog) {
            float r = app.ball.radius;
            g.slider("Near Fog", env.fogStart, r * 0.01f, r * 50, "%.3g", true);
            g.slider("Far Fog", env.fogEnd, r * 0.01f, r * 100, "%.3g", true);
            g.color("Fog Color", env.fogColor);
            if (g.button("Fog Color = Background")) {
                env.fogColor = bg;
            }
        }
    }
    if (g.section("Rendering", app.openRender)) {
        int style = app.renderer->wireframe() ? 1 : 0;
        if (g.menu("Style", style, {"Filled", "Lines"})) {
            app.renderer->setWireframe(style == 1);
        }
        g.toggle("Textures", o.textures);
        g.toggle("Flat Shading", o.flatShading);
        g.toggle("Shadows", o.shadows);
        if (o.shadows) {
            float r = app.ball.radius;
            g.slider("Shadow Distance", o.shadowDistance, r * 0.1f, r * 50, "%.3g", true);
            g.slider("Shadow Strength", o.shadowStrength, 0, 1, "%.2f");
            int sizes[] = {1024, 2048, 4096};
            int sel = o.shadowMapSize <= 1024 ? 0 : o.shadowMapSize <= 2048 ? 1 : 2;
            if (g.menu("Shadow Map", sel, {"1024", "2048", "4096"})) {
                o.shadowMapSize = sizes[sel];
            }
        }
        g.bits("State Mask", app.camera.mask, 16);
        g.columns(2);
        if (g.button("All States")) {
            app.camera.mask = ~0u;
        }
        if (g.button("No State")) {
            app.camera.mask = 0;
        }
        g.columns(1);
    }
    if (g.section("Culling", app.openCull)) {
        int mode = o.frustumCulling ? 1 : 0;
        if (g.menu("Cull Mode", mode, {"None", "View Frustum"})) {
            o.frustumCulling = mode == 1;
        }
        g.toggle("Freeze Culling", o.freezeCulling);
        g.slider("LOD Scale", o.lodScale, 0.05f, 20, "%.2f", true);
    }
    if (g.section("Post Process", app.openPost)) {
        ui::postProcessControls(g, app.post->settings);
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
    for (const LoadedFile &f : app.files) {
        g.message(fmt("%.0f tris, %.0f lines, %.0f points", (double)f.triangles, (double)f.lines, (double)f.points).insert(0, baseName(f.path) + ": ").c_str());
    }
    for (const std::string &e : app.errors) {
        g.message(e.c_str());
    }
    g.endPanel();
}

void nodeTree(App &app, ui::Context &g, scene::Node &n, const Mat4 &parent, int depth) {
    Mat4 world = parent * n.matrix();
    if (app.selected == &n) {
        app.selectedWorld = world;
    }
    auto it = app.openNodes.find(&n);
    if (it == app.openNodes.end()) {
        it = app.openNodes.emplace(&n, depth < 2).first;
    }
    bool open = it->second;
    bool visible = app.hiddenMasks.count(&n) == 0;
    std::string label = n.name().empty() ? std::string("(") + typeName(n.type()) + ")" : n.name();
    if (n.type() == scene::NodeType::INSTANCER) {
        label += fmt("  [%.0f]", (double)static_cast<scene::Instancer &>(n).instances().size());
    } else if (n.type() == scene::NodeType::GEODE || n.type() == scene::NodeType::BILLBOARD) {
        label += fmt("  [%.0f]", (double)static_cast<scene::Geode &>(n).drawables().size());
    }
    ui::Context::TreeRow row;
    row.id = &n;
    row.depth = depth;
    row.label = label.c_str();
    row.tag = typeColor(n.type());
    row.tagText = typeTag(n.type());
    row.hasChildren = !n.children().empty();
    row.selected = app.selected == &n;
    bool wasVisible = visible;
    if (g.treeRow(row, open, visible)) {
        app.selected = app.selected == &n ? nullptr : &n;
    }
    it->second = open;
    if (visible != wasVisible) {
        if (!visible) {
            app.hiddenMasks[&n] = n.mask();
            n.setMask(0);
        } else {
            n.setMask(app.hiddenMasks[&n]);
            app.hiddenMasks.erase(&n);
        }
    }
    if (open) {
        for (const auto &c : n.children()) {
            nodeTree(app, g, *c, world, depth + 1);
        }
    }
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
    uint32_t mask = n->mask();
    uint32_t filter = n->stateFilter();
    if (g.bits("Mask", mask, 16)) {
        n->setMask(mask);
    }
    if (g.bits("State Filter", filter, 16)) {
        n->setStateFilter(filter);
    }
    const Mat4 &m = n->matrix();
    Vec3 t = translationOf(m);
    Sphere bs = n->bounds();
    std::vector<std::pair<std::string, std::string>> rows = {
        {"Translation", fmt("%.4g %.4g %.4g", t.x, t.y, t.z)}, {"Scale", fmt("%.4g", maxScale(m))}, {"Bounds center", fmt("%.4g %.4g %.4g", bs.center.x, bs.center.y, bs.center.z)}, {"Bounds radius", fmt("%.4g", bs.radius)}, {"Children", fmt("%.0f", (double)n->children().size())},
    };
    switch (n->type()) {
    case scene::NodeType::LOD: {
        std::string s;
        for (float v : static_cast<scene::LOD &>(*n).ranges()) {
            s += fmt("%.4g ", v);
        }
        rows.push_back({"Ranges", s});
        break;
    }
    case scene::NodeType::INSTANCER: {
        auto &in = static_cast<scene::Instancer &>(*n);
        rows.push_back({"Instances", fmt("%.0f", (double)in.instances().size())});
        for (size_t i = 0; i < in.levels().size(); i++) {
            rows.push_back({fmt("Level %.0f", (double)i), fmt("%.0f parts, to %.4g", (double)in.levels()[i].parts.size(), in.levels()[i].maxDistance)});
        }
        break;
    }
    default:
        break;
    }
    g.table(rows);
    if (n->type() == scene::NodeType::SWITCH) {
        auto &sw = static_cast<scene::Switch &>(*n);
        float v = (float)sw.value();
        if (g.slider("Switch Value (-1 all, -2 none)", v, -2, (float)n->children().size() - 1, "%.0f")) {
            sw.setValue((int)std::lround(v));
        }
    }
    if (n->type() == scene::NodeType::BILLBOARD) {
        auto &bb = static_cast<scene::Billboard &>(*n);
        int mode = bb.mode() == scene::Billboard::Mode::Point;
        if (g.radio("Mode", mode, {"Axis", "Point"})) {
            bb.setMode(mode ? scene::Billboard::Mode::Point : scene::Billboard::Mode::Axis);
        }
    }
    if (n->type() == scene::NodeType::GEODE || n->type() == scene::NodeType::BILLBOARD) {
        std::vector<std::pair<std::string, std::string>> d;
        for (const scene::Drawable &dr : static_cast<scene::Geode &>(*n).drawables()) {
            std::string what;
            if (dr.mesh) {
                uint32_t per = dr.mesh->primitive == assets::Primitive::TRIANGLES ? 3 : dr.mesh->primitive == assets::Primitive::LINES ? 2 : 1;
                const char *kind = per == 3 ? " tris" : per == 2 ? " lines" : " points";
                what = fmt("%.0f", (double)(dr.mesh->indexCount / per)) + kind;
            }
            if (dr.material) {
                const assets::Material &p = dr.material->params;
                if (dr.material->texture) {
                    what += " tex";
                }
                if (p.alpha == assets::AlphaMode::BLEND) {
                    what += " blend";
                }
                if (p.alpha == assets::AlphaMode::ADDITIVE) {
                    what += " additive";
                }
                if (p.decal) {
                    what += " decal";
                }
                if (p.shading == assets::Shading::UNLIT) {
                    what += " unlit";
                }
            }
            std::string name = dr.mesh && !dr.mesh->name.empty() ? dr.mesh->name : "mesh";
            if (dr.material && !dr.material->params.name.empty()) {
                name += " / " + dr.material->params.name;
            }
            d.push_back({name, what});
            if (d.size() >= 40) {
                d.push_back({"...", ""});
                break;
            }
        }
        g.label("Drawables", ui::Font::BOLD);
        g.table(d);
    }
    g.endPanel();
}

// the leaves side by side, each parent over its children
struct TreeBox {
    scene::Node *node;
    int depth;
    float x;    // in slots
    int parent; // index, -1 for the root
};

float layoutTree(App &app, scene::Node &n, int depth, int parent, float &nextLeaf, std::vector<TreeBox> &out) {
    int me = (int)out.size();
    out.push_back({&n, depth, 0, parent});
    if (depth + 1 >= (int)app.treeDepth || n.children().empty() || out.size() > 20000) {
        out[me].x = nextLeaf++;
        return out[me].x;
    }
    float first = 0;
    float last = 0;
    for (size_t i = 0; i < n.children().size(); i++) {
        float x = layoutTree(app, *n.children()[i], depth + 1, me, nextLeaf, out);
        if (i == 0) {
            first = x;
        }
        last = x;
    }
    out[me].x = (first + last) * 0.5f;
    return out[me].x;
}

void treeDiagram(App &app, ui::Context &g, ui::Rect area) {
    std::vector<TreeBox> boxes;
    float leaves = 0;
    layoutTree(app, *app.models, 0, -1, leaves, boxes);
    int depth = 0;
    for (const TreeBox &b : boxes) {
        depth = std::max(depth, b.depth);
    }
    const float bw = 112;
    const float bh = 30;
    const float sx = 124;
    const float sy = 62;
    if (app.treeScale < 0) {
        float fit = std::min(area.w / (leaves * sx + 20), area.h / ((depth + 1) * sy + 20));
        app.treeScale = std::max(0.1f, std::min(fit, 2.0f));
    }
    float k = app.treeScale;
    float width = leaves * sx * k;
    float height = (depth + 1) * sy * k;
    float ox = area.x + (area.w - width) * 0.5f - app.treePanX * width;
    float oy = area.y + 16 - app.treePanY * height;
    g.beginCanvas("tree", area);
    g.fill(area, ui::Color{0, 0, 0, 120});
    auto boxRect = [&](const TreeBox &b) {
        return ui::Rect{ox + (b.x * sx + (sx - bw) * 0.5f) * k, oy + b.depth * sy * k, bw * k, bh * k};
    };
    for (const TreeBox &b : boxes) {
        if (b.parent >= 0) {
            ui::Rect p = boxRect(boxes[b.parent]);
            ui::Rect c = boxRect(b);
            g.line(p.x + p.w * 0.5f, p.y + p.h, c.x + c.w * 0.5f, c.y, ui::Color::rgb(0.85f, 0.85f, 0.6f), std::max(1.0f, k));
        }
    }
    for (const TreeBox &b : boxes) {
        ui::Rect r = boxRect(b);
        if (r.x > area.x + area.w || r.x + r.w < area.x || r.y > area.y + area.h || r.y + r.h < area.y) {
            continue;
        }
        bool selected = app.selected == b.node;
        bool hidden = app.hiddenMasks.count(b.node) > 0;
        if (g.clickable(b.node, r)) {
            app.selected = selected ? nullptr : b.node;
        }
        ui::Color face = selected ? ui::Color::rgb(0.35f, 0.45f, 0.85f) : typeColor(b.node->type());
        if (hidden) {
            face = ui::Color::rgb(0.45f, 0.45f, 0.48f);
        }
        g.fill(r, face);
        g.bevel(r, selected);
        if (k >= 0.45f) {
            ui::Color ink = selected ? ui::Color::rgb(1, 1, 1) : ui::Color::rgb(0, 0, 0.1f);
            ui::Font small = k >= 0.8f ? ui::Font::REGULAR : ui::Font::SMALL;
            std::string name = b.node->name().empty() ? "-" : b.node->name();
            if (r.h >= 26) {
                g.text(r.x + 4, r.y + 1, typeName(b.node->type()), ink, ui::Font::SMALL, r.w - 8);
                g.text(r.x + 4, r.y + r.h * 0.45f, name.c_str(), ink, small, r.w - 8);
            } else {
                g.text(r.x + 3, r.y + (r.h - 12) * 0.5f, name.c_str(), ink, ui::Font::SMALL, r.w - 6);
            }
        }
    }
    char info[96];
    std::snprintf(info, sizeof info, "%zu nodes on %d levels: click one to select it", boxes.size(), depth + 1);
    g.text(area.x + 6, area.y + 2, info, ui::Color::rgb(1, 1, 0.5f), ui::Font::SMALL);
    g.endCanvas();
}

void perfPanel(App &app, ui::Context &g, ui::Rect r) {
    g.beginPanel("perf", r, "Stats");
    const render::RenderStats &st = app.renderer->stats();
    float fms = average(app.frameMs, 60);
    g.message(fmt("%.1f fps   %.2f ms per frame", fms > 0 ? 1000 / fms : 0, fms).c_str());
    auto plot = [&](const std::deque<float> &v, float minScale, ui::Color c, const char *name) {
        float buf[240] = {};
        int n = (int)std::min<size_t>(v.size(), 240);
        for (int i = 0; i < n; i++) {
            buf[240 - n + i] = v[v.size() - n + i];
        }
        float avg = average(v, 60);
        float peak = 0;
        for (int i = 0; i < 240; i++) {
            peak = std::max(peak, buf[i]);
        }
        float scale = minScale;
        while (scale < peak * 1.1f) {
            scale *= 2;
        }
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
        {"Shadow casters / CPU", fmt("%.0f / %.3f ms", st.shadowCasters, st.shadowMs)},
        {"CPU upload / draw", fmt("%.3f / %.3f ms", st.uploadMs, st.drawMs)},
    });
    g.toggle("Print Once a Second", app.printStats);
    g.endPanel();
}

} // namespace

void guiFrame(App &app, int width, int height, float pixelScale) {
    ui::Context &g = *app.ui;
    g.beginFrame(width, height, app.input, pixelScale);
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
        if (app.showPerf) {
            perfPanel(app, g, {x, h - perfH, kRightW, perfH});
        }
        if (app.showTree) {
            treeDiagram(app, g, sceneViewport(app, width, height));
        }
    }
    g.endFrame();
}

ui::Rect sceneViewport(const App &app, int width, int height) {
    float left = app.showGui ? kLeftW : 0;
    float right = app.showGui && (app.showGraph || app.showPerf) ? kRightW : 0;
    if (left + right > width * 0.7f) {
        left = right = 0;
    }
    return {left, 0, std::max(1.0f, width - left - right), (float)height};
}

bool guiWantsMouse(const App &app) {
    return app.showGui && app.ui->wantsMouse();
}

std::shared_ptr<scene::Geode> makeBoundsMarker(render::Renderer &renderer) {
    assets::Mesh m;
    m.name = "bounds";
    m.primitive = assets::Primitive::LINES;
    const int n = 64;
    for (int axis = 0; axis < 3; axis++) {
        for (int i = 0; i < n; i++) {
            float a = 2 * kPi * i / n;
            float c = std::cos(a);
            float s = std::sin(a);
            assets::Vertex v;
            v.position = axis == 0 ? Vec3{0, c, s} : axis == 1 ? Vec3{c, 0, s} : Vec3{c, s, 0};
            v.color = 0xff20e0ffu;
            m.vertices.push_back(v);
            m.indices.push_back(axis * n + i);
            m.indices.push_back(axis * n + (i + 1) % n);
        }
    }
    m.computeBounds();
    assets::Material mat;
    mat.name = "bounds";
    mat.shading = assets::Shading::UNLIT;
    mat.vertexColors = true;
    auto g = scene::make<scene::Geode>();
    g->setName("bounds marker");
    g->addDrawable({renderer.uploadMesh(m), renderer.createMaterial(mat)});
    return g;
}

} // namespace r4fly
