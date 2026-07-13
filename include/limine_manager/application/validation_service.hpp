#pragma once

#include "limine_manager/config/config.hpp"
#include "limine_manager/domain/validation.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"
#include "limine_manager/infrastructure/snapper_client.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"

namespace limine_manager::application {

class ValidationService {
  public:
    explicit ValidationService(const infrastructure::FileSystem &filesystem)
        : filesystem_(filesystem) {}
    [[nodiscard]] domain::ValidationReport
    validate(const infrastructure::SystemInfo &system,
             const infrastructure::SnapperConfig &snapper_config,
             const std::vector<infrastructure::SnapshotInfo> &snapshots,
             const config::AppConfig &config) const;

  private:
    const infrastructure::FileSystem &filesystem_;
};

} // namespace limine_manager::application
