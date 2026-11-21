#include "r4/scene/node.h"

#include "r4/axes.h"

#include "r4/render/renderer.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace r4::scene {

void Node::addChild(NodePtr child) {
    if (!child || child.get() == this) {
        return;
    }
    child->parents_.push_back(this);
    children_.push_back(std::move(child));
    markDirty();
}

bool Node::removeChild(const Node *child) {
    auto it = std::find_if(children_.begin(), children_.end(), [&](const NodePtr &c) {
        return c.get() == child;
    });
    if (it == children_.end()) {
        return false;
    }
    auto &ps = (*it)->parents_;
    auto pit = std::find(ps.begin(), ps.end(), this);
    if (pit != ps.end()) {
        ps.erase(pit);
    }
    children_.erase(it);
    markDirty();
    return true;
}

void Node::removeAllChildren() {
    for (auto &c : children_) {
        auto &ps = c->parents_;
        auto pit = std::find(ps.begin(), ps.end(), this);
        if (pit != ps.end()) {
            ps.erase(pit);
        }
    }
    children_.clear();
    markDirty();
}

NodePtr Node::findChild(const std::string &name) const {
    for (const NodePtr &c : children_) {
        if (c->name() == name) {
            return c;
        }
        if (NodePtr r = c->findChild(name)) {
            return r;
        }
    }
    return nullptr;
}

void Node::markDirty() {
    if (dirty_) {
        return;
    }
    dirty_ = true;
    for (Node *p : parents_) {
        p->markDirty();
    }
}

const Sphere &Node::bounds() {
    if (dirty_) {
        Sphere s = contentBounds();
        for (const NodePtr &c : children_) {
            s.extend(childBounds(c->bounds()));
        }
        bounds_ = s.transformed(matrix_);
        dirty_ = false;
    }
    return bounds_;
}

Sphere Geode::contentBounds() const {
    Sphere s;
    for (const Drawable &d : drawables_) {
        if (d.mesh) {
            s.extend(d.mesh->bounds);
        }
    }
    return s;
}

Mat4 Billboard::rotationFor(Vec3 eye) const {
    float x = dot(eye, axes::kRight);
    float y = dot(eye, axes::kForward);
    float z = axes::heightOf(eye);
    float h = std::atan2(x, -y);
    Mat4 r = rotate(h, axes::kUp);
    if (mode_ == Mode::Point) {
        float flat = std::sqrt(x * x + y * y);
        float e = std::atan2(z, flat);
        r = r * rotate(-e, axes::kRight);
    }
    return r;
}

static Sphere aroundOrigin(const Sphere &s) {
    if (s.empty()) {
        return s;
    }
    return {{0, 0, 0}, length(s.center) + s.radius};
}

Sphere Billboard::contentBounds() const {
    return aroundOrigin(Geode::contentBounds());
}
Sphere Billboard::childBounds(const Sphere &s) const {
    return aroundOrigin(s);
}

void Instancer::addLevel(std::vector<InstancePart> parts, float maxDistance) {
    Level l;
    l.maxDistance = maxDistance;
    for (const InstancePart &p : parts) {
        if (p.drawable.mesh) {
            l.bounds.extend(p.drawable.mesh->bounds.transformed(p.local));
        }
    }
    l.parts = std::move(parts);
    levels_.push_back(std::move(l));
    markDirty();
}

int Instancer::select(float distance) const {
    for (size_t i = 0; i < levels_.size(); i++) {
        if (distance < levels_[i].maxDistance) {
            return (int)i;
        }
    }
    return -1;
}

void Instancer::placementsChanged() {
    cellsMade_ = false;
    version_++;
    placementBox_ = Box();
    maxPlacementScale_ = 0;
    for (const Placement &p : placements_) {
        placementBox_.extend(p.position);
        maxPlacementScale_ = std::max({maxPlacementScale_, p.scale, p.scaleUp});
    }
    if (static_) {
        // a fixed random order: the first ones are spread over the whole instancer
        std::mt19937 random(0x52344e4bu);
        std::shuffle(placements_.begin(), placements_.end(), random);
    }
    markDirty();
}

void Instancer::setStatic(float thinFrom) {
    static_ = true;
    thinFrom_ = thinFrom;
    placementsChanged();
}

const std::vector<Instancer::Cell> &Instancer::cells() {
    if (cellsMade_) {
        return cells_;
    }
    cellsMade_ = true;
    cells_.clear();
    // the two axes across the up axis
    int u = 0;
    for (int i = 1; i < 3; i++) {
        if (std::abs(up_[i]) > std::abs(up_[u])) {
            u = i;
        }
    }
    const int a = (u + 1) % 3;
    const int b = (u + 2) % 3;
    auto cellOf = [&](const Placement &p) {
        return std::make_pair((int)std::floor(p.position[a] / kCellSize), (int)std::floor(p.position[b] / kCellSize));
    };
    std::sort(placements_.begin(), placements_.end(), [&](const Placement &x, const Placement &y) {
        return cellOf(x) < cellOf(y);
    });
    for (uint32_t i = 0; i < (uint32_t)placements_.size(); i++) {
        const Placement &p = placements_[i];
        if (cells_.empty() || cellOf(placements_[cells_.back().begin]) != cellOf(p)) {
            Cell c;
            c.begin = i;
            cells_.push_back(c);
        }
        Cell &c = cells_.back();
        c.box.extend(p.position);
        c.maxScale = std::max({c.maxScale, p.scale, p.scaleUp});
        c.end = i + 1;
    }
    return cells_;
}

