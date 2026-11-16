#include "r4/scene/node.h"

#include "r4/render/renderer.h"

#include <algorithm>

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
            s.extend(c->bounds());
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

int LOD::select(float distance) const {
    for (size_t i = 0; i + 1 < ranges_.size() && i < children().size(); i++)
        if (distance >= ranges_[i] && distance < ranges_[i + 1])
            return (int)i;
    return -1;
}

} // namespace r4::scene
