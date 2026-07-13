#pragma once

#include <optional>
#include <string>
#include <vector>

namespace limine_manager::domain {

enum class NodeKind { directory, linux_entry };

struct LinuxBootSpec {
    std::string kernel_path;
    std::vector<std::string> module_paths;
    std::string cmdline;
};

struct MenuNode {
    NodeKind kind{NodeKind::directory};
    std::string title;
    std::optional<std::string> comment;
    bool expanded{false};
    std::optional<LinuxBootSpec> boot;
    std::vector<MenuNode> children;

    static MenuNode directory(std::string title, bool expanded = false);
    static MenuNode linux_entry(std::string title, std::string comment, LinuxBootSpec spec);
    MenuNode &add_child(MenuNode child);
};

struct MenuDocument {
    std::vector<std::pair<std::string, std::string>> global_options;
    std::vector<MenuNode> roots;
};

} // namespace limine_manager::domain
