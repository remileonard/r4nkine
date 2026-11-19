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
    if (!e) {
        return nullptr;
    }
    auto w = std::make_unique<RSWorld>();
    w->InitFromRAM(e->data, e->size);
    return w;
}

} // namespace

std::vector<WorldEntry> listWorlds(AssetManager &assets, const std::string &dataDir, size_t own) {
    std::vector<WorldEntry> missions;
    std::vector<WorldEntry> worlds;
    for (size_t t = 0; t < assets.tres.size() && t < own; t++) {
        for (TreEntry *e : assets.tres[t]->entries) {
            std::string file = e->name;
            std::string name = upper(file);
            size_t at = name.find("\\MISSIONS\\");
            if (at == std::string::npos || name.size() < 4 || name.compare(name.size() - 4, 4, ".IFF") != 0) {
                continue;
            }
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
                if (world.tera.empty()) {
                    continue;
                }
                entry.world = label;
                entry.label = label + " (world)";
            }
            std::unique_ptr<RSWorld> world = loadWorld(assets, entry.world);
            if (!world || world->tera.empty()) {
                continue;
            }
            entry.terrain = findTerrain(dataDir, world->tera);
            if (entry.terrain.empty()) {
                continue; // only what can be shown with its terrain
            }
            (entry.mission ? missions : worlds).push_back(entry);
        }
    }
    auto byLabel = [](const WorldEntry &a, const WorldEntry &b) {
        return a.label < b.label;
    };
    std::sort(missions.begin(), missions.end(), byLabel);
    std::sort(worlds.begin(), worlds.end(), byLabel);
    missions.insert(missions.end(), worlds.begin(), worlds.end());
    return missions;
}

WorldScene::WorldScene(AssetManager &assets, RSPalette &palette, r4::render::Renderer &renderer) : assets_(assets), palette_(palette), renderer_(renderer) {
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
    forests_ = r4::scene::make<r4::scene::Switch>();
    forests_->setName("forests");
    buildings_ = r4::scene::make<r4::scene::Switch>();
    buildings_->setName("buildings");
    root_->addChild(terrain_);
    root_->addChild(overlays_);
    root_->addChild(forests_);
    root_->addChild(buildings_);
    root_->addChild(mapLayer_);
    root_->addChild(partsLayer_);
}

WorldScene::~WorldScene() {
    clear();
}

