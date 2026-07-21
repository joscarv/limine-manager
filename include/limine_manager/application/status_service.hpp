#pragma once

#include "limine_manager/application/backup_service.hpp"
#include "limine_manager/application/change_planner.hpp"
#include "limine_manager/config/config.hpp"
#include "limine_manager/domain/validation.hpp"
#include "limine_manager/model/system_model.hpp"

#include <string>
#include <vector>

namespace limine_manager::application {

struct StatusReport {
    std::string text;
    bool healthy{false};
    bool degraded{false};
    bool changes_pending{false};
};

class StatusService {
  public:
    [[nodiscard]] StatusReport build(const model::SystemModel &model,
                                     const domain::ValidationReport &validation,
                                     const ChangePlan &plan, const std::vector<BackupInfo> &backups,
                                     const config::AppConfig &config) const;
};

} // namespace limine_manager::application
