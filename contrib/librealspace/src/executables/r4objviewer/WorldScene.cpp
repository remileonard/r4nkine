//
//  WorldScene.cpp - see WorldScene.h.
//
#include "WorldScene.h"

#include "realspace/AssetManager.h"
#include "realspace/RSArea.h"
#include "realspace/RSEntity.h"
#include "realspace/RSMission.h"
#include "realspace/RSPalette.h"
#include "realspace/RSWorld.h"
#include "realspace/TreArchive.h"

#include "../../r4bridge/R4World.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// the terrain file in the data folder, whatever the case of its name
std::string findTerrain(const std::string &dataDir, const std::string &tera) {
    for (const std::string &name : {upper(tera) + ".PAK", tera + ".PAK", tera + ".pak"}) {
        SDL_RWops *f = SDL_RWFromFile((dataDir + name).c_str(), "rb");
        if (f) {
            SDL_RWclose(f);
            return name;
        }
    }
    return "";
}

// the world of the missions folder: its terrain and its light
std::unique_ptr<RSWorld> loadWorld(AssetManager &assets, const std::string &world) {
    TreEntry *e = assets.GetEntryByName(upper(assets.mission_root_path + world + ".IFF"));
    if (!e)
        return nullptr;
    auto w = std::make_unique<RSWorld>();
    w->InitFromRAM(e->data, e->size);
    return w;
}

} // namespace

std::vector<WorldEntry> listWorlds(AssetManager &assets, const std::string &dataDir) {
    std::vector<WorldEntry> missions, worlds;
    for (TreArchive *tre : assets.tres)
        for (TreEntry *e : tre->entries) {
            std::string file = e->name;
            std::string name = upper(file);
            size_t at = name.find("\\MISSIONS\\");
            if (at == std::string::npos || name.size() < 4 || name.compare(name.size() - 4, 4, ".IFF") != 0)
                continue;
            std::string label = name.substr(at + 10, name.size() - at - 14);
            // a mission names its world; a world names its terrain
            RSMission mission;
            mission.InitFromRAM(e->data, e->size);
            WorldEntry entry;
            entry.file = file;
            if (!mission.mission_data.world_filename.empty()) {
                entry.mission = true;
                entry.world = mission.mission_data.world_filename;
                entry.label = label;
            } else {
                RSWorld world;
                world.InitFromRAM(e->data, e->size);
                if (world.tera.empty())
                    continue;
                entry.world = label;
                entry.label = label + " (world)";
            }
            std::unique_ptr<RSWorld> world = loadWorld(assets, entry.world);
            if (!world || world->tera.empty())
                continue;
            entry.terrain = findTerrain(dataDir, world->tera);
            if (entry.terrain.empty())
                continue; // only what can be shown with its terrain
            (entry.mission ? missions : worlds).push_back(entry);
        }
    auto byLabel = [](const WorldEntry &a, const WorldEntry &b) { return a.label < b.label; };
    std::sort(missions.begin(), missions.end(), byLabel);
    std::sort(worlds.begin(), worlds.end(), byLabel);
    missions.insert(missions.end(), worlds.begin(), worlds.end());
    return missions;
}

WorldScene::WorldScene(AssetManager &assets, RSPalette &palette, r4::render::Renderer &renderer)
    : assets_(assets), palette_(palette), renderer_(renderer) {
    root_ = r4::scene::make<r4::scene::Group>();
    root_->setName("world (libRealSpace axes)");
    // the one place where libRealSpace's axes (Y up) become R4nkine's (Z up)
    root_->setMatrix(r4bridge::realSpaceToR4());
    terrain_ = r4::scene::make<r4::scene::Switch>();
    terrain_->setName("terrain");
    mapLayer_ = r4::scene::make<r4::scene::Switch>();
    mapLayer_->setName("objects of the map");
    partsLayer_ = r4::scene::make<r4::scene::Switch>();
    partsLayer_->setName("parts of the mission");
    overlays_ = r4::scene::make<r4::scene::Switch>();
    overlays_->setName("overlays (runways)");
    root_->addChild(terrain_);
    root_->addChild(overlays_);
    root_->addChild(mapLayer_);
    root_->addChild(partsLayer_);
}

WorldScene::~WorldScene() { clear(); }

void WorldScene::clear() {
    terrain_->removeAllChildren();
    overlays_->removeAllChildren();
    mapLayer_->removeAllChildren();
    partsLayer_->removeAllChildren();
    sceneLayers_.clear();
    scenes_.clear();
    partsWithoutScene = 0;
    overlayTriangles = 0;
    objects_.clear();
    parts_.clear();
    mapObjects_.clear();
    // the parts and the map objects point to the entities: those go last
    mission_.reset();
    area_.reset();
    world_.reset();
    entities_.clear();
}

