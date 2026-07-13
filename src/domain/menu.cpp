#include "limine_manager/domain/menu.hpp"

#include <stdexcept>
#include <utility>

namespace limine_manager::domain {

MenuNode MenuNode::directory(std::string title, bool expanded) {
    MenuNode node;
    node.kind = NodeKind::directory;
    node.title = std::move(title);
    node.expanded = expanded;
    return node;
}

MenuNode MenuNode::linux_entry(std::string title, std::string comment, LinuxBootSpec spec) {
    MenuNode node;
    node.kind = NodeKind::linux_entry;
    node.title = std::move(title);
    node.comment = std::move(comment);
    node.boot = std::move(spec);
    return node;
}

MenuNode &MenuNode::add_child(MenuNode child) {
    if (kind != NodeKind::directory) {
        throw std::logic_error("Only directory nodes can contain children");
    }
    children.push_back(std::move(child));
    return *this;
}

} // namespace limine_manager::domain
