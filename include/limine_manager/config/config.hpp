#pragma once

#include "limine_manager/infrastructure/system_detector.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace limine_manager::config {

struct AppConfig {
    std::size_t schema_version{1};
    infrastructure::SystemProfile system;
    std::string snapper_config{"root"};
    std::string snapshots_subvolume{"@snapshots"};
    std::filesystem::path snapshots_directory{"/.snapshots"};
    std::size_t max_snapshots{0};
    bool include_read_write_snapshots{false};

    std::size_t backup_retention{5};

    std::string root_menu_title{"Arch Linux"};
    std::string snapshots_menu_title{"Snapshots"};
    bool root_menu_expanded{true};
    bool snapshots_menu_expanded{false};

    std::vector<std::string> include_kernels;
    std::vector<std::string> exclude_kernels;
    std::vector<std::string> kernel_order;

    std::map<std::string, std::string> limine_options{{"remember_last_entry", "yes"},
                                                      {"timeout", "5"}};
};

struct LoadedConfig {
    AppConfig value;
    std::optional<std::filesystem::path> source;
};

} // namespace limine_manager::config
