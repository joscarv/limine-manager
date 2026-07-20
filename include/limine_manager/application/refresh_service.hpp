#pragma once

#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/config/config.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"
#include "limine_manager/infrastructure/snapper_client.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"
#include "limine_manager/infrastructure/process.hpp"

#include <cstddef>

namespace limine_manager::application {

struct RefreshResult {
    bool validation_passed{false};
    bool changed{false};
    bool secure_boot_activation_required{false};
    ChangeKind planned_change{ChangeKind::unchanged};
    std::size_t error_count{0};
    std::size_t warning_count{0};
    ApplyResult apply;
    std::size_t pruned_backups{0};
};

class RefreshService {
  public:
    RefreshService(const infrastructure::FileSystem &filesystem,
                   const infrastructure::ProcessRunner &runner)
        : filesystem_(filesystem), runner_(runner) {}

    [[nodiscard]] RefreshResult run(const infrastructure::SystemInfo &system,
                                    const infrastructure::SnapperConfig &snapper_config,
                                    const std::vector<infrastructure::SnapshotInfo> &snapshots,
                                    const config::AppConfig &config) const;

  private:
    const infrastructure::FileSystem &filesystem_;
    const infrastructure::ProcessRunner &runner_;
};

} // namespace limine_manager::application
