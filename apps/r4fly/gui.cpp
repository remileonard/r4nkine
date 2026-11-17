// gui.cpp - the windows of r4fly (Dear ImGui), as the panel of perfly: the
// settings of the camera, the light, the environment, the rendering, the
// culling and the optimisations; the scene graph with the properties of the
// selected node; the performances.
#include "app.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <cstdio>

namespace r4fly {

namespace {

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

ImVec4 typeColor(scene::NodeType t) {
    switch (t) {
    case scene::NodeType::Geode: return {0.55f, 0.85f, 0.55f, 1};
    case scene::NodeType::Switch: return {0.95f, 0.75f, 0.35f, 1};
    case scene::NodeType::LOD: return {0.45f, 0.75f, 1.0f, 1};
    case scene::NodeType::Billboard: return {0.9f, 0.55f, 0.9f, 1};
    case scene::NodeType::Instancer: return {1.0f, 0.5f, 0.45f, 1};
    default: return {0.85f, 0.85f, 0.85f, 1};
    }
}

const char *primitiveName(assets::Primitive p) {
    switch (p) {
    case assets::Primitive::Lines: return "lines";
    case assets::Primitive::Points: return "points";
    default: return "triangles";
    }
}

void colorEdit(const char *label, Vec3 &c) { ImGui::ColorEdit3(label, &c.x); }

void plot(const char *label, const std::deque<float> &v, float scaleMax, const char *overlay) {
    float buf[512];
    int n = (int)std::min<size_t>(v.size(), 512);
    for (int i = 0; i < n; i++)
        buf[i] = v[v.size() - n + i];
    ImGui::PlotLines(label, buf, n, 0, overlay, 0, scaleMax, ImVec2(-1, 60));
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

// --- the settings -----------------------------------------------------------

void scenePanel(App &app) {
    if (!ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    for (const LoadedFile &f : app.files) {
        ImGui::BulletText("%s", f.path.c_str());
        ImGui::TextDisabled("    %zu meshes, %zu tris, %zu lines, %zu points, %zu materials, %zu images",
                            f.model->meshes.size(), f.triangles, f.lines, f.points, f.model->materials.size(),
                            f.model->images.size());
    }
    if (app.forest)
        ImGui::BulletText("forest: %zu instances, %zu levels", app.forest->instances().size(),
                          app.forest->levels().size());
    if (ImGui::Button("Frame all (R)"))
        app.resetView();
    ImGui::SameLine();
    if (ImGui::Button("Capture"))
        app.snapRequest = "r4fly.ppm";
    ImGui::SameLine();
    ImGui::Checkbox("Graph", &app.showGraph);
    ImGui::SameLine();
    ImGui::Checkbox("Perf", &app.showPerf);
}

void cameraPanel(App &app) {
    if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    Trackball &b = app.ball;
    ImGui::SliderFloat("Field of view", &b.fov, 10, 120, "%.0f deg");
    ImGui::SliderFloat("Heading", &b.heading, -180, 180, "%.0f deg");
    ImGui::SliderFloat("Pitch", &b.pitch, -89, 89, "%.0f deg");
    ImGui::SliderFloat("Distance", &b.distance, b.radius * 0.01f, b.radius * 50, "%.3g",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("Automatic clip planes", &b.autoClip);
    if (!b.autoClip) {
        ImGui::SliderFloat("Near", &b.znear, b.radius * 1e-4f, b.radius * 10, "%.4g", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Far", &b.zfar, b.radius * 0.1f, b.radius * 1000, "%.4g", ImGuiSliderFlags_Logarithmic);
    } else {
        ImGui::TextDisabled("near %.4g, far %.4g", b.znear, b.zfar);
    }
    if (ImGui::Button("Front")) { b.heading = 0; b.pitch = 0; }
    ImGui::SameLine();
    if (ImGui::Button("Side")) { b.heading = 90; b.pitch = 0; }
    ImGui::SameLine();
    if (ImGui::Button("Top")) { b.heading = 0; b.pitch = 89; }
    ImGui::SameLine();
    if (ImGui::Button("3/4")) { b.heading = 30; b.pitch = 25; }
    ImGui::SameLine();
    if (ImGui::Button("Recenter")) b.pan = {};
    Vec3 e = b.eye();
    ImGui::TextDisabled("eye %.3g %.3g %.3g", e.x, e.y, e.z);
}

void lightPanel(App &app) {
    if (!ImGui::CollapsingHeader("Lighting"))
        return;
    render::Environment &env = app.renderer->environment();
    ImGui::Checkbox("Lighting", &app.renderer->options().lighting);
    ImGui::Checkbox("Headlight (follows the eye)", &app.headlight);
    if (!app.headlight) {
        ImGui::SliderFloat("Azimuth", &app.lightAzimuth, -180, 180, "%.0f deg");
        ImGui::SliderFloat("Elevation", &app.lightElevation, -89, 89, "%.0f deg");
    }
    colorEdit("Light color", env.lightColor);
    colorEdit("Ambient", env.ambient);
}

void environmentPanel(App &app) {
    if (!ImGui::CollapsingHeader("Environment"))
        return;
    render::Environment &env = app.renderer->environment();
    ImGui::ColorEdit3("Background", &env.clearColor.x);
    ImGui::Checkbox("Fog", &env.fog);
    if (env.fog) {
        colorEdit("Fog color", env.fogColor);
        float r = app.ball.radius;
        ImGui::SliderFloat("Fog start", &env.fogStart, 0, r * 50, "%.3g", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Fog end", &env.fogEnd, 0, r * 100, "%.3g", ImGuiSliderFlags_Logarithmic);
        if (ImGui::Button("Fog color = background"))
            env.fogColor = {env.clearColor.x, env.clearColor.y, env.clearColor.z};
    }
}

void renderPanel(App &app) {
    if (!ImGui::CollapsingHeader("Rendering"))
        return;
    render::RenderOptions &o = app.renderer->options();
    bool wire = app.renderer->wireframe();
    if (ImGui::Checkbox("Wireframe (W)", &wire))
        app.renderer->setWireframe(wire);
    ImGui::Checkbox("Textures", &o.textures);
    // the state of the camera: the bits the nodes must meet (flight: 0x1 body,
    // 0x2 gear, 0x4 far, 0x8 shadow, 0xff00 weapons)
    ImGui::InputScalar("State mask", ImGuiDataType_U32, &app.camera.mask, nullptr, nullptr, "%08X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    for (int bit = 0; bit < 16; bit++) {
        bool on = (app.camera.mask >> bit) & 1;
        char label[8];
        std::snprintf(label, sizeof label, "%X##b%d", bit, bit);
        if (bit % 8)
            ImGui::SameLine();
        if (ImGui::Checkbox(label, &on))
            app.camera.mask = on ? app.camera.mask | 1u << bit : app.camera.mask & ~(1u << bit);
    }
}

void cullingPanel(App &app) {
    if (!ImGui::CollapsingHeader("Culling and levels of detail"))
        return;
    render::RenderOptions &o = app.renderer->options();
    ImGui::Checkbox("View frustum culling", &o.frustumCulling);
    ImGui::Checkbox("Freeze the culling (move to see it)", &o.freezeCulling);
    ImGui::SliderFloat("LOD scale", &o.lodScale, 0.05f, 20, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("> 1: the coarser levels come nearer");
}

void optimPanel(App &app) {
    if (!ImGui::CollapsingHeader("Optimisations"))
        return;
    render::RenderOptions &o = app.renderer->options();
    ImGui::Checkbox("Sort by state (pipeline, material, mesh)", &o.sortByState);
    ImGui::Checkbox("Instancing (equal items in one draw)", &o.instancing);
    if (ImGui::Checkbox("Vertical sync", &app.vsync))
        SDL_GL_SetSwapInterval(app.vsync ? 1 : 0);
}

// --- the scene graph --------------------------------------------------------

void nodeTree(App &app, scene::Node &n, const Mat4 &parent, int depth) {
    Mat4 world = parent * n.matrix();
    const auto &kids = n.children();
    bool hasContent = n.type() == scene::NodeType::Geode || n.type() == scene::NodeType::Billboard;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (kids.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (app.selected == &n) {
        flags |= ImGuiTreeNodeFlags_Selected;
        app.selectedWorld = world;
    }
    if (depth < 2)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::PushID(&n);
    // the eye: shown or hidden (its mask set to 0)
    bool hidden = app.hiddenMasks.count(&n) > 0;
    bool visible = !hidden;
    if (ImGui::Checkbox("##vis", &visible)) {
        if (!visible) {
            app.hiddenMasks[&n] = n.mask();
            n.setMask(0);
        } else {
            n.setMask(app.hiddenMasks[&n]);
            app.hiddenMasks.erase(&n);
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, typeColor(n.type()));
    char label[256];
    size_t drawables = hasContent ? static_cast<scene::Geode &>(n).drawables().size() : 0;
    if (n.type() == scene::NodeType::Instancer)
        std::snprintf(label, sizeof label, "%s %s  [%zu instances]", typeName(n.type()), n.name().c_str(),
                      static_cast<scene::Instancer &>(n).instances().size());
    else if (drawables)
        std::snprintf(label, sizeof label, "%s %s  [%zu drawables]", typeName(n.type()), n.name().c_str(), drawables);
    else
        std::snprintf(label, sizeof label, "%s %s", typeName(n.type()), n.name().c_str());
    bool open = ImGui::TreeNodeEx("node", flags, "%s", label);
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        app.selected = &n;
    if (open) {
        for (size_t i = 0; i < kids.size(); i++)
            nodeTree(app, *kids[i], world, depth + 1);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void nodeDetails(App &app) {
    scene::Node *n = app.selected;
    if (!n) {
        ImGui::TextDisabled("Select a node in the tree.");
        return;
    }
    ImGui::Text("%s  %s", typeName(n->type()), n->name().c_str());
    ImGui::Checkbox("Show its bounds", &app.showSelectedBounds);
    uint32_t mask = n->mask(), filter = n->stateFilter();
    if (ImGui::InputScalar("Mask", ImGuiDataType_U32, &mask, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal))
        n->setMask(mask);
    if (ImGui::InputScalar("State filter", ImGuiDataType_U32, &filter, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal))
        n->setStateFilter(filter);
    const Mat4 &m = n->matrix();
    Vec3 t = m.translation();
    ImGui::Text("Translation %.4g %.4g %.4g, scale %.4g", t.x, t.y, t.z, m.maxScale());
    Sphere wb = n->bounds();
    ImGui::Text("Bounds (parent space): center %.4g %.4g %.4g, radius %.4g", wb.center.x, wb.center.y, wb.center.z,
                wb.radius);
    switch (n->type()) {
    case scene::NodeType::Switch: {
        auto &sw = static_cast<scene::Switch &>(*n);
        int v = sw.value();
        if (ImGui::InputInt("Switch value (-1 all, -2 none)", &v))
            sw.setValue(std::max(-2, std::min(v, (int)n->children().size() - 1)));
        break;
    }
    case scene::NodeType::LOD: {
        auto &lod = static_cast<scene::LOD &>(*n);
        ImGui::Text("Ranges:");
        for (float r : lod.ranges()) {
            ImGui::SameLine();
            ImGui::Text("%.4g", r);
        }
        break;
    }
    case scene::NodeType::Billboard: {
        auto &bb = static_cast<scene::Billboard &>(*n);
        int mode = bb.mode() == scene::Billboard::Mode::Point;
        if (ImGui::Combo("Mode", &mode, "Axis\0Point\0"))
            bb.setMode(mode ? scene::Billboard::Mode::Point : scene::Billboard::Mode::Axis);
        break;
    }
    case scene::NodeType::Instancer: {
        auto &in = static_cast<scene::Instancer &>(*n);
        ImGui::Text("%zu instances", in.instances().size());
        for (size_t i = 0; i < in.levels().size(); i++)
            ImGui::BulletText("level %zu: %zu parts, up to %.4g", i, in.levels()[i].parts.size(),
                              in.levels()[i].maxDistance);
        break;
    }
    default:
        break;
    }
    if (n->type() == scene::NodeType::Geode || n->type() == scene::NodeType::Billboard) {
        auto &g = static_cast<scene::Geode &>(*n);
        if (ImGui::BeginTable("drawables", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 160))) {
            ImGui::TableSetupColumn("Mesh");
            ImGui::TableSetupColumn("Primitives");
            ImGui::TableSetupColumn("Material");
            ImGui::TableSetupColumn("Flags");
            ImGui::TableHeadersRow();
            for (const scene::Drawable &d : g.drawables()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(d.mesh ? d.mesh->name.c_str() : "-");
                ImGui::TableNextColumn();
                if (d.mesh) {
                    uint32_t per = d.mesh->primitive == assets::Primitive::Triangles ? 3
                                   : d.mesh->primitive == assets::Primitive::Lines ? 2 : 1;
                    ImGui::Text("%u %s", d.mesh->indexCount / per, primitiveName(d.mesh->primitive));
                }
                ImGui::TableNextColumn();
                if (d.material) {
                    const assets::Material &p = d.material->params;
                    ImGui::ColorButton("##c", ImVec4(p.diffuse.x, p.diffuse.y, p.diffuse.z, p.diffuse.w),
                                       ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(p.name.c_str());
                }
                ImGui::TableNextColumn();
                if (d.material) {
                    const assets::Material &p = d.material->params;
                    ImGui::Text("%s%s%s%s%s%s", p.shading == assets::Shading::Unlit ? "unlit " : "",
                                d.material->texture ? "tex " : "", p.alpha == assets::AlphaMode::Blend ? "blend "
                                : p.alpha == assets::AlphaMode::Mask ? "mask " : "",
                                p.doubleSided ? "2-sided " : "", p.decal ? "decal " : "",
                                p.vertexColors ? "vcolors" : "");
                }
            }
            ImGui::EndTable();
        }
    }
}

void graphWindow(App &app) {
    // on the right, above the performances
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(460, std::max(240.0f, display.y - 470)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(display.x - 470, 10), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene graph", &app.showGraph)) {
        ImGui::End();
        return;
    }
    ImGui::BeginChild("tree", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f), ImGuiChildFlags_Border);
    nodeTree(app, *app.models, Mat4(), 0);
    ImGui::EndChild();
    ImGui::SeparatorText("Selected node");
    ImGui::BeginChild("details");
    nodeDetails(app);
    ImGui::EndChild();
    ImGui::End();
}

// --- the performances -------------------------------------------------------

void perfWindow(App &app) {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(460, 440), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(display.x - 470, display.y - 450), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performances", &app.showPerf)) {
        ImGui::End();
        return;
    }
    const render::RenderStats &st = app.renderer->stats();
    float fms = average(app.frameMs, 60);
    ImGui::Text("%.1f fps, %.2f ms per frame", fms > 0 ? 1000 / fms : 0, fms);
    char overlay[64];
    std::snprintf(overlay, sizeof overlay, "frame %.2f ms", fms);
    plot("##frame", app.frameMs, std::max(33.3f, fms * 2), overlay);
    std::snprintf(overlay, sizeof overlay, "cull %.3f ms", average(app.cullMs, 60));
    plot("##cull", app.cullMs, std::max(2.0f, average(app.cullMs, 60) * 3), overlay);
    std::snprintf(overlay, sizeof overlay, "draw (CPU) %.3f ms", average(app.drawMs, 60));
    plot("##draw", app.drawMs, std::max(2.0f, average(app.drawMs, 60) * 3), overlay);
    if (ImGui::BeginTable("counters", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        auto row = [](const char *name, const char *fmt, auto... v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text(fmt, v...);
        };
        row("Draw calls", "%u", st.gpu.drawCalls);
        row("Triangles", "%u", st.gpu.triangles);
        row("Pipeline / texture changes", "%u / %u", st.gpu.pipelineChanges, st.gpu.textureChanges);
        row("Items opaque / transparent", "%u / %u", st.opaqueItems, st.transparentItems);
        row("Items per draw", "%.1f",
            st.gpu.drawCalls ? (double)(st.opaqueItems + st.transparentItems) / st.gpu.drawCalls : 0.0);
        row("Nodes visited / culled", "%u / %u", st.nodesVisited, st.nodesCulled);
        row("Instances visited / culled", "%u / %u", st.instancesVisited, st.instancesCulled);
        row("CPU cull / sort", "%.3f / %.3f ms", st.cullMs, st.sortMs);
        row("CPU upload / draw", "%.3f / %.3f ms", st.uploadMs, st.drawMs);
        ImGui::EndTable();
    }
    ImGui::Checkbox("Print once a second", &app.printStats);
    ImGui::End();
}

} // namespace

void guiInit(App &app, SDL_GLContext ctx) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = "r4fly.ini";
    ImGui::StyleColorsDark();
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 4;
    s.FrameRounding = 3;
    s.Colors[ImGuiCol_WindowBg].w = 0.88f;
    ImGui_ImplSDL2_InitForOpenGL(app.window, ctx);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void guiShutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

bool guiEvent(const SDL_Event &e) { return ImGui_ImplSDL2_ProcessEvent(&e); }
bool guiWantsMouse() { return ImGui::GetIO().WantCaptureMouse; }
bool guiWantsKeyboard() { return ImGui::GetIO().WantCaptureKeyboard; }

void guiFrame(App &app) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (!app.showGui)
        return;
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, ImGui::GetIO().DisplaySize.y - 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("r4fly");
    ImGui::TextDisabled("%s", app.device->name());
    ImGui::TextDisabled("left: turn, wheel: zoom, right: move, F1: GUI");
    scenePanel(app);
    cameraPanel(app);
    lightPanel(app);
    environmentPanel(app);
    renderPanel(app);
    cullingPanel(app);
    optimPanel(app);
    ImGui::End();
    if (app.showGraph)
        graphWindow(app);
    if (app.showPerf)
        perfWindow(app);
}

void guiRender(App &app) {
    ImGui::Render();
    if (!app.showGui)
        return;
    // the GUI draws with its own GL state: the device saves its own around
    app.device->beginHostInterop();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    app.device->endHostInterop();
}

std::shared_ptr<scene::Geode> makeBoundsMarker(render::Renderer &renderer) {
    // three circles of radius 1, about X, Y and Z
    assets::Mesh m;
    m.name = "bounds";
    m.primitive = assets::Primitive::Lines;
    const int n = 64;
    for (int axis = 0; axis < 3; axis++)
        for (int i = 0; i < n; i++) {
            float a = 2 * kPi * i / n;
            Vec3 p;
            float c = std::cos(a), s = std::sin(a);
            if (axis == 0)
                p = {0, c, s};
            else if (axis == 1)
                p = {c, 0, s};
            else
                p = {c, s, 0};
            assets::Vertex v;
            v.position = p;
            v.color = 0xff20e0ffu; // yellow
            uint32_t base = (uint32_t)m.vertices.size();
            m.vertices.push_back(v);
            m.indices.insert(m.indices.end(), {base, axis * n + (uint32_t)((i + 1) % n)});
        }
    m.computeBounds();
    assets::Material mat;
    mat.name = "bounds";
    mat.shading = assets::Shading::Unlit;
    mat.vertexColors = true;
    mat.diffuse = {1, 1, 1, 1};
    auto g = scene::make<scene::Geode>();
    g->setName("bounds marker");
    g->addDrawable({renderer.uploadMesh(m), renderer.createMaterial(mat)});
    return g;
}

} // namespace r4fly
