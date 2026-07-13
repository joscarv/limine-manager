#pragma once

#include "limine_manager/config/config.hpp"
#include "limine_manager/domain/menu.hpp"
#include "limine_manager/infrastructure/snapper_client.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"

namespace limine_manager::application {

class PreviewService {
  public:
    domain::MenuDocument build(const infrastructure::SystemInfo &system,
                               const std::vector<infrastructure::SnapshotInfo> &snapshots,
                               const config::AppConfig &config) const;
};

} // namespace limine_manager::application
