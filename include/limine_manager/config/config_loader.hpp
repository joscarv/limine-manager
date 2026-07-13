#pragma once

#include "limine_manager/config/config.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace limine_manager::config {

class ConfigLoader {
  public:
    ConfigLoader(const infrastructure::FileSystem &filesystem,
                 std::filesystem::path default_path = "/etc/limine-manager/limine-manager.conf")
        : filesystem_(filesystem), default_path_(std::move(default_path)) {}

    [[nodiscard]] LoadedConfig
    load(const std::optional<std::filesystem::path> &explicit_path = std::nullopt) const;
    [[nodiscard]] std::string render(const LoadedConfig &config) const;

  private:
    const infrastructure::FileSystem &filesystem_;
    std::filesystem::path default_path_;
};

} // namespace limine_manager::config
