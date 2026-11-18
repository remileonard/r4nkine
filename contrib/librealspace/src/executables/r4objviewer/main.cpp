//
//  r4objviewer - les objets 3D de Strike Commander (OBJECTS\*.IFF) vus par
//  R4nkine.
//
//  libRealSpace lit tout : AssetManager ouvre les TRE, RSPalette la palette,
//  RSEntity un objet. r4bridge::toModel() en fait un modèle R4nkine, que le
//  Renderer charge sur le GPU (upload) et dessine.
//
//  Deux modes :
//  - Objects : un objet du jeu à la fois ;
//  - Missions : une mission (ou un monde seul) avec son terrain, les objets
//    de la carte et ceux de la mission, chargés par libRealSpace (RSMission,
//    RSWorld, RSArea) ; seulement celles dont le terrain (<tera>.PAK) est
//    dans le dossier des données.
//
//    r4objviewer [-data DIR] [-tre FICHIER.TRE ...] [-object NOM] [-mission NOM] [-frames N] [-shot F.bmp]
//
//  -data     le dossier des TRE et des terrains .PAK (par défaut le dossier courant)
//  -tre      les TRE à ouvrir (par défaut TOBIGTRE.TRE, ou OBJECTS.TRE + MISC.TRE)
//  -object   l'objet montré au départ (F-16DES, A-10...)
//  -mission  la mission ou le monde montré au départ (MISN-A1, TURKEY...)
//  -focus    avec -mission : la caméra sur le premier objet de ce nom (ZSU-23...)
//  -graph    le graphe de la scène ouvert au départ
//  -distance la distance de la caméra au départ
//
//  À gauche, la liste des objets ou des missions ; à droite, le rendu et les
//  statistiques.
//  Souris : bouton gauche tourner, molette zoom, bouton droit déplacer.
//  Clavier : flèches haut et bas, objet précédent ou suivant ; Échap, quitter.
//
#include "engine/Config.hpp"
#include "realspace/AssetManager.h"
#include "realspace/RSEntity.h"
#include "realspace/RSPalette.h"
#include "realspace/TreArchive.h"

#include "../../r4bridge/R4Bridge.h"
#include "WorldScene.h"

#include "r4/gpu/device.h"
#include "r4/render/orbit.h"
#include "r4/render/postprocess.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"
#include "r4/ui/controls.h"
#include "r4/ui/ui.h"

#include <SDL.h>
#include <SDL_opengl.h> // glReadPixels, pour -shot

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

// --- la fenêtre : SDL2 + un contexte OpenGL core (3.3, 4.1 sur macOS) -----------------
struct Window {
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    int width = 0, height = 0;           // le framebuffer, en pixels
    int pointWidth = 0, pointHeight = 0; // la fenêtre, en points (la souris)

    bool open(const char *title, int w, int h) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            return false;
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window || !(context = SDL_GL_CreateContext(window)))
            return false;
        SDL_GL_SetSwapInterval(1);
        return true;
    }
    void sizes() {
        SDL_GL_GetDrawableSize(window, &width, &height);
        SDL_GetWindowSize(window, &pointWidth, &pointHeight);
    }
    ~Window() {
        if (context)
            SDL_GL_DeleteContext(context);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

// --- un objet du jeu : son entrée dans le TRE ---------------------------------------
struct ObjectEntry {
    std::string label; // F-16DES
    TreEntry *entry;
};

// les objets 3D des TRE ouverts : les entrées ...\OBJECTS\*.IFF
std::vector<ObjectEntry> listObjects(AssetManager &assets) {
    std::vector<ObjectEntry> list;
    for (TreArchive *tre : assets.tres)
        for (TreEntry *e : tre->entries) {
            std::string name = e->name;
            std::string upper = name;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            size_t slash = upper.rfind('\\');
            if (upper.find("\\OBJECTS\\") == std::string::npos || upper.size() < 4 ||
                upper.compare(upper.size() - 4, 4, ".IFF") != 0)
                continue;
            list.push_back({name.substr(slash + 1, name.size() - slash - 5), e});
        }
    std::sort(list.begin(), list.end(), [](const ObjectEntry &a, const ObjectEntry &b) { return a.label < b.label; });
    return list;
}

std::string fmt(const char *f, ...) {
    char buf[256];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buf, sizeof buf, f, args);
    va_end(args);
    return buf;
}

