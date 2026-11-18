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
    // the terrain again, at another level of detail (0 finest .. 2)
    void setTerrainLod(int lod);

    r4::scene::NodePtr root() const { return root_; }
    // the layers, shown or hidden
    void showTerrain(bool on);
    void showMapObjects(bool on);
    void showParts(bool on);

    // what the scene holds, for the GUI
    struct Item {
        std::string name;
        r4::Vec3 position; // R4nkine's axes
        float radius;
        bool found;        // its object is in the TRE
    };
    const std::vector<Item> &parts() const { return parts_; }
    const std::vector<Item> &mapObjects() const { return mapObjects_; }
    std::string worldName, terrainName;
    size_t terrainTriangles = 0, terrainTextures = 0;
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

    AssetManager &assets_;
    RSPalette &palette_;
    r4::render::Renderer &renderer_;
    std::unique_ptr<RSMission> mission_;
    std::unique_ptr<RSWorld> world_;
    std::unique_ptr<RSArea> area_;
    std::map<std::string, std::unique_ptr<RSEntity>> entities_;
    std::map<std::string, r4::scene::NodePtr> objects_;
    std::shared_ptr<r4::scene::Group> root_;
    std::shared_ptr<r4::scene::Switch> terrain_, mapLayer_, partsLayer_;
    std::vector<Item> parts_, mapObjects_;
    int terrainLod_ = 0;
};