r4::scene::NodePtr WorldScene::object(const std::string &name, float *radius) {
    auto it = objects_.find(name);
    if (it == objects_.end()) {
        r4::scene::NodePtr node;
        // as SCMission::LoadEntity
        TreEntry *e = assets_.GetEntryByName(upper(assets_.object_root_path + name + ".IFF"));
        if (e) {
            auto entity = std::make_unique<RSEntity>();
            entity->InitFromRAM(e->data, e->size, name);
            // in libRealSpace's axes: the root of the world turns them all
            auto model = r4bridge::toModel(entity.get(), *palette_.GetColorPalette(), palette_.ranges, lighting,
                                           r4bridge::Axes::RealSpace);
            if (model) {
                node = renderer_.upload(*model);
                node->setName(name);
            }
            entities_[name] = std::move(entity);
        }
        it = objects_.emplace(name, node).first;
    }
    if (radius)
        *radius = it->second ? it->second->bounds().radius : 0;
    return it->second;
}

bool WorldScene::load(const WorldEntry &entry, std::string &error) {
    Uint64 t0 = SDL_GetPerformanceCounter();
    clear();
    TreEntry *e = assets_.GetEntryByName(entry.file);
    if (!e) {
        error = "not in the TRE: " + entry.file;
        return false;
    }
    if (entry.mission) {
        mission_ = std::make_unique<RSMission>();
        mission_->InitFromRAM(e->data, e->size);
    }
    world_ = loadWorld(assets_, entry.world);
    if (!world_) {
        error = "no world " + entry.world;
        return false;
    }
    worldName = entry.world;
    terrainName = entry.terrain;
    // the light of the world, as SCMission gives it to SCRenderer::setObjectLight
    hasLight = world_->has_light;
    lighting = {};
    if (hasLight) {
        float total = (float)(world_->light_ambient_weight + world_->light_weight);
        if (total > 0) {
            lighting.ambient = world_->light_ambient_weight / total;
            lighting.light = world_->light_weight / total;
        }
        // libRealSpace makes (c0, c2, -c1) of it; turned to R4nkine's axes,
        // that is (c0, c1, c2): the values of the file as they are
        r4::Vec3 d{(float)world_->light_direction[0], (float)world_->light_direction[1],
                   (float)world_->light_direction[2]};
        if (r4::length(d) > 0)
            lightTowards = r4::normalize(d);
    }

    // the terrain (RSArea reads the PAK in the data folder)
    area_ = std::make_unique<RSArea>();
    area_->InitFromPAKFileName(entry.terrain.c_str());
    setTerrainLod(terrainLod_);
    // the runways and their ground, over the terrain
    if (auto overlays = r4bridge::overlayModel(area_.get(), *palette_.GetColorPalette())) {
        for (const r4::assets::Mesh &m : overlays->meshes)
            overlayTriangles += m.indices.size() / 3;
        overlays_->addChild(renderer_.upload(*overlays));
    }

    // the objects of the map: as SCMission, each its entity
    for (MapObject &o : area_->objects) {
        float radius = 0;
        std::string name(o.name, strnlen(o.name, sizeof o.name));
        r4::scene::NodePtr node = object(name, &radius);
        o.entity = entities_.count(name) ? entities_[name].get() : nullptr;
        r4::Mat4 m = r4bridge::mapObjectMatrix(o);
        if (node) {
            auto place = r4::scene::make<r4::scene::Group>();
            place->setName(name);
            place->setMatrix(m);
            place->addChild(node);
            mapLayer_->addChild(place);
        }
        mapObjects_.push_back({name, r4::transformPoint(r4bridge::realSpaceToR4(), r4::translationOf(m)),
                               radius, node != nullptr});
    }

    // the parts of the mission, where their scenes make them appear
    start = {0, 0, 0};
    if (mission_)
        placeParts();
    loadMs = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    return true;
}

