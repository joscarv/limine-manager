#pragma once

#include "limine_manager/domain/rollback.hpp"
#include "limine_manager/infrastructure/btrfs_client.hpp"

#include <filesystem>
#include <utility>

namespace limine_manager::application {

struct RollbackExecutionOptions {
    std::filesystem::path runtime_directory{"/run/limine-manager"};
};

struct RollbackResult {
    std::filesystem::path mounted_top_level;
    std::string preserved_subvolume;
    std::string active_subvolume;
};

class RollbackService {
  public:
    explicit RollbackService(const infrastructure::BtrfsClient &btrfs,
                             RollbackExecutionOptions options = {})
        : btrfs_(btrfs), options_(std::move(options)) {}
    [[nodiscard]] RollbackResult execute(const domain::RollbackPlan &plan) const;

  private:
    const infrastructure::BtrfsClient &btrfs_;
    RollbackExecutionOptions options_;
};

} // namespace limine_manager::application
