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
//  -flat     le rendu plat, objets et terrain (object_gouraud faux, terrain_flat)
//  -gouraud  l'éclairage des objets calculé aux sommets, comme le jeu
//  -lit      le terrain éclairé par le soleil (ses rampes, ses normales)
//  -sun C E  le soleil : son cap et son élévation, en degrés
//  -shadows  les ombres portées du soleil
//  -at X,Y   avec -mission : la caméra sur ce point de la carte (axes de R4nkine)
//  -upscale N les textures agrandies une fois : 0 non, 1 2xSaI, 2 xBR 2x, 3 xBR 4x
//  -mipmaps  les textures avec leurs mipmaps
//  -aniso N  le filtrage anisotrope (1, 2, 4, 8, 16)
//  -forests N les forêts sur le vert du terrain : 0 aucune, 1 clairsemées,
//            2 normales (par défaut), 3 denses
//  -diag DIR avec -mission : les types de terrain et les textures de la carte,
//            en images dans DIR (voir TerrainDiag.h)
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
#include "TerrainDiag.h"
#include "WorldScene.h"

#include "r4/axes.h"
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
    std::string dataDir = ".", startObject = "F-16DES", startMission, focus, shot, diagDir;
    std::vector<std::string> tres;
    int frames = -1;
    bool openTextures = false, graphAtStart = false;
    float startDistance = 0;
    float startAt[2] = {0, 0};
    bool hasStartAt = false;
    bool flatAtStart = false, gouraudAtStart = false, litTerrainAtStart = false, shadowsAtStart = false;
    int forestsAtStart = 2;
    int upscaleAtStart = 0;
    bool mipmapsAtStart = false;
    float anisoAtStart = 1;
    int reliefAtStart = 2;
    float sunAtStart[2] = {1000, 1000}; // -sun cap élévation
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
        else if (a == "-lit") // le terrain éclairé par le soleil
            litTerrainAtStart = true;
        else if (a == "-relief" && i + 1 < argc) // 0..4 : x1..x16
            reliefAtStart = std::atoi(argv[++i]);
        else if (a == "-sun" && i + 2 < argc) {
            sunAtStart[0] = (float)std::atof(argv[++i]);
            sunAtStart[1] = (float)std::atof(argv[++i]);
        } else if (a == "-flat") // le rendu plat (objets et terrain)
            flatAtStart = true;
        else if (a == "-gouraud") // l'éclairage des objets par sommet, comme le jeu
            gouraudAtStart = true;
        else if (a == "-shadows") // les ombres portées du soleil
            shadowsAtStart = true;
        else if (a == "-at" && i + 1 < argc) // la cible au départ, en X,Y de R4nkine
            hasStartAt = std::sscanf(argv[++i], "%f,%f", &startAt[0], &startAt[1]) == 2;
        else if (a == "-upscale" && i + 1 < argc) // 0..3 : non, 2xSaI, xBR 2x, xBR 4x
            upscaleAtStart = std::clamp(std::atoi(argv[++i]), 0, 3);
        else if (a == "-mipmaps")
            mipmapsAtStart = true;
        else if (a == "-aniso" && i + 1 < argc)
            anisoAtStart = std::max(1.0f, (float)std::atof(argv[++i]));
        else if (a == "-forests" && i + 1 < argc) // 0..3 : aucune .. denses
            forestsAtStart = std::clamp(std::atoi(argv[++i]), 0, 3);
        else if (a == "-diag" && i + 1 < argc) // les types et les textures du terrain
            diagDir = argv[++i];
        else if (a == "-distance" && i + 1 < argc)
            startDistance = (float)std::atof(argv[++i]);
    }

    // la qualité des textures du jeu, pour tous les modèles faits par le pont
    r4bridge::textureQuality().upscale = (r4::assets::Upscale)upscaleAtStart;
    r4bridge::textureQuality().mipmaps = mipmapsAtStart;

    // --- 1. libRealSpace : la configuration (RSArea la lit ; sans fichier,
    // ses valeurs par défaut), les TRE, la palette -----------------------------------
    Config &config = Config::getInstance();
    AssetManager &assets = AssetManager::getInstance();
    if (dataDir.back() != '/' && dataDir.back() != '\\')
        dataDir += '/';
    assets.SetBase(dataDir.c_str());
    // la configuration du jeu, quand elle est là (les ombres du terrain de
    // RSArea : terrain_shadow_lift, terrain_shadow_range...)
    for (const std::string &ini : {dataDir + "config.ini", dataDir + "assets/config.ini", std::string("./assets/config.ini")})
        if (SDL_RWops *f = SDL_RWFromFile(ini.c_str(), "rb")) {
            SDL_RWclose(f);
            config.load(ini);
            std::printf("r4objviewer: configuration %s\n", ini.c_str());
            break;
        }
    if (flatAtStart) {
        config.setBool("Game", "object_gouraud", false);
        config.setBool("Game", "terrain_flat", true);
    }
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
        renderer.options().anisotropy = anisoAtStart;
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
        bool showTerrain = true, showMapObjects = true, showParts = true, showOverlays = true, showForests = true, showBuildings = true;
        // les forêts : leur densité (r4bridge::forestOf), avant le chargement d'un monde
        int forests = forestsAtStart;
        const float kForestDensity[] = {0, 0.4f, 1, 2.5f};
        world.setForestDensity(kForestDensity[forests]);
        std::vector<bool> showScenes; // by scene, then "no scene"
        GraphView graph;
        bool showGraph = graphAtStart;
        std::string worldError;
        const r4::Vec3 objectLight = env.lightDirection;
        // le soleil : son cap et son élévation (degrés, les axes de R4nkine) ;
        // env.lightDirection en est l'opposé (là où va la lumière)
        float sunHeading = r4::axes::headingOf(-objectLight), sunElevation = r4::axes::pitchOf(-objectLight);
        auto sunTowards = [&]() {
            float h = r4::radians(sunHeading), el = r4::radians(sunElevation);
            return r4::axes::kRight * (-std::sin(h) * std::cos(el)) + r4::axes::kForward * (std::cos(h) * std::cos(el)) +
                   r4::axes::kUp * std::sin(el);
        };
        auto sunFrom = [&](r4::Vec3 towards) {
            sunHeading = r4::axes::headingOf(towards);
            sunElevation = r4::axes::pitchOf(towards);
        };
        bool dynamicTerrainLight = litTerrainAtStart;
        int reliefChoice = std::clamp(reliefAtStart, 0, 4); // x1, x2, x4, x8, x16
        auto reliefOf = [&]() { return (float)(1 << reliefChoice); };
        bool followGround = true; // la navigation : la cible posée sur le sol
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
            world.showForests(showForests);
            world.showBuildings(showBuildings);
            if (world.hasLight)
                sunFrom(world.lightTowards); // la lumière du monde
            if (world.terrainLightDynamic() != dynamicTerrainLight || world.terrainRelief() != reliefOf())
                world.setTerrainLight(dynamicTerrainLight, reliefOf());
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
                if (!diagDir.empty()) {
                    std::string error;
                    if (terrainDiagnostic(world.area(), *palette.GetColorPalette(), palette.ranges, diagDir, error))
                        std::printf("r4objviewer: diagnostic du terrain dans %s\n", diagDir.c_str());
                    else
                        std::fprintf(stderr, "r4objviewer: diagnostic : %s\n", error.c_str());
                }
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
        if (hasStartAt && mode == 1) {
            target = {startAt[0], startAt[1], 0};
            float ground;
            if (world.groundHeight(target, ground))
                target.z = ground;
        }
        if (startDistance > 0)
            distance = startDistance;
        if (sunAtStart[0] < 999) {
            sunHeading = sunAtStart[0];
            sunElevation = sunAtStart[1];
        }
        r4::render::Camera camera;

        r4::ui::Input input;
        bool quit = false, sceneDrag = false, wireframe = false;
        bool openRender = false, openPost = false, openPerf = true, openObject = true;
        bool vsync = true; // SDL_GL_SetSwapInterval(1) à l'ouverture de la fenêtre
        // le rendu plat et l'éclairage par sommet, d'après la configuration du
        // jeu : object_gouraud (faux : les objets à plat), terrain_flat
        bool flat = !config.getBool("Game", "object_gouraud", true) || config.getBool("Game", "terrain_flat", false);
        bool gouraudByVertex = gouraudAtStart;
        renderer.options().rampGouraud = gouraudByVertex;
        renderer.options().flatShading = flat;
        bool openWorld = true, openParts = false, openMapObjects = false, openScenes = true;
        bool openSun = false, openNav = true;
        // les ombres portées : une shadow map du soleil sur la vue jusqu'à
        // leur distance (Auto : celle de la caméra x 3)
        bool shadows = shadowsAtStart;
        int shadowRange = 0;
        const float kShadowRanges[] = {0, 500, 2000, 10000, 50000};
        std::deque<float> frameMs, cullMs, drawMs;
        Uint64 lastFrame = SDL_GetPerformanceCounter();
        const float leftWidth = 230, rightWidth = 310;
        r4::Vec2 doubleClick{-1, -1}; // un double clic sur la vue, en points
        int lastVx = 0, lastVw = 1;   // la vue de l'image précédente, en pixels
        while (!quit) {
            // les événements : sur un panneau la souris va à la GUI, sinon à la caméra
            input.pressed = input.released = false;
            input.wheel = 0;
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                bool guiMouse = gui.wantsMouse() && !sceneDrag;
                if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                    quit = true;
                // les flèches : l'objet suivant ou précédent ; dans une mission, elles déplacent
                if (e.type == SDL_KEYDOWN && mode == 0 &&
                    (e.key.keysym.sym == SDLK_DOWN || e.key.keysym.sym == SDLK_UP)) {
                    int step = e.key.keysym.sym == SDLK_DOWN ? 1 : -1;
                    int n = (int)objects.size();
                    select((current + step + n) % n);
                }
                // dans une mission, un double clic sur le terrain y amène la caméra
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && e.button.clicks == 2 &&
                    !guiMouse && mode == 1 && currentWorld >= 0)
                    doubleClick = {(float)e.button.x, (float)e.button.y};
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
            float frameSeconds = frameMs.empty() ? 1.0f / 60 : std::min(0.1f, frameMs.back() / 1000.0f);

            // --- la navigation dans une mission ---------------------------------------
            if (mode == 1 && currentWorld >= 0) {
                // le clavier (les touches à leur place : WASD en QWERTY, ZQSD en AZERTY)
                const Uint8 *keys = SDL_GetKeyboardState(nullptr);
                float fwd = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f) -
                            (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);
                float side = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) -
                             (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
                float turn = (keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_E] ? 1.0f : 0.0f);
                float zoom = (keys[SDL_SCANCODE_F] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_R] ? 1.0f : 0.0f);
                bool fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                if (fwd != 0 || side != 0) {
                    // dans le plan du sol, à une vitesse qui suit le recul de la caméra
                    r4::Vec3 f = orbit.forward(), r = orbit.right();
                    f -= r4::axes::kUp * r4::axes::heightOf(f);
                    r -= r4::axes::kUp * r4::axes::heightOf(r);
                    if (r4::length(f) > 1e-4f)
                        f = r4::normalize(f);
                    if (r4::length(r) > 1e-4f)
                        r = r4::normalize(r);
                    float speed = std::max(50.0f, distance) * (fast ? 3.0f : 0.8f);
                    target += (f * fwd + r * side) * (speed * frameSeconds);
                }
                if (turn != 0) {
                    float h, p, rl;
                    orbit.angles(h, p, rl);
                    orbit.setAngles(h + turn * 60 * frameSeconds, p, rl);
                }
                if (zoom != 0)
                    distance *= std::exp(zoom * frameSeconds * (fast ? 2.5f : 1.2f));
                // le double clic : le point du terrain sous la souris
                if (doubleClick.x >= 0) {
                    float pixel = win.pointWidth > 0 ? (float)win.width / win.pointWidth : 1;
                    float nx = (doubleClick.x * pixel - lastVx) / lastVw * 2 - 1;
                    float ny = 1 - doubleClick.y * pixel / std::max(1, win.height) * 2;
                    r4::Mat4 inv = r4::inverse(camera.projection * camera.view);
                    r4::Vec4 a = inv * r4::Vec4(nx, ny, -1, 1), b = inv * r4::Vec4(nx, ny, 1, 1);
                    r4::Vec3 from = r4::Vec3(a) / a.w, dir = r4::normalize(r4::Vec3(b) / b.w - r4::Vec3(a) / a.w);
                    // la marche le long du rayon, puis une dichotomie
                    float step = std::max(5.0f, distance / 200), t0 = 0, ground = 0;
                    bool hit = false;
                    for (float t = 0; t < 600000 && !hit; t += step) {
                        r4::Vec3 p = from + dir * t;
                        if (world.groundHeight(p, ground) && r4::axes::heightOf(p) <= ground) {
                            float lo = t0, hi = t;
                            for (int k = 0; k < 24; k++) {
                                float mid = (lo + hi) / 2;
                                r4::Vec3 q = from + dir * mid;
                                float g = 0;
                                (world.groundHeight(q, g) && r4::axes::heightOf(q) <= g ? hi : lo) = mid;
                            }
                            target = from + dir * hi;
                            hit = true;
                        }
                        t0 = t;
                        step *= 1.01f; // plus loin, plus vite
                    }
                    doubleClick = {-1, -1};
                }
                // la cible sur le sol
                float ground = 0;
                if (followGround && world.groundHeight(target, ground))
                    target = target - r4::axes::kUp * r4::axes::heightOf(target) + r4::axes::kUp * ground;
            }

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
                if (gui.toggle("Flat Shading", flat)) {
                    // les objets : la normale de la face (le object_gouraud du jeu) ;
                    // le terrain : ses couleurs refaites par RSArea (terrain_flat)
                    renderer.options().flatShading = flat;
                    config.setBool("Game", "terrain_flat", flat);
                    if (mode == 1 && currentWorld >= 0)
                        loadWorld(currentWorld);
                }
                if (gui.toggle("Gouraud by Vertex (as the game)", gouraudByVertex))
                    renderer.options().rampGouraud = gouraudByVertex;
                // les textures : agrandies une fois, au chargement, puis filtrées ;
                // l'objet ou le monde est refait
                r4bridge::TextureQuality &tq = r4bridge::textureQuality();
                int up = (int)tq.upscale;
                bool remake = gui.menu("Texture Upscale", up, {"None (as the game)", "2xSaI", "xBR 2x", "xBR 4x"});
                tq.upscale = (r4::assets::Upscale)up;
                remake |= gui.toggle("Mipmaps", tq.mipmaps);
                const float kAniso[] = {1, 2, 4, 8, 16};
                int an = 0;
                while (an < 4 && kAniso[an] < renderer.options().anisotropy)
                    an++;
                bool anisoOk = device->limits().maxAnisotropy > 1;
                if (gui.menu("Anisotropic Filter", an, {"Off", "2x", "4x", "8x", "16x"}, anisoOk)) {
                    renderer.options().anisotropy = kAniso[an];
                    remake = true;
                }
                if (remake) {
                    if (mode == 1 && currentWorld >= 0)
                        loadWorld(currentWorld);
                    else if (current >= 0)
                        load(current);
                }
                gui.columns(2);
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
                           {"Shadow casters / CPU", fmt("%u / %.2f ms", st.shadowCasters, st.shadowMs)},
                           {"Nodes visited / culled", fmt("%u / %u", st.nodesVisited, st.nodesCulled)}});
            }
            if (gui.section("Sun", openSun)) {
                gui.slider("Sun Heading", sunHeading, -180, 180, "%.0f");
                gui.slider("Sun Elevation", sunElevation, -10, 90, "%.0f");
                if (mode == 1 && currentWorld >= 0) {
                    gui.columns(2);
                    if (gui.button("World's Light", world.hasLight))
                        sunFrom(world.lightTowards);
                    if (gui.toggle("Lit Terrain", dynamicTerrainLight))
                        world.setTerrainLight(dynamicTerrainLight, reliefOf());
                    gui.columns(1);
                    if (dynamicTerrainLight &&
                        gui.menu("Relief of the Light", reliefChoice, {"x1 (as it is)", "x2", "x4", "x8", "x16"}))
                        world.setTerrainLight(true, reliefOf());
                    gui.label(dynamicTerrainLight ? "terrain: its ramps lit by the sun" : "terrain: the light of the data (RSArea)",
                              r4::ui::Font::Small);
                }
                gui.toggle("Cast Shadows", shadows);
                if (shadows) {
                    gui.menu("Shadow Distance", shadowRange, {"Auto (the view)", "500", "2 000", "10 000", "50 000"});
                    // ce qui n'est pas éclairé par le soleil : le terrain de
                    // la lumière des données, les pistes
                    gui.slider("Shadow on Unlit", renderer.options().shadowStrength, 0, 1, "%.2f");
                }
            }
            if (mode == 1 && currentWorld >= 0 && gui.section("Navigation", openNav)) {
                gui.message("W A S D / Z Q S D, arrows: move\nQ E / A E: turn   R F: in, out\nShift: faster   double click: go there");
                gui.toggle("Follow the Ground", followGround);
                gui.columns(2);
                if (gui.button("Whole Map")) {
                    r4::Sphere b = world.root()->bounds();
                    target = b.center;
                    distance = b.radius * 1.6f;
                }
                if (gui.button("Top View")) {
                    float h, p, rl;
                    orbit.angles(h, p, rl);
                    orbit.setAngles(h, 89, 0);
                }
                gui.columns(1);
                for (const WorldScene::Item &a : world.areas())
                    if (gui.button(a.name.c_str())) {
                        target = a.position;
                        distance = std::max(a.radius * 2.5f, 1500.0f);
                    }
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
                    if (gui.toggle("Forests", showForests))
                        world.showForests(showForests);
                    if (gui.toggle("Buildings", showBuildings))
                        world.showBuildings(showBuildings);
                    gui.columns(1);
                    if (gui.menu("Forest Density", forests, {"None", "Sparse", "Normal", "Dense"}))
                        world.setForestDensity(kForestDensity[forests]);
                    if (gui.menu("Terrain Detail", terrainLod, {"By Distance (LOD)", "Fine (400 per block)", "Medium (100)", "Coarse (25)"}))
                        world.setTerrainLod(terrainLod);
                    size_t missing = 0;
                    for (const auto *list : {&world.parts(), &world.mapObjects()})
                        for (const WorldScene::Item &it : *list)
                            missing += it.found ? 0 : 1;
                    gui.table({{"World", world.worldName},
                               {"Terrain", world.terrainName},
                               {"Terrain blocks", fmt("%zu (a LOD each)", world.terrainBlocks)},
                               {"Terrain triangles (fine + skirts)", fmt("%zu", world.terrainTriangles)},
                               {"Terrain textures", fmt("%zu", world.terrainTextures)},
                               {"Runway triangles", fmt("%zu", world.overlayTriangles)},
                               {"Buildings", fmt("%zu", world.buildings)},
                               {"Trees (chunks of 5 km)", fmt("%zu (%zu)", world.forestTrees, world.forestChunks)},
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
            lastVx = vx;
            lastVw = vw;
            orbit.target = target;
            orbit.distance = distance;
            bool showWorld = mode == 1 && currentWorld >= 0;
            if (showWorld) {
                // un monde : la carte fait 360 km de côté
                orbit.apply(camera, 45, win.height > 0 ? (float)vw / win.height : 1, std::max(1.0f, distance * 0.01f),
                            distance + 600000.0f);
                env.lightDirection = -sunTowards(); // le soleil
            } else {
                orbit.apply(camera, 45, win.height > 0 ? (float)vw / win.height : 1, distance * 0.01f,
                            distance + bounds.radius * 400);
                env.lightDirection = -sunTowards(); // le soleil
            }
            // les ombres : sur la vue jusqu'à leur distance, les objets
            // jusqu'à 8 km vers le soleil (un avion haut au-dessus du sol)
            renderer.options().shadows = shadows;
            renderer.options().shadowDistance = shadowRange > 0 ? kShadowRanges[shadowRange] : std::max(distance * 3, 100.0f);
            renderer.options().shadowCasterReach = showWorld ? 8000.0f : std::max(distance * 2, bounds.radius * 4);
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