// SCMission::loadMission and SCMissionScene::spawnCast:
// - the player: its position in the area of the mission it names
//   (part position + areas[area_id]->position);
// - the other parts appear with the first scene whose cast holds them (the
//   cast numbers the parts in their list): placement 0, in their area (part
//   position + areas[area_id]->position); placement 1 (relative to the player
//   in the game, at the time the scene comes), at the center of the area of
//   the scene, 4000 high for an aircraft;
// - then on the ground when under it (RSArea::getY), an aircraft within 10 of
//   it too;
// - a part of no scene (brought by a program of the mission) is placed the
//   same way, in a layer of its own, hidden at first.
void WorldScene::placeParts() {
    MISN &data = mission_->mission_data;
    MISN_PART *player = mission_->getPlayerCoord();
    auto areaOf = [&](int id) -> AREA * { return id >= 0 && id < (int)data.areas.size() ? data.areas[id] : nullptr; };
    std::vector<int> sceneOf(data.parts.size(), -1);
    for (size_t s = 0; s < data.scenes.size(); s++)
        for (uint16_t cast : data.scenes[s]->cast)
            if (cast < data.parts.size() && sceneOf[cast] < 0)
                sceneOf[cast] = (int)s;

    auto isAircraft = [&](MISN_PART *part) {
        auto it = entities_.find(part->member_name);
        return it != entities_.end() && it->second->entity_type == EntityType::jet;
    };
    auto onGround = [&](MISN_PART *part) {
        float ground = area_->getY(part->position.x, part->position.z);
        if (isAircraft(part) ? std::abs(ground - part->position.y) <= 10 || part->position.y < ground
                             : part->position.y < ground)
            part->position.y = ground;
    };
    for (size_t i = 0; i < data.parts.size(); i++) {
        MISN_PART *part = data.parts[i];
        object(part->member_name, nullptr); // its entity, for its type
        if (part == player) {
            if (AREA *a = areaOf(part->area_id); a && part->area_id != 255 && part->placement == 0)
                part->position += a->position;
            continue;
        }
        if (part->area_id == 255 && part->placement == 0)
            continue; // a place of the world, as it is
        if (part->placement != 0) {
            // instead of SCMission::placeRelativeToPlayer: the center of the scene's area
            int s = sceneOf[i];
            AREA *a = s >= 0 ? areaOf(data.scenes[s]->area_id) : areaOf(part->area_id);
            if (a)
                part->position = a->position;
            if (isAircraft(part))
                part->position.y = 4000;
        } else if (AREA *a = areaOf(part->area_id)) {
            part->position += a->position;
        }
        onGround(part);
    }

    // a layer by scene, and one for the parts of no scene
    for (size_t s = 0; s < data.scenes.size(); s++) {
        AREA *a = areaOf(data.scenes[s]->area_id);
        Scene sc;
        sc.label = "Scene " + std::to_string(s) + (a ? std::string(": ") + a->AreaName : std::string(": no area"));
        scenes_.push_back(sc);
    }
    for (size_t s = 0; s <= data.scenes.size(); s++) {
        auto layer = r4::scene::make<r4::scene::Switch>();
        layer->setName(s < data.scenes.size() ? scenes_[s].label : "no scene");
        if (s == data.scenes.size())
            layer->setValue(r4::scene::Switch::kNone);
        partsLayer_->addChild(layer);
        sceneLayers_.push_back(layer);
    }
    for (size_t i = 0; i < data.parts.size(); i++) {
        MISN_PART *part = data.parts[i];
        float radius = 0;
        r4::scene::NodePtr node = object(part->member_name, &radius);
        part->entity = entities_.count(part->member_name) ? entities_[part->member_name].get() : nullptr;
        int s = sceneOf[i];
        if (s < 0)
            partsWithoutScene++;
        else
            scenes_[s].parts++;
        r4::Mat4 m = r4bridge::missionPartMatrix(*part);
        if (node) {
            auto place = r4::scene::make<r4::scene::Group>();
            place->setName(part->member_name);
            place->setMatrix(m);
            place->addChild(node);
            sceneLayers_[s < 0 ? data.scenes.size() : (size_t)s]->addChild(place);
        }
        r4::Vec3 at = r4::transformPoint(r4bridge::realSpaceToR4(), r4::translationOf(m));
        std::string label = part->member_name + (part == player ? " (player)" : "");
        parts_.push_back({label, at, radius, node != nullptr, s});
        if (part == player)
            start = at;
    }
}

void WorldScene::showOverlays(bool on) {
    overlays_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}
void WorldScene::showScene(int scene, bool on) {
    size_t i = scene < 0 ? sceneLayers_.size() - 1 : (size_t)scene;
    if (i < sceneLayers_.size())
        sceneLayers_[i]->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}

void WorldScene::setTerrainLod(int lod) {
    terrainLod_ = lod;
    terrain_->removeAllChildren();
    terrainTriangles = terrainTextures = 0;
    if (!area_)
        return;
    auto model = r4bridge::terrainModel(area_.get(), *palette_.GetColorPalette(), lod);
    if (!model)
        return;
    for (const r4::assets::Mesh &m : model->meshes)
        if (m.material == 0)
            terrainTriangles += m.indices.size() / 3;
    terrainTextures = model->images.size();
    terrain_->addChild(renderer_.upload(*model));
}

void WorldScene::showTerrain(bool on) { terrain_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone); }
void WorldScene::showMapObjects(bool on) {
    mapLayer_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}
void WorldScene::showParts(bool on) {
    partsLayer_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}
