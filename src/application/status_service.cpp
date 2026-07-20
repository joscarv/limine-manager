#include "limine_manager/application/status_service.hpp"

#include <sstream>

namespace limine_manager::application {
namespace {
std::string yes_no(bool value) { return value ? "yes" : "no"; }
std::string path_text(const std::filesystem::path &path) { return path.generic_string(); }
} // namespace

StatusReport StatusService::build(const model::SystemModel &model,
                                  const domain::ValidationReport &validation,
                                  const ChangePlan &plan,
                                  const std::vector<BackupInfo> &backups,
                                  const config::AppConfig &config) const {
    const auto &system = model.system;
    const bool encryption_metadata_incomplete =
        system.root_encrypted && (system.encrypted_backing_device.empty() || system.luks_uuid.empty());
    const bool large_snapshot_menu = model.snapshots.selected.size() > 100;
    const bool degraded = validation.valid() && (encryption_metadata_incomplete || large_snapshot_menu);
    const auto health = !validation.valid() ? "unhealthy" : degraded ? "degraded" : "healthy";
    const auto health_mark = !validation.valid() ? "[FAIL]" : degraded ? "[WARN]" : "[OK]";
    const auto warning_count = validation.warning_count() +
                               (encryption_metadata_incomplete ? 1U : 0U) +
                               (large_snapshot_menu ? 1U : 0U);

    std::ostringstream out;
    out << "limine-manager status\n\n"
        << "Overall\n"
        << "  " << health_mark << " System health: " << health << '\n'
        << "  " << (plan.has_changes() ? "[NOTICE] Configuration: changes pending"
                                         : "[OK] Configuration: synchronized") << '\n'
        << "  Errors: " << validation.error_count() << '\n'
        << "  Warnings: " << warning_count << "\n\n"
        << "System\n"
        << "  OS: " << system.os_name << '\n'
        << "  Root filesystem: " << system.root_fstype << '\n'
        << "  Root device: " << path_text(system.root_source.substr(0, system.root_source.find('['))) << '\n'
        << "  Root subvolume: " << system.root_subvolume << '\n'
        << "  Encrypted: " << yes_no(system.root_encrypted) << '\n';
    if (system.root_encrypted) {
        out << "  Mapper: " << system.root_mapper_name << '\n'
            << "  Backing device: " << system.encrypted_backing_device << '\n'
            << "  LUKS UUID: " << system.luks_uuid << '\n'
            << "  Backing PARTUUID: " << system.encrypted_backing_partuuid << '\n';
    }
    out << "\nBoot\n"
        << "  Mount: " << path_text(system.boot_mount) << '\n'
        << "  Device: " << system.boot_source << " (" << system.boot_fstype << ")\n"
        << "  Limine configuration: " << path_text(system.limine_config) << '\n'
        << "  Command line source: "
        << (system.kernel_cmdline_generated ? "generated" : path_text(system.kernel_cmdline_file))
        << "\n\nKernels\n";
    for (const auto &kernel : system.kernels) {
        out << "  " << (kernel.running ? "*" : "-") << ' ' << kernel.display_name;
        if (!kernel.release.empty())
            out << ' ' << kernel.release;
        out << " [" << (kernel.unified_kernel_image ? "UKI" : "kernel+initramfs") << "]\n"
            << "    " << path_text(kernel.image) << '\n';
    }
    if (system.kernels.empty())
        out << "  none detected\n";
    out << "\nSnapshots\n"
        << "  Snapper config: " << model.snapper.name << " (" << model.snapper.subvolume << ")\n"
        << "  Available snapshots: " << model.snapshots.available.size() << '\n'
        << "  Menu snapshots: " << model.snapshots.selected.size() << '\n'
        << "  Maximum configured: ";
    if (model.snapshots.maximum == 0)
        out << "unlimited\n";
    else
        out << model.snapshots.maximum << '\n';
    if (large_snapshot_menu)
        out << "  [WARNING] Large snapshot menu\n";
    out << "\nConfiguration\n"
        << "  State: " << (plan.has_changes() ? "changes pending" : "synchronized") << '\n'
        << "  Theme: " << config.theme_name << '\n'
        << "  Backups: " << backups.size() << '\n'
        << "  Retention: " << config.backup_retention << '\n';
    if (!backups.empty())
        out << "  Latest backup: " << path_text(backups.front().path) << '\n';

    if (!validation.valid() || encryption_metadata_incomplete || large_snapshot_menu) {
        out << "\nProblems\n";
        if (encryption_metadata_incomplete)
            out << "  [WARNING] encryption.discovery: Encrypted root detected, but backing device metadata is incomplete.\n";
        if (large_snapshot_menu)
            out << "  [WARNING] snapshots.menu: More than 100 selected snapshots will produce a large Limine menu.\n";
        for (const auto &item : validation.diagnostics()) {
            if (item.severity == domain::DiagnosticSeverity::info)
                continue;
            out << "  [" << domain::to_string(item.severity) << "] " << item.code << ": "
                << item.message << '\n';
        }
    }
    return {.text = out.str(),
            .healthy = validation.valid(),
            .degraded = degraded,
            .changes_pending = plan.has_changes()};
}

} // namespace limine_manager::application
