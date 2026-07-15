#pragma once

#include <map>
#include <string>
#include <vector>

namespace limine_manager::config {

struct ThemePreset {
    std::string name;
    std::string display_name;
    std::map<std::string, std::string> options;
};

[[nodiscard]] const ThemePreset *find_theme(std::string name);
[[nodiscard]] std::vector<ThemePreset> available_themes();

} // namespace limine_manager::config
