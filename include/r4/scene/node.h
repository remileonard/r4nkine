// r4/scene/node.h - the scene graph.
//
// The nodes of Performer, in C++: a Group holds children, every node has a
// matrix relative to its parent (identity by default), a Geode holds the
// drawables, a Switch shows some of its children, a LOD chooses one by
// distance, a Billboard turns its drawables to the eye, an Instancer draws
// the same geometry at many places (forests, villages). The bounding spheres
// are kept up to date lazily: a change marks the node and its ancestors, and
// the next cull recomputes what was marked.
#pragma once

#include "r4/math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace r4::render {
struct GpuMesh;
struct MaterialInstance;
} // namespace r4::render

namespace r4::scene {

class Node;
using NodePtr = std::shared_ptr<Node>;

enum class NodeType { Group, Geode, Switch, LOD, Billboard, Instancer };

// a mesh with its material, as the renderer draws them
struct Drawable {
    std::shared_ptr<render::GpuMesh> mesh;
    std::shared_ptr<render::MaterialInstance> material;
};

class Node : public std::enable_shared_from_this<Node> {
public:
    explicit Node(NodeType type = NodeType::Group) : type_(type) {}
    virtual ~Node() = default;

    NodeType type() const { return type_; }
    const std::string &name() const { return name_; }
    void setName(std::string n) { name_ = std::move(n); }

    // the matrix relative to the parent
    const Mat4 &matrix() const { return matrix_; }
    void setMatrix(const Mat4 &m) {
        matrix_ = m;
        markDirty();
    }

    // the children; a node may have several parents (instancing a subgraph)
    const std::vector<NodePtr> &children() const { return children_; }
    void addChild(NodePtr child);
    bool removeChild(const Node *child);
    void removeAllChildren();
    NodePtr findChild(const std::string &name) const; // depth first

    // the bounding sphere in the space of the parent (the matrix applied)
    const Sphere &bounds();

    // the visibility mask: a node is skipped when (mask & state) == 0, the
    // state being the mask of the camera
    uint32_t mask() const { return mask_; }
    void setMask(uint32_t m) { mask_ = m; }
    // the state of the subtree: the state below is (state & filter); so one
    // model can be drawn in its own state (drawobj(obj, state) of libgobj)
    uint32_t stateFilter() const { return filter_; }
    void setStateFilter(uint32_t f) { filter_ = f; }

    // marks the bounds of this node and its ancestors to be computed again
    void markDirty();

protected:
    // the bounds of what the node itself draws, in its own space
    virtual Sphere contentBounds() const { return {}; }
    // the bounds of a child as they count for this node (a billboard turns
    // its children: their bounds must hold for every turn)
    virtual Sphere childBounds(const Sphere &s) const { return s; }

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
    Group() : Node(NodeType::Group) {}
};

class Geode : public Node {
public:
    Geode() : Node(NodeType::Geode) {}
    explicit Geode(NodeType type) : Node(type) {}
    const std::vector<Drawable> &drawables() const { return drawables_; }
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

// shows one child, all of them or none
class Switch : public Node {
public:
    static constexpr int kAll = -1, kNone = -2;
    Switch() : Node(NodeType::Switch) {}
    int value() const { return value_; }
    void setValue(int v) { value_ = v; }
    bool isActive(int child) const { return value_ == kAll || value_ == child; }

private:
    int value_ = kAll;
};

// child i is drawn when the distance of the eye to the center is in
// [range(i), range(i + 1)); one range more than children
class LOD : public Node {
public:
    LOD() : Node(NodeType::LOD) {}
    const std::vector<float> &ranges() const { return ranges_; }
    void setRanges(std::vector<float> r) { ranges_ = std::move(r); }
    // the center of the distances, in the node's space; the bounds center by default
    void setCenter(Vec3 c) {
        center_ = c;
        hasCenter_ = true;
    }
    Vec3 center() { return hasCenter_ ? center_ : bounds().center; }
    // the child for a distance, -1: none
    int select(float distance) const;

private:
    std::vector<float> ranges_;
    Vec3 center_;
    bool hasCenter_ = false;
};

// the drawables (and the children) turned about the origin of the node to
// face the eye, as pfBillboard: the geometry is made facing -Y. Axis: turned
// about Z only (trees, the posts of a city); Point: turned about Z and X so
// that it faces the eye from any direction (lights, sprites)
class Billboard : public Geode {
public:
    enum class Mode { Axis, Point };
    Billboard() : Geode(NodeType::Billboard) {}
    Mode mode() const { return mode_; }
    void setMode(Mode m) { mode_ = m; }
    // the turn for an eye given in the space of the node
    Mat4 rotationFor(Vec3 eye) const;

protected:
    Sphere contentBounds() const override;
    Sphere childBounds(const Sphere &s) const override;

private:
    Mode mode_ = Mode::Axis;
};

// a part of what an instancer draws: a drawable at a place of the instance
struct InstancePart {
    Drawable drawable;
    Mat4 local;
};

// the same geometry at many places, each one culled on its own, with levels
// of detail by distance: level i is used up to its distance. The renderer
// draws the visible instances of each part in one call (GPU instancing).
class Instancer : public Node {
public:
    Instancer() : Node(NodeType::Instancer) {}

    struct Level {
        float maxDistance = 1e30f;
        std::vector<InstancePart> parts;
        Sphere bounds; // in the space of an instance
    };
    // the levels go from the nearest to the farthest
    void addLevel(std::vector<InstancePart> parts, float maxDistance = 1e30f);
    const std::vector<Level> &levels() const { return levels_; }

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
    const std::vector<Mat4> &instances() const { return instances_; }
    // the level for a distance, -1: none (farther than the last)
    int select(float distance) const;

protected:
    Sphere contentBounds() const override;

private:
    std::vector<Level> levels_;
    std::vector<Mat4> instances_;
};

// the drawables under a node with their matrices (the node's own included),
// as the renderer would draw them in the state given: the nodes whose mask
// misses the state are left, the active children of the switches and the
// most detailed child of the LODs are taken. What an Instancer draws of a
// model.
std::vector<InstancePart> collectParts(Node &node, uint32_t state = 0xffffffffu);

template <class T, class... A> std::shared_ptr<T> make(A &&...a) { return std::make_shared<T>(std::forward<A>(a)...); }

} // namespace r4::scene