void WorldScene::clear() {
    terrain_->removeAllChildren();
    overlays_->removeAllChildren();
    forests_->removeAllChildren();
    forestNodes_.clear();
    forestTrees = forestChunks = 0;
    buildings_->removeAllChildren();
    buildings = 0;
    mapLayer_->removeAllChildren();
    partsLayer_->removeAllChildren();
    sceneLayers_.clear();
    scenes_.clear();
    partsWithoutScene = 0;
    areas_.clear();
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
            auto model = r4bridge::toModel(entity.get(), *palette_.GetColorPalette(), palette_.ranges, lighting, r4bridge::Axes::REAL_SPACE);
            if (model) {
                node = renderer_.upload(*model);
                node->setName(name);
            }
            entities_[name] = std::move(entity);
        }
        it = objects_.emplace(name, node).first;
    }
    if (radius) {
        *radius = it->second ? it->second->bounds().radius : 0;
    }
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
        r4::Vec3 d{(float)world_->light_direction[0], (float)world_->light_direction[1], (float)world_->light_direction[2]};
        if (r4::length(d) > 0) {
            lightTowards = r4::normalize(d);
        }
    }

    // the terrain (RSArea reads the PAK in the data folder, and the textures
    // of the game in the TRE: it cannot do without them)
    if (!assets_.GetEntryByName(assets_.txm_filename) || !assets_.GetEntryByName(assets_.acc_filename)) {
        error = "no " + assets_.txm_filename + " or " + assets_.acc_filename + " in the TRE (TEXTURES.TRE)";
        return false;
    }
    area_ = std::make_unique<RSArea>();
    area_->InitFromPAKFileName(entry.terrain.c_str());
    buildTerrain();
    // the runways and their ground, over the terrain
    if (auto overlays = r4bridge::overlayModel(area_.get(), *palette_.GetColorPalette())) {
        for (const r4::assets::Mesh &m : overlays->meshes) {
            overlayTriangles += m.indices.size() / 3;
        }
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
        mapObjects_.push_back({name, r4::transformPoint(r4bridge::realSpaceToR4(), r4::translationOf(m)), radius, node != nullptr});
    }

    // the parts of the mission, where their scenes make them appear
    start = {0, 0, 0};
    if (mission_) {
        placeParts();
    }
    // the buildings of the cities, villages, farms and harbors, on their
    // textures; none where an object of the map or of the mission stands
    // (R4nkine's (x, y) is libRealSpace's (x, -z))
    std::vector<r4::Vec3> keepOut;
    for (const auto *list : {&parts_, &mapObjects_}) {
        for (const Item &it : *list) {
            keepOut.push_back({it.position.x, -it.position.y, std::max(it.radius * 1.2f, 40.0f)});
        }
    }
    if (auto city = r4bridge::cityModel(area_.get(), *palette_.GetColorPalette(), lighting, keepOut, &buildings)) {
        buildings_->addChild(renderer_.upload(*city));
    }
    // the forests, around the objects
    buildForests();
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
    auto areaOf = [&](int id) -> AREA * {
        return id >= 0 && id < (int)data.areas.size() ? data.areas[id] : nullptr;
    };
    std::vector<int> sceneOf(data.parts.size(), -1);
    for (size_t s = 0; s < data.scenes.size(); s++) {
        for (uint16_t cast : data.scenes[s]->cast) {
            if (cast < data.parts.size() && sceneOf[cast] < 0) {
                sceneOf[cast] = (int)s;
            }
        }
    }

    auto isAircraft = [&](MISN_PART *part) {
        auto it = entities_.find(part->member_name);
        return it != entities_.end() && it->second->entity_type == EntityType::jet;
    };
    auto onGround = [&](MISN_PART *part) {
        float ground = area_->getY(part->position.x, part->position.z);
        if (isAircraft(part) ? std::abs(ground - part->position.y) <= 10 || part->position.y < ground : part->position.y < ground) {
            part->position.y = ground;
        }
    };
    for (size_t i = 0; i < data.parts.size(); i++) {
        MISN_PART *part = data.parts[i];
        object(part->member_name, nullptr); // its entity, for its type
        if (part == player) {
            if (AREA *a = areaOf(part->area_id); a && part->area_id != r4bridge::AREA_NONE && part->placement == r4bridge::PLACEMENT_IN_AREA) {
                part->position += a->position;
            }
            continue;
        }
        if (part->area_id == r4bridge::AREA_NONE && part->placement == r4bridge::PLACEMENT_IN_AREA) {
            continue; // a place of the world, as it is
        }
        if (part->placement != r4bridge::PLACEMENT_IN_AREA) {
            // instead of SCMission::placeRelativeToPlayer: the center of the scene's area
            int s = sceneOf[i];
            AREA *a = s >= 0 ? areaOf(data.scenes[s]->area_id) : areaOf(part->area_id);
            if (a) {
                part->position = a->position;
            }
            if (isAircraft(part)) {
                part->position.y = 4000;
            }
        } else if (AREA *a = areaOf(part->area_id)) {
            part->position += a->position;
        }
        onGround(part);
    }

    // the areas, to go to them
    for (size_t a = 0; a < data.areas.size(); a++) {
        AREA *ar = data.areas[a];
        r4::Vec3 at = r4::transformPoint(r4bridge::realSpaceToR4(), {ar->position.x, ar->position.y, ar->position.z});
        float radius = std::max(1000.0f, 0.5f * (float)std::max(ar->AreaWidth, ar->AreaHeight));
        areas_.push_back({"Area " + std::to_string(a) + ": " + ar->AreaName, at, radius, true});
    }
    // the graph: a node by area of the mission (entering it starts its
    // scenes), a layer by scene under its area, and one for the parts of no
    // scene
    std::map<int, std::shared_ptr<r4::scene::Group>> areaNodes;
    auto areaNode = [&](int id) {
        auto it = areaNodes.find(id);
        if (it != areaNodes.end()) {
            return it->second;
        }
        auto g = r4::scene::make<r4::scene::Group>();
        AREA *a = areaOf(id);
        g->setName(a ? "Area " + std::to_string(id) + ": " + a->AreaName : std::string("No area"));
        partsLayer_->addChild(g);
        return areaNodes[id] = g;
    };
    for (size_t s = 0; s < data.scenes.size(); s++) {
        int id = data.scenes[s]->area_id;
        AREA *a = areaOf(id);
        Scene sc;
        sc.label = (a ? std::string(a->AreaName) : std::string("No area")) + " / Scene " + std::to_string(s);
        scenes_.push_back(sc);
        auto layer = r4::scene::make<r4::scene::Switch>();
        layer->setName("Scene " + std::to_string(s));
        areaNode(a ? id : -1)->addChild(layer);
        sceneLayers_.push_back(layer);
    }
    {
        auto layer = r4::scene::make<r4::scene::Switch>();
        layer->setName("no scene");
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
        if (s < 0) {
            partsWithoutScene++;
        } else {
            scenes_[s].parts++;
        }
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
        if (part == player) {
            start = at;
        }
    }
}