Mat4 Instancer::matrixOf(const Placement &p) const {
    Mat4 s(1.0f);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            s[j][i] = (i == j ? p.scale : 0.0f) + (p.scaleUp - p.scale) * up_[i] * up_[j];
        }
    }
    return translate(p.position) * rotate(p.heading, up_) * s;
}

Sphere Instancer::contentBounds() const {
    Sphere one;
    for (const Level &l : levels_) {
        one.extend(l.bounds);
    }
    if (one.empty() || count() == 0) {
        return {};
    }
    Box b;
    float r = 0;
    for (const Mat4 &m : instances_) {
        Sphere s = one.transformed(m);
        b.extend(s.center);
        r = std::max(r, s.radius);
    }
    for (const Placement &p : placements_) {
        Sphere s = one.transformed(matrixOf(p));
        b.extend(s.center);
        r = std::max(r, s.radius);
    }
    Sphere s = sphereOf(b);
    s.radius += r;
    return s;
}

static void collect(Node &n, const Mat4 &m, uint32_t state, std::vector<InstancePart> &out, int depth) {
    if (depth > 64 || !(n.mask() & state)) {
        return;
    }
    state &= n.stateFilter();
    Mat4 here = m * n.matrix();
    if (n.type() == NodeType::GEODE || n.type() == NodeType::BILLBOARD) {
        for (const Drawable &d : static_cast<Geode &>(n).drawables()) {
            out.push_back({d, here});
        }
    }
    const auto &kids = n.children();
    if (n.type() == NodeType::SWITCH) {
        auto &sw = static_cast<Switch &>(n);
        for (size_t i = 0; i < kids.size(); i++) {
            if (sw.isActive((int)i)) {
                collect(*kids[i], here, state, out, depth + 1);
            }
        }
    } else if (n.type() == NodeType::LOD) {
        if (!kids.empty()) {
            collect(*kids[0], here, state, out, depth + 1);
        }
    } else {
        for (const NodePtr &k : kids) {
            collect(*k, here, state, out, depth + 1);
        }
    }
}

std::vector<InstancePart> collectParts(Node &node, uint32_t state) {
    std::vector<InstancePart> out;
    collect(node, Mat4(), state, out, 0);
    return out;
}

int LOD::select(float distance) const {
    for (size_t i = 0; i + 1 < ranges_.size() && i < children().size(); i++) {
        if (distance >= ranges_[i] && distance < ranges_[i + 1]) {
            return (int)i;
        }
    }
    return -1;
}

void Trigger::setSphere(float radius) {
    shape_ = Shape::SPHERE;
    radius_ = radius;
}

void Trigger::setCylinder(float radius, float height) {
    shape_ = Shape::CYLINDER;
    radius_ = radius;
    height_ = height;
}

void Trigger::setBox(const Box &box) {
    shape_ = Shape::BOX;
    box_ = box;
}

bool Trigger::contains(Vec3 p) const {
    switch (shape_) {
    case Shape::SPHERE:
        return length(p) <= radius_;
    case Shape::CYLINDER:
        return p.z >= 0 && p.z <= height_ && length(Vec2(p.x, p.y)) <= radius_;
    case Shape::BOX:
        return p.x >= box_.min.x && p.x <= box_.max.x && p.y >= box_.min.y && p.y <= box_.max.y && p.z >= box_.min.z && p.z <= box_.max.z;
    }
    return false;
}

void Trigger::update(const std::vector<Tracker> &trackers, const Mat4 &toLocal) {
    std::unordered_set<uint32_t> now;
    for (const Tracker &t : trackers) {
        if (contains(transformPoint(toLocal, t.position))) {
            now.insert(t.id);
        }
    }
    for (uint32_t id : now) {
        if (!inside_.count(id) && onEnter) {
            onEnter(id);
        }
    }
    for (uint32_t id : inside_) {
        if (!now.count(id) && onLeave) {
            onLeave(id);
        }
    }
    inside_ = std::move(now);
}

namespace {

void visitTriggers(Node &node, const Mat4 &parent, const std::vector<Tracker> &trackers, uint32_t mask) {
    if ((node.mask() & mask) == 0) {
        return;
    }
    const Mat4 world = parent * node.matrix();
    if (node.type() == NodeType::TRIGGER) {
        static_cast<Trigger &>(node).update(trackers, inverse(world));
    }
    for (const NodePtr &c : node.children()) {
        visitTriggers(*c, world, trackers, mask);
    }
}

} // namespace

void checkTriggers(Node &root, const std::vector<Tracker> &trackers, uint32_t mask) {
    visitTriggers(root, Mat4(1.0f), trackers, mask);
}

void Node::setAppCallbacks(AppCallback before, AppCallback after) {
    if (!before && !after) {
        app_.reset();
        return;
    }
    app_ = std::make_unique<AppCallbacks>();
    app_->before = std::move(before);
    app_->after = std::move(after);
}

void app(Node &root, uint32_t mask) {
    if ((root.appMask() & mask) == 0) {
        return;
    }
    if (root.app_ && root.app_->before) {
        root.app_->before(root);
    }
    // by index: a callback below can change this list
    for (size_t i = 0; i < root.children().size(); i++) {
        NodePtr child = root.children()[i];
        app(*child, mask);
    }
    if (root.app_ && root.app_->after) {
        root.app_->after(root);
    }
}

} // namespace r4::scene
