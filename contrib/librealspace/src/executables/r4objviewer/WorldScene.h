//
//  WorldScene.h - a mission (or a world alone) of the game as a scene of
//  R4nkine: the terrain, the objects of the map and the parts of the mission.
//
//  libRealSpace loads everything: RSMission, RSWorld (the terrain's name and
//  its light), RSArea (the terrain, its textures and its objects), RSEntity
//  (each object). r4bridge makes the models, R4nkine draws them.
//
#pragma once

#include "../../r4bridge/R4Bridge.h"
#include "../../r4bridge/R4World.h"

#include "r4/render/renderer.h"
#include "r4/scene/node.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class AssetManager;
class RSArea;
class RSEntity;
class RSMission;
class RSPalette;
class RSWorld;

// an entry of the list: a mission, or a world without a mission
struct WorldEntry {
    std::string label;   // MISN-A1, or TURKEY (world)
    std::string file;    // ..\..\DATA\MISSIONS\MISN-A1.IFF
    std::string world;   // SYRIA22
    std::string terrain; // SYRIA.PAK
    bool mission = false;
};

// the missions and worlds whose terrain (<tera>.PAK) is in the data folder
std::vector<WorldEntry> listWorlds(AssetManager &assets, const std::string &dataDir);

class WorldScene {
public:
    WorldScene(AssetManager &assets, RSPalette &palette, r4::render::Renderer &renderer);
    ~WorldScene();

    // loads an entry; false (and why) when it cannot be
    bool load(const WorldEntry &entry, std::string &error);
    // the levels of the blocks of the terrain: 0 by the distance (the LOD
    // nodes), 1..3 the level 0..2 everywhere (the world_detail of the game)
    void setTerrainLod(int mode);
    // the light of the terrain: baked (RSArea's colors) or dynamic (the sun,
    // through normals from the relief); the terrain is made again
    void setTerrainLight(bool dynamic, float relief = 4);
    float terrainRelief() const { return terrainRelief_; }
    bool terrainLightDynamic() const { return terrainDynamic_; }
    // the height of the ground under a point (R4nkine's axes); false out of the map
    bool groundHeight(r4::Vec3 p, float &height) const;

    r4::scene::NodePtr root() const { return root_; }
    RSArea *area() const { return area_.get(); }
    // the layers, shown or hidden
    void showTerrain(bool on);
    void showMapObjects(bool on);
    void showParts(bool on);
    void showOverlays(bool on);
    void showForests(bool on);
    void showBuildings(bool on);
    size_t buildings = 0; // in the cities
    // the forests on the green of the terrain (r4bridge::forestOf), made again
    // at this density (0: none)
    void setForestDensity(float density);
    float forestDensity() const { return forestDensity_; }
    size_t forestTrees = 0, forestChunks = 0;
    // the trees: the detailed ones up to the first, the simple ones up to the second
    const float kTreeNear = 1500, kTreeFar = 6000;
    // the parts of a scene of the mission (or, scene -1, those of no scene)
    void showScene(int scene, bool on);

    // what the scene holds, for the GUI
    struct Item {
        std::string name;
        r4::Vec3 position; // R4nkine's axes
        float radius;
        bool found;        // its object is in the TRE
        int scene = -2;    // the scene that makes it appear; -1: none, -2: not a part
    };
    // the scenes of the mission: their area and how many parts they bring
    struct Scene {
        std::string label;
        size_t parts = 0;
    };
    const std::vector<Scene> &scenes() const { return scenes_; }
    size_t partsWithoutScene = 0;
    const std::vector<Item> &parts() const { return parts_; }
    const std::vector<Item> &mapObjects() const { return mapObjects_; }
    // the areas of the mission (entering one starts its scenes)
    const std::vector<Item> &areas() const { return areas_; }
    std::string worldName, terrainName;
    size_t terrainTriangles = 0, terrainTextures = 0, overlayTriangles = 0, terrainBlocks = 0;
    // the distances of the levels of the blocks: the finest up to 2 blocks,
    // the coarsest beyond 5
    const std::vector<float> kTerrainRanges{0, 40000, 100000, 1e30f};
    double loadMs = 0;
    bool hasLight = false;
    r4::Vec3 lightTowards{0, 0, 1}; // R4nkine's axes, towards the light
    r4bridge::Lighting lighting;
    r4::Vec3 start{0, 0, 0}; // where to look first: the player, or the middle

private:
    // an object of the game by its name (F-16DES): loaded once, then shared
    // by its instances; null when it is not in the TRE
    r4::scene::NodePtr object(const std::string &name, float *radius);
    void clear();
    // the places of the parts, as the game makes them appear: see placeParts()
    void placeParts();
    void buildTerrain();
    void buildForests();

    AssetManager &assets_;
    RSPalette &palette_;
    r4::render::Renderer &renderer_;
    std::unique_ptr<RSMission> mission_;
    std::unique_ptr<RSWorld> world_;
    std::unique_ptr<RSArea> area_;
    std::map<std::string, std::unique_ptr<RSEntity>> entities_;
    std::map<std::string, r4::scene::NodePtr> objects_;
    std::shared_ptr<r4::scene::Group> root_;
    std::shared_ptr<r4::scene::Switch> terrain_, overlays_, mapLayer_, partsLayer_, forests_, buildings_;
    std::vector<std::shared_ptr<r4::scene::Switch>> sceneLayers_; // the scenes, then "no scene"
    std::vector<Scene> scenes_;
    std::vector<Item> parts_, mapObjects_, areas_;
    bool terrainDynamic_ = false;
    float terrainRelief_ = 4;
    float forestDensity_ = 1;
    int terrainLod_ = 0;
    std::vector<r4::scene::LOD *> terrainLods_;
    struct ForestNode {
        r4bridge::ForestChunk chunk;
        std::shared_ptr<r4::scene::Instancer> trees[2];
    };
    std::vector<ForestNode> forestNodes_;
    r4bridge::ForestSettings forestSettings_;
};