void WorldScene::showOverlays(bool on) {
    overlays_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}
void WorldScene::showScene(int scene, bool on) {
    size_t i = scene < 0 ? sceneLayers_.size() - 1 : (size_t)scene;
    if (i < sceneLayers_.size()) {
        sceneLayers_[i]->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
    }
}

void WorldScene::buildTerrain() {
    terrain_->removeAllChildren();
    terrainLods_.clear();
    terrainTriangles = terrainTextures = 0;
    terrainBlocks = 0;
    if (!area_) {
        return;
    }
    r4bridge::TerrainLight light;
    light.dynamic = terrainDynamic_;
    light.ranges = &palette_.ranges;
    light.lighting = lighting;
    light.relief = terrainRelief_;
    auto model = r4bridge::terrainModel(area_.get(), *palette_.GetColorPalette(), kTerrainRanges, light);
    if (!model) {
        return;
    }
    // the triangles of the finest level (each colored triangle once)
    for (const r4::assets::Node &n : model->nodes) {
        if (n.name.compare(0, 4, "LVL0") == 0) {
            for (int mi : n.meshes) {
                if (model->meshes[mi].material == 0) {
                    terrainTriangles += model->meshes[mi].indices.size() / 3;
                }
            }
        }
    }
    terrainTextures = model->images.size();
    r4::scene::NodePtr node = renderer_.upload(*model);
    terrain_->addChild(node);
    // the LOD nodes of the blocks, for setTerrainLod
    std::vector<r4::scene::Node *> stack{node.get()};
    while (!stack.empty()) {
        r4::scene::Node *n = stack.back();
        stack.pop_back();
        if (n->type() == r4::scene::NodeType::LOD) {
            terrainLods_.push_back(static_cast<r4::scene::LOD *>(n));
        }
        for (const auto &c : n->children()) {
            stack.push_back(c.get());
        }
    }
    terrainBlocks = terrainLods_.size();
    setTerrainLod(terrainLod_);
}

