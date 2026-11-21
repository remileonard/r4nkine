// r4/scene/node.h - the scene graph, after the nodes of Performer. Bounds are computed again lazily, after a change marks the node and its ancestors.
#pragma once

#include "r4/axes.h"
#include "r4/math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace r4::render {
struct GpuMesh;
struct MaterialInstance;
} // namespace r4::render

namespace r4::scene {

class Node;
using NodePtr = std::shared_ptr<Node>;

enum class NodeType {
    GROUP,
    GEODE,
    SWITCH,
    LOD,
    BILLBOARD,
    INSTANCER,
    TRIGGER
};

struct Drawable {
    std::shared_ptr<render::GpuMesh> mesh;
    std::shared_ptr<render::MaterialInstance> material;
};

class Node : public std::enable_shared_from_this<Node> {
public:
    explicit Node(NodeType type = NodeType::GROUP) : type_(type) {
    }
    virtual ~Node() = default;

    NodeType type() const {
        return type_;
    }
    const std::string &name() const {
        return name_;
    }
    void setName(std::string n) {
        name_ = std::move(n);
    }

    const Mat4 &matrix() const {
        return matrix_;
    }
    void setMatrix(const Mat4 &m) {
        matrix_ = m;
        markDirty();
    }

    // a node may have several parents
    const std::vector<NodePtr> &children() const {
        return children_;
    }
    void addChild(NodePtr child);
    bool removeChild(const Node *child);
    void removeAllChildren();
    NodePtr findChild(const std::string &name) const; // depth first

    // in the space of the parent
    const Sphere &bounds();

    // skipped when (mask & state) == 0, state being the mask of the camera
    uint32_t mask() const {
        return mask_;
    }
    void setMask(uint32_t m) {
        mask_ = m;
    }
    // the state below is (state & filter)
    uint32_t stateFilter() const {
        return filter_;
    }
    void setStateFilter(uint32_t f) {
        filter_ = f;
    }

    void markDirty();

protected:
    // in the node's own space
    virtual Sphere contentBounds() const {
        return {};
    }
    // a billboard turns its children: their bounds hold for every turn
    virtual Sphere childBounds(const Sphere &s) const {
        return s;
    }

private:
    NodeType type_;
    std::string name_;
    Mat4 matrix_;
    std::vector<NodePtr> children_;
    std::vector<Node *> parents_;
    Sphere bounds_;
    bool dirty_ = true;
    uint32_t mask_ = 0xffffffffu;
    uint32_t filter_ = 0xffffffffu;
};

class Group : public Node {
public:
    Group() : Node(NodeType::GROUP) {
    }
};

class Geode : public Node {
public:
    Geode() : Node(NodeType::GEODE) {
    }
    explicit Geode(NodeType type) : Node(type) {
    }
    const std::vector<Drawable> &drawables() const {
        return drawables_;
    }
    void addDrawable(Drawable d) {
        drawables_.push_back(std::move(d));
        markDirty();
    }
    void clearDrawables() {
        drawables_.clear();
        markDirty();
    }

protected:
    Sphere contentBounds() const override;

private:
    std::vector<Drawable> drawables_;
};

class Switch : public Node {
public:
    static constexpr int kAll = -1;
    static constexpr int kNone = -2;
    Switch() : Node(NodeType::SWITCH) {
    }
    int value() const {
        return value_;
    }
    void setValue(int v) {
        value_ = v;
    }
    bool isActive(int child) const {
        return value_ == kAll || value_ == child;
    }

private:
    int value_ = kAll;
};

// child i drawn when the distance of the eye is in [range(i), range(i + 1)): one range more than children
class LOD : public Node {
public:
    LOD() : Node(NodeType::LOD) {
    }
    const std::vector<float> &ranges() const {
        return ranges_;
    }
    void setRanges(std::vector<float> r) {
        ranges_ = std::move(r);
    }
    // the bounds center by default
    void setCenter(Vec3 c) {
        center_ = c;
        hasCenter_ = true;
    }
    Vec3 center() {
        return hasCenter_ ? center_ : bounds().center;
    }
    // -1: none
    int select(float distance) const;

private:
    std::vector<float> ranges_;
    Vec3 center_;
    bool hasCenter_ = false;
};

// turned about the origin of the node to face the eye, the geometry made facing -Y: about Z (AXIS) or about Z and X (POINT)
class Billboard : public Geode {
public:
    enum class Mode {
        Axis,
        Point
    };
    Billboard() : Geode(NodeType::BILLBOARD) {
    }
    Mode mode() const {
        return mode_;
    }
    void setMode(Mode m) {
        mode_ = m;
    }
    // the eye in the space of the node
    Mat4 rotationFor(Vec3 eye) const;

protected:
    Sphere contentBounds() const override;
    Sphere childBounds(const Sphere &s) const override;

private:
    Mode mode_ = Mode::Axis;
};

struct InstancePart {
    Drawable drawable;
    Mat4 local;
};

// each instance culled on its own; level i used up to its distance; the visible instances of a part drawn in one call
// an instance standing on the ground, its matrix made when it is visited: 24 bytes instead of 64
struct Placement {
    Vec3 position;
    float heading = 0; // radians, about the up axis
    float scale = 1;
    float scaleUp = 1;
};

class Instancer : public Node {
public:
    Instancer() : Node(NodeType::INSTANCER) {
    }

