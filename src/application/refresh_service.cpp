#include "limine_manager/application/refresh_service.hpp"

#include "limine_manager/application/backup_service.hpp"
#include "limine_manager/application/change_planner.hpp"
#include "limine_manager/application/preview_service.hpp"
#include "limine_manager/application/secure_boot_apply_service.hpp"
#include "limine_manager/application/validation_service.hpp"
#include "limine_manager/render/limine_renderer.hpp"

namespace limine_manager::application {

RefreshResult RefreshService::run(const infrastructure::SystemInfo &system,
                                  const infrastructure::SnapperConfig &snapper_config,
                                  const std::vector<infrastructure::SnapshotInfo> &snapshots,
                                  const config::AppConfig &config) const {
    RefreshResult result;

    ValidationService validation(filesystem_);
    const auto report = validation.validate(system, snapper_config, snapshots, config);
    result.validation_passed = report.valid();
    result.error_count = report.error_count();
    result.warning_count = report.warning_count();
    if (!report.valid())
        return result;

    PreviewService preview;
    render::LimineRenderer renderer;
    const auto generated = renderer.render(preview.build(system, snapshots, config));

    ChangePlanner planner(filesystem_);
    const auto plan = planner.build(system.limine_config, generated);
    result.planned_change = plan.kind;
    if (!plan.has_changes())
        return result;

    if (system.secure_boot.enabled && !config.secure_boot_automatic_apply) {
        result.secure_boot_activation_required = true;
        return result;
    }
    SecureBootApplyService apply_service(runner_);
    result.apply = apply_service.apply(plan, system, config);
    result.changed = result.apply.changed;

    BackupService backup_service;
    result.pruned_backups = backup_service.prune(plan.target, config.backup_retention);
    return result;
}

} // namespace limine_manager::application