// une courbe des 240 dernières images, à l'échelle de son pic
void plot(r4::ui::Context &gui, const std::deque<float> &values, float minScale, r4::ui::Color color,
          const char *name) {
    float buf[240] = {};
    int n = (int)std::min<size_t>(values.size(), 240);
    float sum = 0, peak = 0;
    for (int i = 0; i < n; i++) {
        float v = values[values.size() - n + i];
        buf[240 - n + i] = v;
        sum += v;
        peak = std::max(peak, v);
    }
    float scale = minScale;
    while (scale < peak * 1.1f)
        scale *= 2;
    gui.plot(buf, 240, scale, color, fmt("%s %.2f ms", name, n ? sum / n : 0.0f).c_str());
}

// une capture du framebuffer, en BMP (les tests)
void screenshot(const char *path, int w, int h) {
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    for (int y = 0; y < h; y++) // OpenGL commence en bas
        std::memcpy((uint8_t *)s->pixels + (size_t)y * s->pitch, &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

// --- the scene graph, as the Scene Graph of r4fly ------------------------------------
struct GraphView {
    std::map<const r4::scene::Node *, bool> open;      // the open nodes
    std::map<r4::scene::Node *, uint32_t> hiddenMasks; // the hidden nodes, their old mask
    const r4::scene::Node *selected = nullptr;
    void reset() {
        open.clear();
        hiddenMasks.clear();
        selected = nullptr;
    }
};

const char *typeTag(r4::scene::NodeType t) {
    switch (t) {
    case r4::scene::NodeType::Group: return "GRP";
    case r4::scene::NodeType::Geode: return "GEO";
    case r4::scene::NodeType::Switch: return "SW";
    case r4::scene::NodeType::LOD: return "LOD";
    case r4::scene::NodeType::Billboard: return "BB";
    case r4::scene::NodeType::Instancer: return "INST";
    }
    return "?";
}
r4::ui::Color typeColor(r4::scene::NodeType t) {
    switch (t) {
    case r4::scene::NodeType::Geode: return r4::ui::Color::rgb(0.55f, 0.85f, 0.55f);
    case r4::scene::NodeType::Switch: return r4::ui::Color::rgb(0.95f, 0.75f, 0.4f);
    case r4::scene::NodeType::LOD: return r4::ui::Color::rgb(0.6f, 0.75f, 1.0f);
    default: return r4::ui::Color::rgb(0.85f, 0.85f, 0.85f);
    }
}

// a node and, open, its children: the light shows or hides it (its mask), a
// click on its name selects it; returns the node clicked, with its center
r4::scene::Node *graphRows(r4::ui::Context &gui, GraphView &view, r4::scene::Node &n, const r4::Mat4 &parent,
                           int depth, r4::Vec3 &center, float &radius) {
    r4::scene::Node *clicked = nullptr;
    if (gui.remainingHeight() < 22)
        return nullptr; // the panel is full
    auto it = view.open.find(&n);
    if (it == view.open.end())
        it = view.open.emplace(&n, depth < 2).first;
    bool open = it->second;
    bool visible = view.hiddenMasks.count(&n) == 0;
    std::string label = n.name().empty() ? std::string("(") + typeTag(n.type()) + ")" : n.name();
    if (!n.children().empty())
        label += fmt("  [%zu]", n.children().size());
    r4::ui::Context::TreeRow row;
    row.id = &n;
    row.depth = depth;
    row.label = label.c_str();
    row.tag = typeColor(n.type());
    row.tagText = typeTag(n.type());
    row.hasChildren = !n.children().empty();
    row.selected = view.selected == &n;
    bool wasVisible = visible;
    if (gui.treeRow(row, open, visible)) {
        view.selected = &n;
        clicked = &n;
        // its bounds are in the space of its parent
        center = r4::transformPoint(parent, n.bounds().center);
        radius = n.bounds().radius;
    }
    it->second = open;
    if (visible != wasVisible) {
        if (!visible) {
            view.hiddenMasks[&n] = n.mask();
            n.setMask(0);
        } else {
            n.setMask(view.hiddenMasks[&n]);
            view.hiddenMasks.erase(&n);
        }
    }
    if (open) {
        r4::Mat4 world = parent * n.matrix();
        for (const auto &c : n.children())
            if (r4::scene::Node *k = graphRows(gui, view, *c, world, depth + 1, center, radius))
                clicked = k;
    }
    return clicked;
}

} // namespace

int main(int argc, char **argv) {
    std::string dataDir = ".", startObject = "F-16DES", startMission, focus, shot;
    std::vector<std::string> tres;
    int frames = -1;
    bool openTextures = false, graphAtStart = false;
    float startDistance = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-data" && i + 1 < argc)
            dataDir = argv[++i];
        else if (a == "-tre" && i + 1 < argc)
            tres.push_back(argv[++i]);
        else if (a == "-object" && i + 1 < argc)
            startObject = argv[++i];
        else if (a == "-mission" && i + 1 < argc)
            startMission = argv[++i];
        else if (a == "-focus" && i + 1 < argc)
            focus = argv[++i];
        else if (a == "-frames" && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (a == "-shot" && i + 1 < argc)
            shot = argv[++i];
        else if (a == "-textures") // la section des textures ouverte au départ
            openTextures = true;
        else if (a == "-graph") // le graphe de la scène ouvert au départ
            graphAtStart = true;
        else if (a == "-distance" && i + 1 < argc)
            startDistance = (float)std::atof(argv[++i]);
    }

    // --- 1. libRealSpace : la configuration (RSArea la lit ; sans fichier,
    // ses valeurs par défaut), les TRE, la palette -----------------------------------
    Config::getInstance();
    AssetManager &assets = AssetManager::getInstance();
    if (dataDir.back() != '/' && dataDir.back() != '\\')
        dataDir += '/';
    assets.SetBase(dataDir.c_str());
    if (tres.empty()) {
        SDL_RWops *big = SDL_RWFromFile((dataDir + "TOBIGTRE.TRE").c_str(), "rb");
        if (big) {
            SDL_RWclose(big);
            tres = {"TOBIGTRE.TRE"};
        } else {
            tres = {"OBJECTS.TRE", "MISC.TRE"};
        }
    }
    assets.init(tres);

    TreEntry *paletteEntry = assets.GetEntryByName("..\\..\\DATA\\PALETTE\\PALETTE.IFF");
    if (!paletteEntry) {
        std::fprintf(stderr, "r4objviewer: pas de PALETTE.IFF dans les TRE de %s\n", dataDir.c_str());
        return 1;
    }
    RSPalette palette;
    palette.initFromFileRam(paletteEntry->data, paletteEntry->size);
    std::vector<ObjectEntry> objects = listObjects(assets);
    if (objects.empty()) {
        std::fprintf(stderr, "r4objviewer: aucun objet (OBJECTS\\*.IFF) dans les TRE\n");
        return 1;
    }
    std::printf("r4objviewer: %zu objets\n", objects.size());
    // les chemins des missions, des objets et des textures du terrain, comme le jeu
    assets.mission_root_path = "..\\..\\DATA\\MISSIONS\\";
    assets.object_root_path = "..\\..\\DATA\\OBJECTS\\";
    assets.texture_root_path = "..\\..\\DATA\\TXM\\";
    assets.txm_filename = assets.texture_root_path + "TXMPACK.PAK";
    assets.acc_filename = assets.texture_root_path + "ACCPACK.PAK";
    std::vector<WorldEntry> worlds = listWorlds(assets, dataDir);
    std::printf("r4objviewer: %zu missions ou mondes avec leur terrain\n", worlds.size());

    // --- 2. R4nkine : la fenêtre, le device, le renderer, la GUI ----------------------------
    Window win;
    if (!win.open("r4objviewer - libRealSpace + R4nkine", 1400, 860)) {
        std::fprintf(stderr, "r4objviewer: %s\n", SDL_GetError());
        return 1;
    }
    std::string error;
    std::unique_ptr<r4::gpu::Device> device =
        r4::gpu::createGLDevice([](const char *n) { return SDL_GL_GetProcAddress(n); }, &error);
    if (!device) {
        std::fprintf(stderr, "r4objviewer: %s\n", error.c_str());
        return 1;
    }
    std::printf("r4objviewer: %s\n", device->name());
    {
        r4::render::Renderer renderer(*device);
        r4::render::PostProcess post(*device);
        r4::ui::Context gui(*device);
        r4::render::Environment &env = renderer.environment();
        env.clearColor = {0.30f, 0.42f, 0.60f, 1}; // le ciel
        // la lumière du jeu vient d'en haut, un peu de face
        env.lightDirection = r4::normalize(r4::Vec3{0.3f, 0.5f, -0.8f});
        r4::Vec3 sky{env.clearColor.x, env.clearColor.y, env.clearColor.z};

        // --- 3. un objet : RSEntity -> r4::assets::Model -> scène ----------------------
        int current = -1;
        for (size_t i = 0; i < objects.size(); i++)
            if (objects[i].label == startObject)
                current = (int)i;
        if (current < 0) {
            std::fprintf(stderr, "r4objviewer: pas d'objet %s, le premier est montre\n", startObject.c_str());
            current = 0;
        }
        std::unique_ptr<RSEntity> entity;
        std::shared_ptr<r4::assets::Model> model;
        r4::scene::NodePtr root;
        r4::Sphere bounds;
        r4bridge::Lighting lighting;
        // les textures de l'objet, pour les voir dans le panneau (première ligne en haut)
        struct Thumb {
            std::string name;
            int width, height, frames;
            r4::gpu::TextureHandle texture;
        };
        std::vector<Thumb> thumbs;
        size_t modelVertices = 0, modelTriangles = 0;
        double loadMs = 0;
        float listScroll = 0;
        int listRows = 20; // recalculé à chaque image, selon la hauteur du panneau

        auto load = [&](int index) {
            const ObjectEntry &o = objects[index];
            Uint64 t0 = SDL_GetPerformanceCounter();
            entity = std::make_unique<RSEntity>();
            entity->InitFromRAM(o.entry->data, o.entry->size, o.label);
            model = r4bridge::toModel(entity.get(), *palette.GetColorPalette(), palette.ranges, lighting);
            auto group = r4::scene::make<r4::scene::Group>();
            if (model)
                group->addChild(renderer.upload(*model));
            root = group;
            loadMs = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
            bounds = root->bounds();
            if (bounds.radius <= 0)
                bounds.radius = 1;
            modelVertices = modelTriangles = 0;
            if (model)
                for (const r4::assets::Mesh &m : model->meshes) {
                    modelVertices += m.vertices.size();
                    modelTriangles += m.indices.size() / 3;
                }
            for (Thumb &t : thumbs)
                device->destroyTexture(t.texture);
            thumbs.clear();
            const VGAPalette &pal = *palette.GetColorPalette();
            for (RSImage *img : entity->images) {
                if (!img || !img->data || !img->width || !img->height)
                    continue;
                r4::assets::Image im;
                im.width = (int)img->width;
                im.height = (int)img->height;
                im.channels = 4;
                im.clampS = im.clampT = true;
                im.pixels.resize((size_t)im.width * im.height * 4);
                for (size_t p = 0; p < (size_t)im.width * im.height; p++) {
                    const Texel &c = pal.colors[img->data[p]];
                    uint8_t *d = &im.pixels[p * 4];
                    d[0] = c.r, d[1] = c.g, d[2] = c.b, d[3] = img->data[p] == 255 ? 0 : 255;
                }
                thumbs.push_back({img->name, im.width, im.height, img->nbframes, renderer.uploadImage(im, false)});
            }
            // la liste montre l'objet courant
            if (index < (int)listScroll || index >= (int)listScroll + listRows)
                listScroll = (float)std::max(0, index - listRows / 2);
        };
        load(current);

        // --- 3 bis. une mission : son terrain, ses objets (WorldScene) ----------------
        WorldScene world(assets, palette, renderer);
        int mode = 0;          // 0 objets, 1 missions
        int currentWorld = -1; // dans worlds
        int terrainLod = 0;
        bool showTerrain = true, showMapObjects = true, showParts = true, showOverlays = true;
        std::vector<bool> showScenes; // by scene, then "no scene"
        GraphView graph;
        bool showGraph = graphAtStart;
        std::string worldError;
        const r4::Vec3 objectLight = env.lightDirection;
        float worldScroll = 0;
        auto loadWorld = [&](int index) {
            currentWorld = index;
            worldError.clear();
            if (!world.load(worlds[index], worldError))
                std::fprintf(stderr, "r4objviewer: %s\n", worldError.c_str());
            world.showTerrain(showTerrain);
            world.showMapObjects(showMapObjects);
            world.showParts(showParts);
            world.showOverlays(showOverlays);
            showScenes.assign(world.scenes().size() + 1, true);
            showScenes.back() = false; // the parts of no scene: hidden at first
            graph.reset();
            std::printf("r4objviewer: %s : %zu triangles de terrain, %zu objets de carte, %zu parts, %.0f ms\n",
                        worlds[index].label.c_str(), world.terrainTriangles, world.mapObjects().size(),
                        world.parts().size(), world.loadMs);
            if (index < (int)worldScroll || index >= (int)worldScroll + listRows)
                worldScroll = (float)std::max(0, index - listRows / 2);
        };

        // --- 4. la caméra : r4::render::Orbit tourne autour d'un point ------------------
        r4::render::Orbit orbit;
        orbit.setAngles(-140, 20, 0);
        r4::Vec3 target = bounds.center;
        float distance = bounds.radius * 3;
        auto select = [&](int index) {
            graph.reset();
            load(current = index);
            target = bounds.center;
            distance = bounds.radius * 3;
        };
        // une mission : la caméra sur le joueur (ou le milieu de la carte), d'un peu haut
        auto selectWorld = [&](int index) {
            loadWorld(index);
            if (world.parts().empty()) {
                // un monde seul : toute la carte
                r4::Sphere b = world.root()->bounds();
                target = b.center;
                distance = b.radius * 1.6f;
            } else {
                target = world.start;
                distance = 3000;
            }
            orbit.setAngles(-140, 25, 0);
        };
        if (!startMission.empty()) {
            for (size_t i = 0; i < worlds.size(); i++)
                if (worlds[i].label == startMission || worlds[i].label == startMission + " (world)")
                    currentWorld = (int)i;
            if (currentWorld >= 0) {
                mode = 1;
                selectWorld(currentWorld);
                bool found = false;
                for (const auto *list : {&world.parts(), &world.mapObjects()})
                    for (const WorldScene::Item &it : *list)
                        if (!found && !focus.empty() && it.name.compare(0, focus.size(), focus) == 0) {
                            target = it.position;
                            distance = std::max(it.radius * 4, 60.0f);
                            found = true;
                        }
            } else {
                std::fprintf(stderr, "r4objviewer: pas de mission %s avec son terrain\n", startMission.c_str());
            }
        }
        if (startDistance > 0)
            distance = startDistance;
        r4::render::Camera camera;

        r4::ui::Input input;
        bool quit = false, sceneDrag = false, wireframe = false;
        bool openRender = false, openPost = false, openPerf = true, openObject = true;
        bool vsync = true; // SDL_GL_SetSwapInterval(1) à l'ouverture de la fenêtre
        bool openWorld = true, openParts = false, openMapObjects = false, openScenes = true;
        std::deque<float> frameMs, cullMs, drawMs;
        Uint64 lastFrame = SDL_GetPerformanceCounter();
        const float leftWidth = 230, rightWidth = 310;
        while (!quit) {
            // les événements : sur un panneau la souris va à la GUI, sinon à la caméra
            input.pressed = input.released = false;
            input.wheel = 0;
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                bool guiMouse = gui.wantsMouse() && !sceneDrag;
                if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                    quit = true;
                if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_DOWN || e.key.keysym.sym == SDLK_UP)) {
                    int step = e.key.keysym.sym == SDLK_DOWN ? 1 : -1;
                    if (mode == 0) {
                        int n = (int)objects.size();
                        select((current + step + n) % n);
                    } else if (!worlds.empty()) {
                        int n = (int)worlds.size();
                        selectWorld(((currentWorld < 0 ? 0 : currentWorld) + step + n) % n);
                    }
                }
                if (e.type == SDL_MOUSEMOTION) {
                    input.mouseX = (float)e.motion.x;
                    input.mouseY = (float)e.motion.y;
                    if (sceneDrag && (e.motion.state & SDL_BUTTON_LMASK))
                        orbit.turn(e.motion.xrel * 0.4f, e.motion.yrel * 0.4f);
                    if (sceneDrag && (e.motion.state & SDL_BUTTON_RMASK)) {
                        float k = distance * 0.0015f;
                        target += orbit.right() * (-e.motion.xrel * k) + orbit.up() * (e.motion.yrel * k);
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                    bool down = e.type == SDL_MOUSEBUTTONDOWN;
                    if (down && !guiMouse)
                        sceneDrag = true;
                    if (!sceneDrag) {
                        (e.button.button == SDL_BUTTON_LEFT ? input.left : input.right) = down;
                        (down ? input.pressed : input.released) = true;
                    }
                    if (!down && !(SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)))
                        sceneDrag = false;
                } else if (e.type == SDL_MOUSEWHEEL) {
                    if (guiMouse)
                        input.wheel += (float)e.wheel.y;
                    else
                        distance *= std::exp(-e.wheel.y * 0.12f);
                }
            }
            win.sizes();
            if (frames >= 0 && frames-- == 0)
                quit = true;

            // les temps de l'image précédente
            Uint64 now = SDL_GetPerformanceCounter();
            auto push = [](std::deque<float> &d, double v) {
                d.push_back((float)v);
                if (d.size() > 240)
                    d.pop_front();
            };
            push(frameMs, (now - lastFrame) * 1000.0 / SDL_GetPerformanceFrequency());
            lastFrame = now;
            const r4::render::RenderStats &st = renderer.stats();
            push(cullMs, st.cullMs);
            push(drawMs, st.drawMs);

            // le niveau de détail vu : la distance de l'œil, comme le renderer la mesure
            float eyeDistance = r4::length(orbit.eye() - bounds.center);
            int shownLod = 0;
            if (model && model->nodes.size() > 1 && model->nodes[1].kind == r4::assets::NodeKind::LOD) {
                const std::vector<float> &r = model->nodes[1].lodRanges;
                float d = eyeDistance * renderer.options().lodScale;
                for (size_t i = 0; i + 1 < r.size(); i++)
                    if (d >= r[i] && d < r[i + 1])
                        shownLod = (int)i;
            }

            // --- la GUI : à gauche les objets, à droite le rendu et les statistiques -------
            gui.beginFrame(win.pointWidth, win.pointHeight, input,
                           win.pointWidth > 0 ? (float)win.width / win.pointWidth : 1);

            gui.beginPanel("objects", {0, 0, leftWidth, (float)win.pointHeight}, "r4objviewer");
            if (gui.radio("Mode", mode, {"Objects", "Missions"}) && mode == 1 && currentWorld < 0 && !worlds.empty())
                selectWorld(0);
            if (mode == 1) {
                // les missions et les mondes dont le terrain est là
                if (worlds.empty()) {
                    gui.message("No terrain (.PAK) in the data\nfolder for these missions.");
                } else {
                    gui.message(fmt("%d / %d   %s", currentWorld + 1, (int)worlds.size(),
                                    currentWorld >= 0 ? worlds[currentWorld].label.c_str() : "-")
                                    .c_str());
                    listRows = std::max(4, (int)(gui.remainingHeight() / 21.0f) - 1);
                    int maxFirst = std::max(0, (int)worlds.size() - listRows);
                    worldScroll = std::clamp(worldScroll - input.wheel * 3, 0.0f, (float)maxFirst);
                    int first = (int)worldScroll;
                    for (int i = first; i < std::min((int)worlds.size(), first + listRows); i++) {
                        r4::ui::Context::TreeRow row;
                        row.id = &worlds[i];
                        row.label = worlds[i].label.c_str();
                        row.selected = i == currentWorld;
                        bool open = false, visible = true;
                        if (gui.treeRow(row, open, visible) && i != currentWorld)
                            selectWorld(i);
                    }
                }
                gui.endPanel();
            } else {
            gui.message(fmt("%d / %d   %s", current + 1, (int)objects.size(), objects[current].label.c_str()).c_str());
            gui.columns(2);
            if (gui.button("< Previous"))
                select((current + (int)objects.size() - 1) % (int)objects.size());
            if (gui.button("Next >"))
                select((current + 1) % (int)objects.size());
            gui.columns(1);
            {
                // la liste : autant de lignes que le panneau en tient, la molette la fait défiler
                listRows = std::max(4, (int)(gui.remainingHeight() / 21.0f) - 1);
                int maxFirst = std::max(0, (int)objects.size() - listRows);
                listScroll = std::clamp(listScroll - input.wheel * 3, 0.0f, (float)maxFirst);
                int first = (int)listScroll;
                for (int i = first; i < std::min((int)objects.size(), first + listRows); i++) {
                    r4::ui::Context::TreeRow row;
                    row.id = &objects[i];
                    row.label = objects[i].label.c_str();
                    row.selected = i == current;
                    bool open = false, visible = true;
                    if (gui.treeRow(row, open, visible) && i != current)
                        select(i);
                }
                gui.label(fmt("%d-%d of %d, wheel to scroll", first + 1,
                              std::min((int)objects.size(), first + listRows), (int)objects.size())
                              .c_str(),
                          r4::ui::Font::Small);
            }
            gui.endPanel();
            }

            gui.beginPanel("view", {(float)win.pointWidth - rightWidth, 0, rightWidth, (float)win.pointHeight},
                           "r4objviewer");
            // le graphe de la scène, sous son bouton
            gui.toggle("Scene Graph", showGraph);
            if (showGraph) {
                r4::scene::Node &shown = mode == 1 && currentWorld >= 0 ? *world.root() : *root;
                r4::Vec3 center;
                float radius = 0;
                if (graphRows(gui, graph, shown, r4::Mat4(1.0f), 0, center, radius)) {
                    // la caméra sur le nœud cliqué
                    target = center;
                    distance = std::max(radius * 3, 60.0f);
                }
            }
            if (gui.section("Render", openRender)) {
                gui.columns(2);
                gui.toggle("Wireframe", wireframe);
                gui.toggle("Textures", renderer.options().textures);
                gui.columns(1);
                gui.slider("LOD Scale", renderer.options().lodScale, 0.05f, 4, "%.2f", true);
                bool relit = gui.slider("Ambient", lighting.ambient, 0, 1);
                relit |= gui.slider("Light", lighting.light, 0, 1);
                if (relit) // la lumière est dans les matériaux : l'objet est refait
                    load(current);
                if (gui.color("Sky", sky))
                    env.clearColor = {sky, 1};
                float heading, pitch, roll;
                orbit.angles(heading, pitch, roll);
                bool turned = gui.slider("Yaw (Heading)", heading, -180, 180, "%.0f");
                turned |= gui.slider("Pitch", pitch, -89, 89, "%.0f");
                turned |= gui.slider("Roll", roll, -180, 180, "%.0f");
                if (turned)
                    orbit.setAngles(heading, pitch, roll);
                gui.slider("Distance", distance, bounds.radius * 0.2f, bounds.radius * 400, "%.0f", true);
                if (gui.button("Reset View")) {
                    orbit.setAngles(-140, 20, 0);
                    target = bounds.center;
                    distance = bounds.radius * 3;
                }
            }
            if (gui.section("Post Process", openPost))
                r4::ui::postProcessControls(gui, post.settings);
            if (gui.section("Performance", openPerf)) {
                if (gui.toggle("Vertical Sync", vsync))
                    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
                float avg = 0;
                for (float v : frameMs)
                    avg += v;
                avg = frameMs.empty() ? 0 : avg / frameMs.size();
                gui.message(fmt("%.1f fps   %.2f ms per frame", avg > 0 ? 1000 / avg : 0, avg).c_str());
                plot(gui, frameMs, 16.667f, r4::ui::Color::rgb(0.4f, 1, 0.4f), "frame");
                plot(gui, cullMs, 0.25f, r4::ui::Color::rgb(0.4f, 0.8f, 1), "cull");
                plot(gui, drawMs, 0.5f, r4::ui::Color::rgb(1, 0.6f, 0.3f), "draw");
                gui.table({{"Draw calls", fmt("%u", st.gpu.drawCalls)},
                           {"Triangles drawn", fmt("%u", st.gpu.triangles)},
                           {"Pipeline / texture changes",
                            fmt("%u / %u", st.gpu.pipelineChanges, st.gpu.textureChanges)},
                           {"Items opaque / transparent", fmt("%u / %u", st.opaqueItems, st.transparentItems)},
                           {"Nodes visited / culled", fmt("%u / %u", st.nodesVisited, st.nodesCulled)}});
            }
            if (mode == 1 && currentWorld >= 0) {
                if (gui.section("World", openWorld)) {
                    gui.columns(2);
                    if (gui.toggle("Terrain", showTerrain))
                        world.showTerrain(showTerrain);
                    if (gui.toggle("Runways", showOverlays))
                        world.showOverlays(showOverlays);
                    if (gui.toggle("Map Objects", showMapObjects))
                        world.showMapObjects(showMapObjects);
                    if (gui.toggle("Mission Parts", showParts))
                        world.showParts(showParts);
                    gui.columns(1);
                    if (gui.menu("Terrain Detail", terrainLod, {"Fine (400 per block)", "Medium (100)", "Coarse (25)"}))
                        world.setTerrainLod(terrainLod);
                    size_t missing = 0;
                    for (const auto *list : {&world.parts(), &world.mapObjects()})
                        for (const WorldScene::Item &it : *list)
                            missing += it.found ? 0 : 1;
                    gui.table({{"World", world.worldName},
                               {"Terrain", world.terrainName},
                               {"Terrain triangles", fmt("%zu", world.terrainTriangles)},
                               {"Terrain textures", fmt("%zu", world.terrainTextures)},
                               {"Runway triangles", fmt("%zu", world.overlayTriangles)},
                               {"Map objects", fmt("%zu", world.mapObjects().size())},
                               {"Mission parts", fmt("%zu", world.parts().size())},
                               {"Objects not in the TRE", fmt("%zu", missing)},
                               {"Light ambient / direct", fmt("%.2f / %.2f", world.lighting.ambient, world.lighting.light)},
                               {"Load + convert", fmt("%.0f ms", world.loadMs)}});
                    if (!worldError.empty())
                        gui.message(worldError.c_str());
                }
                // un clic : la caméra sur l'objet
                auto itemList = [&](const std::vector<WorldScene::Item> &items) {
                    for (const WorldScene::Item &it : items) {
                        r4::ui::Context::TreeRow row;
                        row.id = &it;
                        std::string label = it.scene >= 0 ? fmt("%s  (scene %d)", it.name.c_str(), it.scene)
                                            : it.scene == -1 ? it.name + "  (no scene)" : it.name;
                        row.label = label.c_str();
                        row.tagText = it.found ? "" : "?";
                        bool open = false, visible = true;
                        if (gui.treeRow(row, open, visible)) {
                            target = it.position;
                            distance = std::max(it.radius * 4, 60.0f);
                        }
                    }
                };
                if (!world.scenes().empty() && gui.section(fmt("Scenes (%zu)", world.scenes().size()).c_str(), openScenes)) {
                    // the parts by the scene that makes them appear
                    for (size_t sc = 0; sc <= world.scenes().size(); sc++) {
                        bool last = sc == world.scenes().size();
                        std::string label = last ? fmt("No scene (%zu)", world.partsWithoutScene)
                                                 : fmt("%s (%zu)", world.scenes()[sc].label.c_str(), world.scenes()[sc].parts);
                        bool on = showScenes[sc];
                        if (gui.toggle(label.c_str(), on)) {
                            showScenes[sc] = on;
                            world.showScene(last ? -1 : (int)sc, on);
                        }
                    }
                }
                if (!world.parts().empty() && gui.section(fmt("Parts (%zu)", world.parts().size()).c_str(), openParts))
                    itemList(world.parts());
                if (gui.section(fmt("Map Objects (%zu)", world.mapObjects().size()).c_str(), openMapObjects))
                    itemList(world.mapObjects());
            }
            if (mode == 0 && gui.section("Object", openObject)) {
                const ObjectEntry &o = objects[current];
                std::vector<std::pair<std::string, std::string>> rows = {
                    {"File", fmt("%s, %u bytes", o.label.c_str(), (unsigned)o.entry->size)},
                    {"Vertices", fmt("%zu", entity->vertices.size())},
                    {"Triangles / quads", fmt("%zu / %zu", entity->triangles.size(), entity->quads.size())},
                    {"Textured tris / quads", fmt("%zu / %zu", entity->uvs.size(), entity->qmapuvs.size())},
                    {"Textures", fmt("%zu", entity->images.size())},
                    {"Radius", fmt("%.1f", bounds.radius)},
                    {"Load + convert", fmt("%.2f ms", loadMs)}};
                for (size_t i = 0; i < entity->lods.size(); i++)
                    rows.push_back({fmt("LOD %zu%s", i, (int)i == shownLod ? " (shown)" : ""),
                                    fmt("from %d, %d faces", (int)entity->lods[i].dist, entity->lods[i].numTriangles)});
                rows.push_back({"Eye distance x scale", fmt("%.0f", eyeDistance * renderer.options().lodScale)});
                if (model) {
                    rows.push_back({"R4nkine meshes", fmt("%zu (%zu materials)", model->meshes.size(),
                                                          model->materials.size())});
                    rows.push_back({"R4nkine vertices", fmt("%zu (%zu triangles)", modelVertices, modelTriangles)});
                }
                gui.table(rows);
            }
            if (mode == 0 && gui.section(fmt("Textures (%zu)", thumbs.size()).c_str(), openTextures)) {
                for (const Thumb &t : thumbs) {
                    std::string frames = t.frames > 1 ? fmt(", %d frames", t.frames) : "";
                    gui.label(fmt("%s  %dx%d%s", t.name.c_str(), t.width, t.height, frames.c_str()).c_str(),
                              r4::ui::Font::Small);
                    gui.image(t.texture, (float)t.width / t.height, 80);
                }
            }
            gui.endPanel();

            gui.endFrame();

            // --- la scène entre les panneaux, puis la GUI par-dessus -------------------------
            float pixel = win.pointWidth > 0 ? (float)win.width / win.pointWidth : 1;
            int vx = (int)(leftWidth * pixel), vw = std::max(1, win.width - (int)((leftWidth + rightWidth) * pixel));
            orbit.target = target;
            orbit.distance = distance;
            bool showWorld = mode == 1 && currentWorld >= 0;
            if (showWorld) {
                // un monde : la carte fait 360 km de côté
                orbit.apply(camera, 45, win.height > 0 ? (float)vw / win.height : 1, std::max(1.0f, distance * 0.01f),
                            distance + 600000.0f);
                env.lightDirection = -world.lightTowards; // la lumière du monde
            } else {
                orbit.apply(camera, 45, win.height > 0 ? (float)vw / win.height : 1, distance * 0.01f,
                            distance + bounds.radius * 400);
                env.lightDirection = objectLight;
            }
            renderer.setWireframe(wireframe);
            r4::gpu::RenderTargetHandle rt = post.begin(win.width, win.height);
            renderer.setTarget(rt);
            renderer.render(showWorld ? *world.root() : *root, camera, vx, 0, vw, win.height);
            renderer.setTarget({});
            post.end(0, 0, win.width, win.height, win.width, win.height);
            gui.render();
            device->takeStats(); // les draws de la GUI ne sont pas ceux de la scène
            if (!shot.empty() && frames == 0)
                screenshot(shot.c_str(), win.width, win.height);
            SDL_GL_SwapWindow(win.window);
        }
        for (Thumb &t : thumbs)
            device->destroyTexture(t.texture);
    }
    device.reset();
    return 0;
}
