#include "r4/scene/node.h"

#include "r4/render/renderer.h"

#include <algorithm>
#include <cmath>

namespace r4::scene {

void Node::addChild(NodePtr child) {
    if (!child || child.get() == this)
        return;
    child->parents_.push_back(this);
    children_.push_back(std::move(child));
    markDirty();
}

bool Node::removeChild(const Node *child) {
    auto it = std::find_if(children_.begin(), children_.end(), [&](const NodePtr &c) { return c.get() == child; });
    if (it == children_.end())
        return false;
    auto &ps = (*it)->parents_;
    auto pit = std::find(ps.begin(), ps.end(), this);
    if (pit != ps.end())
        ps.erase(pit);
    children_.erase(it);
    markDirty();
    return true;
}

void Node::removeAllChildren() {
    for (auto &c : children_) {
        auto &ps = c->parents_;
        auto pit = std::find(ps.begin(), ps.end(), this);
        if (pit != ps.end())
            ps.erase(pit);
    }
    children_.clear();
    markDirty();
}

NodePtr Node::findChild(const std::string &name) const {
    for (const NodePtr &c : children_) {
        if (c->name() == name)
            return c;
        if (NodePtr r = c->findChild(name))
            return r;
    }
    return nullptr;
}

void Node::markDirty() {
    // stops at the nodes already marked: their ancestors are too
    if (dirty_)
        return;
    dirty_ = true;
    for (Node *p : parents_)
        p->markDirty();
}

const Sphere &Node::bounds() {
    if (dirty_) {
        Sphere s = contentBounds();
        for (const NodePtr &c : children_)
            s.extend(childBounds(c->bounds()));
        bounds_ = s.transformed(matrix_);
        dirty_ = false;
    }
    return bounds_;
}

Sphere Geode::contentBounds() const {
    Sphere s;
    for (const Drawable &d : drawables_)
        if (d.mesh)
            s.extend(d.mesh->bounds);
    return s;
}

Mat4 Billboard::rotationFor(Vec3 eye) const {
    // -Y turned towards the eye: about Z by atan2(x, -y), then for a point
    // billboard about X by the elevation
    float h = std::atan2(eye.x, -eye.y);
    Mat4 r = Mat4::rotate(h, {0, 0, 1});
    if (mode_ == Mode::Point) {
        float flat = std::sqrt(eye.x * eye.x + eye.y * eye.y);
        float e = std::atan2(eye.z, flat);
        r = r * Mat4::rotate(-e, {1, 0, 0});
    }
    return r;
}

static Sphere aroundOrigin(const Sphere &s) {
    if (s.empty())
        return s;
    return {{0, 0, 0}, length(s.center) + s.radius};
}

Sphere Billboard::contentBounds() const { return aroundOrigin(Geode::contentBounds()); }
Sphere Billboard::childBounds(const Sphere &s) const { return aroundOrigin(s); }

void Instancer::addLevel(std::vector<InstancePart> parts, float maxDistance) {
    Level l;
    l.maxDistance = maxDistance;
    for (const InstancePart &p : parts)
        if (p.drawable.mesh)
            l.bounds.extend(p.drawable.mesh->bounds.transformed(p.local));
    l.parts = std::move(parts);
    levels_.push_back(std::move(l));
    markDirty();
}

int Instancer::select(float distance) const {
    for (size_t i = 0; i < levels_.size(); i++)
        if (distance < levels_[i].maxDistance)
            return (int)i;
    return -1;
}

Sphere Instancer::contentBounds() const {
    Sphere one;
    for (const Level &l : levels_)
        one.extend(l.bounds);
    if (one.empty() || instances_.empty())
        return {};
    // the box of the centers, grown by the largest radius
    Box b;
    float r = 0;
    for (const Mat4 &m : instances_) {
        Sphere s = one.transformed(m);
        b.extend(s.center);
        r = std::max(r, s.radius);
    }
    Sphere s = sphereOf(b);
    s.radius += r;
    return s;
}

static void collect(Node &n, const Mat4 &m, uint32_t state, std::vector<InstancePart> &out, int depth) {
    if (depth > 64 || !(n.mask() & state))
        return;
    state &= n.stateFilter();
    Mat4 here = m * n.matrix();
    if (n.type() == NodeType::Geode || n.type() == NodeType::Billboard)
        for (const Drawable &d : static_cast<Geode &>(n).drawables())
            out.push_back({d, here});
    const auto &kids = n.children();
    if (n.type() == NodeType::Switch) {
        auto &sw = static_cast<Switch &>(n);
        for (size_t i = 0; i < kids.size(); i++)
            if (sw.isActive((int)i))
                collect(*kids[i], here, state, out, depth + 1);
    } else if (n.type() == NodeType::LOD) {
        if (!kids.empty())
            collect(*kids[0], here, state, out, depth + 1);
    } else {
        for (const NodePtr &k : kids)
            collect(*k, here, state, out, depth + 1);
    }
}

std::vector<InstancePart> collectParts(Node &node, uint32_t state) {
    std::vector<InstancePart> out;
    collect(node, Mat4(), state, out, 0);
    return out;
}

int LOD::select(float distance) const {
    for (size_t i = 0; i + 1 < ranges_.size() && i < children().size(); i++)
        if (distance >= ranges_[i] && distance < ranges_[i + 1])
            return (int)i;
    return -1;
}

} // namespace r4::scene
