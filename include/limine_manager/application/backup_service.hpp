#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace limine_manager::application {

struct BackupInfo {
    std::filesystem::path path;
    std::uintmax_t size{0};
    std::filesystem::file_time_type modified{};
};

class BackupService {
  public:
    [[nodiscard]] std::vector<BackupInfo> list(const std::filesystem::path &target) const;
    [[nodiscard]] std::optional<BackupInfo> latest(const std::filesystem::path &target) const;
    std::size_t prune(const std::filesystem::path &target, std::size_t retain) const;
};

} // namespace limine_manager::application