    struct Level {
        float maxDistance = 1e30f;
        std::vector<InstancePart> parts;
        Sphere bounds; // in the space of an instance
    };
    // from the nearest to the farthest
    void addLevel(std::vector<InstancePart> parts, float maxDistance = 1e30f);
    const std::vector<Level> &levels() const {
        return levels_;
    }

    void addInstance(const Mat4 &m) {
        instances_.push_back(m);
        markDirty();
    }
    void setInstances(std::vector<Mat4> m) {
        instances_ = std::move(m);
        markDirty();
    }
    void clearInstances() {
        instances_.clear();
        markDirty();
    }
    const std::vector<Mat4> &instances() const {
        return instances_;
    }
    // the up axis of the placements: R4nkine's by default, (0, 1, 0) in a subgraph of Y-up data
    void setPlacements(std::vector<Placement> p) {
        placements_ = std::move(p);
        markDirty();
    }
    const std::vector<Placement> &placements() const {
        return placements_;
    }
    void setUp(Vec3 up) {
        up_ = normalize(up);
        markDirty();
    }
    Vec3 up() const {
        return up_;
    }
    Mat4 matrixOf(const Placement &p) const;
    size_t count() const {
        return instances_.size() + placements_.size();
    }
    // -1: farther than the last
    int select(float distance) const;

protected:
    Sphere contentBounds() const override;

private:
    std::vector<Level> levels_;
    std::vector<Mat4> instances_;
    std::vector<Placement> placements_;
    Vec3 up_ = axes::kUp;
};

// a point that the triggers follow: its id and its position in the space of the parent of the root of checkTriggers
struct Tracker {
    uint32_t id = 0;
    Vec3 position;
};

// A volume that tells when a tracked point goes in or out. It is not drawn and has no bounds. In its own space, Z up:
// a sphere about the origin, a cylinder about the Z axis from 0 to height, or a box.
class Trigger : public Node {
public:
    enum class Shape {
        SPHERE,
        CYLINDER,
        BOX
    };
    Trigger() : Node(NodeType::TRIGGER) {
    }
    void setSphere(float radius);
    void setCylinder(float radius, float height);
    void setBox(const Box &box);
    Shape shape() const {
        return shape_;
    }
    // a point in the space of the node
    bool contains(Vec3 p) const;
    // the trackers inside, by their id
    const std::unordered_set<uint32_t> &inside() const {
        return inside_;
    }
    // the trackers now, toLocal from their space to the one of the node: onEnter and onLeave for the changes
    void update(const std::vector<Tracker> &trackers, const Mat4 &toLocal);
    std::function<void(uint32_t)> onEnter;
    std::function<void(uint32_t)> onLeave;

private:
    Shape shape_ = Shape::SPHERE;
    float radius_ = 0;
    float height_ = 0;
    Box box_;
    std::unordered_set<uint32_t> inside_;
};

// the APP check of the triggers below root, in the nodes whose mask has a bit of mask: onEnter for a tracker that comes in,
// onLeave for one that goes out or is not in the list any more
void checkTriggers(Node &root, const std::vector<Tracker> &trackers, uint32_t mask = 0xffffffffu);

// the drawables under a node with their matrices, as the renderer draws them in this state: what an Instancer draws of a model
std::vector<InstancePart> collectParts(Node &node, uint32_t state = 0xffffffffu);

template <class T, class... A> std::shared_ptr<T> make(A &&...a) {
    return std::make_shared<T>(std::forward<A>(a)...);
}

} // namespace r4::scene
