#pragma once

#include "limine_manager/config/config.hpp"
#include "limine_manager/domain/rollback.hpp"
#include "limine_manager/infrastructure/btrfs_client.hpp"
#include "limine_manager/infrastructure/snapper_client.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"

#include <string>
#include <vector>

namespace limine_manager::application {

struct RollbackPlannerOptions {
    std::string transaction_id;
};

class RollbackPlanner {
  public:
    explicit RollbackPlanner(const infrastructure::BtrfsClient &btrfs,
                             RollbackPlannerOptions options = {});
    [[nodiscard]] domain::RollbackPlan
    build(const infrastructure::SystemInfo &system,
          const infrastructure::SnapperConfig &snapper_config,
          const std::vector<infrastructure::SnapshotInfo> &snapshots,
          const config::AppConfig &config) const;

  private:
    const infrastructure::BtrfsClient &btrfs_;
    RollbackPlannerOptions options_;
};

} // namespace limine_manager::application