void WorldScene::buildForests() {
    forests_->removeAllChildren();
    forestNodes_.clear();
    forestTrees = forestChunks = 0;
    if (!area_ || forestDensity_ <= 0) {
        return;
    }
    forestSettings_ = {};
    forestSettings_.density = forestDensity_;
    forestSettings_.mode = forestMode_;
    forestSettings_.palette = palette_.GetColorPalette();
    // none on the objects of the map and of the mission (R4nkine's (x, y) is
    // libRealSpace's (x, -z))
    for (const auto *list : {&parts_, &mapObjects_}) {
        for (const Item &it : *list) {
            forestSettings_.keepOut.push_back({it.position.x, -it.position.y, std::max(it.radius * 2, 150.0f)});
        }
    }
    std::vector<r4bridge::ForestChunk> chunks = r4bridge::forestChunks(area_.get(), forestSettings_);
    if (chunks.empty()) {
        return;
    }
    // the 2 species, each detailed then simple
    std::vector<r4::scene::InstancePart> parts[r4bridge::TREE_SPECIES_COUNT][2];
    for (int sp = 0; sp < r4bridge::TREE_SPECIES_COUNT; sp++) {
        for (int level = 0; level < 2; level++) {
            auto model = r4bridge::treeModel(sp, level != 0, *palette_.GetColorPalette(), palette_.ranges, lighting);
            parts[sp][level] = r4::scene::collectParts(*renderer_.upload(*model));
        }
    }
    forestNodes_.resize(chunks.size());
    std::map<std::pair<int, int>, std::shared_ptr<r4::scene::Group>> blocks;
    for (size_t i = 0; i < chunks.size(); i++) {
        ForestNode &f = forestNodes_[i];
        f.chunk = std::move(chunks[i]);
        // a chunk is skipped as a whole beyond the trees' distance
        auto lod = r4::scene::make<r4::scene::LOD>();
        lod->setName("trees " + std::to_string(f.chunk.cx) + "," + std::to_string(f.chunk.cz));
        lod->setCenter(f.chunk.center);
        lod->setRanges({0, kTreeFar + f.chunk.radius});
        auto group = r4::scene::make<r4::scene::Group>();
        for (int sp = 0; sp < r4bridge::TREE_SPECIES_COUNT; sp++) {
            f.trees[sp] = r4::scene::make<r4::scene::Instancer>();
            f.trees[sp]->setName(std::string(r4bridge::isConifer(sp) ? "conifers" : "broadleaves") + (r4bridge::isJungleTree(sp) ? " (jungle)" : ""));
            f.trees[sp]->setUp({0, 1, 0}); // libRealSpace's axes
            f.trees[sp]->addLevel(parts[sp][0], kTreeNear);
            f.trees[sp]->addLevel(parts[sp][1], kTreeFar);
            group->addChild(f.trees[sp]);
        }
        // its trees, all planted at the load (about 2 million on TURKEY,
        // 140 MB of matrices)
        r4bridge::plantForest(area_.get(), forestSettings_, f.chunk);
        for (int sp = 0; sp < r4bridge::TREE_SPECIES_COUNT; sp++) {
            forestTrees += f.chunk.trees[sp].size();
            f.trees[sp]->setPlacements(std::move(f.chunk.trees[sp]));
            f.chunk.trees[sp].clear();
        }
        lod->addChild(group);
        // the chunks by block of the terrain (20 km): a few nodes tested
        // against the view, then the chunks of the blocks in it
        int bx = (int)(f.chunk.cx * forestSettings_.chunk / BLOCK_WIDTH);
        int bz = (int)(f.chunk.cz * forestSettings_.chunk / BLOCK_WIDTH);
        auto &block = blocks[{bx, bz}];
        if (!block) {
            block = r4::scene::make<r4::scene::Group>();
            block->setName("trees of block " + std::to_string(bx) + "," + std::to_string(bz));
            forests_->addChild(block);
        }
        block->addChild(lod);
    }
    forestChunks = forestNodes_.size();
}

void WorldScene::setForestMode(r4bridge::ForestMode mode) {
    forestMode_ = mode;
    if (area_) {
        buildForests();
    }
}

void WorldScene::setForestDensity(float density) {
    forestDensity_ = density;
    if (area_) {
        buildForests();
    }
}

void WorldScene::showBuildings(bool on) {
    buildings_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}

void WorldScene::showForests(bool on) {
    forests_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}

void WorldScene::setTerrainLight(bool dynamic, float relief) {
    terrainDynamic_ = dynamic;
    terrainRelief_ = relief;
    if (area_) {
        buildTerrain();
    }
}

bool WorldScene::groundHeight(r4::Vec3 p, float &height) const {
    if (!area_) {
        return false;
    }
    // R4nkine's (x, y) is libRealSpace's (x, -z): r4::axes::toYUp
    const float half = BLOCK_WIDTH * BLOCK_PER_MAP_SIDE_DIV_2 - 1;
    if (std::abs(p.x) >= half || std::abs(p.y) >= half) {
        return false;
    }
    height = area_->getY(p.x, -p.y);
    return true;
}

void WorldScene::setTerrainLod(int mode) {
    terrainLod_ = mode;
    // 0: by distance; 1..3: the level mode - 1 everywhere (the world_detail of the game)
    std::vector<float> ranges = kTerrainRanges;
    if (mode > 0) {
        int level = mode - 1;
        for (int i = 0; i <= 3; i++) {
            ranges[i] = i <= level ? 0.0f : 1e30f;
        }
    }
    for (r4::scene::LOD *lod : terrainLods_) {
        lod->setRanges(ranges);
    }
}

void WorldScene::showTerrain(bool on) {
    terrain_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}
void WorldScene::showMapObjects(bool on) {
    mapLayer_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}
void WorldScene::showParts(bool on) {
    partsLayer_->setValue(on ? r4::scene::Switch::kAll : r4::scene::Switch::kNone);
}
