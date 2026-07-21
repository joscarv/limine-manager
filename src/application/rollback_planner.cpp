#include "limine_manager/application/rollback_planner.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <unistd.h>

namespace limine_manager::application {
namespace {
void add(domain::RollbackPlan &plan, domain::RollbackSeverity severity, std::string code,
         std::string message) {
    plan.diagnostics.push_back({severity, std::move(code), std::move(message)});
}

bool has_error(const domain::RollbackPlan &plan) {
    return std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &item) {
        return item.severity == domain::RollbackSeverity::error;
    });
}

bool contains_subvolume(const std::vector<infrastructure::BtrfsSubvolume> &subvolumes,
                        const std::string &path) {
    return std::any_of(subvolumes.begin(), subvolumes.end(),
                       [&](const auto &item) { return item.path == path; });
}

std::string snapshot_prefix(std::string value) {
    while (!value.empty() && value.front() == '/')
        value.erase(0, 1);
    while (!value.empty() && value.back() == '/')
        value.pop_back();
    return value;
}

std::optional<unsigned long> parse_snapshot_number(const std::string &current,
                                                   const std::string &snapshots_subvolume) {
    const auto prefix = snapshot_prefix(snapshots_subvolume) + "/";
    const std::string suffix = "/snapshot";
    if (!current.starts_with(prefix) || !current.ends_with(suffix))
        return std::nullopt;
    const auto value =
        current.substr(prefix.size(), current.size() - prefix.size() - suffix.size());
    if (value.empty() || !std::all_of(value.begin(), value.end(),
                                      [](unsigned char ch) { return std::isdigit(ch) != 0; }))
        return std::nullopt;
    return std::stoul(value);
}

std::string rootflags_subvolume(const domain::KernelCommandLine &cmdline) {
    const auto rootflags = cmdline.value("rootflags");
    if (!rootflags)
        return {};
    constexpr std::string_view prefix {"subvol="};
    if (!rootflags->starts_with(prefix))
        return {};
    auto value = rootflags->substr(prefix.size());
    while (!value.empty() && value.front() == '/')
        value.erase(0, 1);
    return value;
}

std::string default_transaction_id() {
    const auto now = std::chrono::system_clock::now();
    const auto ticks =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::ostringstream value;
    value << ticks << '.' << ::getpid();
    return value.str();
}
} // namespace

RollbackPlanner::RollbackPlanner(const infrastructure::BtrfsClient &btrfs,
                                 RollbackPlannerOptions options)
    : btrfs_(btrfs), options_(std::move(options)) {
    if (options_.transaction_id.empty())
        options_.transaction_id = default_transaction_id();
}

domain::RollbackPlan
RollbackPlanner::build(const infrastructure::SystemInfo &system,
                       const infrastructure::SnapperConfig &snapper_config,
                       const std::vector<infrastructure::SnapshotInfo> &snapshots,
                       const config::AppConfig &config) const {
    domain::RollbackPlan plan;
    plan.btrfs_source = infrastructure::normalize_btrfs_source(system.root_source);
    plan.current_subvolume = system.root_subvolume;
    plan.target_subvolume = rootflags_subvolume(system.kernel_cmdline);

    if (system.root_fstype != "btrfs") {
        add(plan, domain::RollbackSeverity::error, "root.filesystem",
            "Rollback requires Btrfs; detected " + system.root_fstype);
    } else {
        add(plan, domain::RollbackSeverity::info, "root.filesystem", "Root filesystem is Btrfs");
    }

    if (plan.target_subvolume.empty()) {
        add(plan, domain::RollbackSeverity::error, "kernel.rootflags",
            "Canonical kernel command line must define rootflags=subvol=<main-root>");
    }

    if (plan.current_subvolume.empty()) {
        add(plan, domain::RollbackSeverity::error, "root.subvolume",
            "Unable to detect the currently booted Btrfs subvolume");
    } else if (!plan.target_subvolume.empty() && plan.current_subvolume == plan.target_subvolume) {
        plan.boot_mode = domain::RollbackBootMode::normal_root;
        add(plan, domain::RollbackSeverity::error, "rollback.boot_mode",
            "Refusing rollback while booted from the normal root subvolume " +
                plan.current_subvolume);
    } else {
        plan.snapshot_number =
            parse_snapshot_number(plan.current_subvolume, config.snapshots_subvolume);
        if (plan.snapshot_number) {
            plan.boot_mode = domain::RollbackBootMode::managed_snapshot;
            plan.source_snapshot_subvolume = plan.current_subvolume;
            add(plan, domain::RollbackSeverity::info, "rollback.boot_mode",
                "Booted from managed snapshot #" + std::to_string(*plan.snapshot_number));
        } else {
            plan.boot_mode = domain::RollbackBootMode::unknown;
            add(plan, domain::RollbackSeverity::error, "rollback.boot_mode",
                "Current root subvolume is not a managed Snapper snapshot: " +
                    plan.current_subvolume);
        }
    }

    if (snapper_config.subvolume != "/") {
        add(plan, domain::RollbackSeverity::error, "snapper.subvolume",
            "Snapper config '" + config.snapper_config + "' manages '" + snapper_config.subvolume +
                "', expected '/'");
    }

    const auto snapshot =
        plan.snapshot_number
            ? std::find_if(snapshots.begin(), snapshots.end(),
                           [&](const auto &item) { return item.number == *plan.snapshot_number; })
            : snapshots.end();
    if (plan.snapshot_number && snapshot == snapshots.end()) {
        add(plan, domain::RollbackSeverity::error, "snapshot.snapper",
            "Snapshot #" + std::to_string(*plan.snapshot_number) +
                " is not reported by the configured Snapper root config");
    } else if (snapshot != snapshots.end()) {
        plan.source_snapshot_read_only = snapshot->read_only;
        if (snapshot->read_only)
            add(plan, domain::RollbackSeverity::info, "snapshot.read_only",
                "Source snapshot is read-only and will not be modified");
        else
            add(plan, domain::RollbackSeverity::warning, "snapshot.read_only",
                "Source snapshot is read-write; rollback will still create a separate writable @");
    }

    std::vector<infrastructure::BtrfsSubvolume> subvolumes;
    if (system.root_fstype == "btrfs") {
        try {
            subvolumes = btrfs_.list_subvolumes("/");
        } catch (const std::exception &error) {
            add(plan, domain::RollbackSeverity::error, "btrfs.subvolumes",
                "Unable to inspect Btrfs subvolumes: " + std::string(error.what()));
        }
    }
    if (!plan.target_subvolume.empty() && !contains_subvolume(subvolumes, plan.target_subvolume)) {
        add(plan, domain::RollbackSeverity::error, "target.exists",
            "Main root subvolume does not exist: " + plan.target_subvolume);
    }
    if (!plan.source_snapshot_subvolume.empty() &&
        !contains_subvolume(subvolumes, plan.source_snapshot_subvolume)) {
        add(plan, domain::RollbackSeverity::error, "snapshot.exists",
            "Source snapshot subvolume does not exist: " + plan.source_snapshot_subvolume);
    }

    if (plan.snapshot_number && !plan.target_subvolume.empty()) {
        plan.preserved_subvolume = plan.target_subvolume + ".limine-manager.rollback." +
                                   std::to_string(*plan.snapshot_number) + "." +
                                   options_.transaction_id;
        plan.replacement_subvolume = plan.target_subvolume + ".limine-manager.new." +
                                     std::to_string(*plan.snapshot_number) + "." +
                                     options_.transaction_id;
        if (contains_subvolume(subvolumes, plan.preserved_subvolume)) {
            add(plan, domain::RollbackSeverity::error, "preserve.conflict",
                "Recovery subvolume already exists: " + plan.preserved_subvolume);
        }
        if (contains_subvolume(subvolumes, plan.replacement_subvolume)) {
            add(plan, domain::RollbackSeverity::error, "replacement.conflict",
                "Temporary replacement subvolume already exists: " + plan.replacement_subvolume);
        }
    }

    plan.eligible = !has_error(plan);
    plan.operations = {"Validate rollback preconditions",
                       "Acquire rollback lock",
                       "Mount Btrfs top-level with subvolid=5",
                       "Create writable replacement from snapshot " +
                           (plan.snapshot_number ? std::to_string(*plan.snapshot_number) : "?"),
                       "Preserve current " + plan.target_subvolume + " as " +
                           plan.preserved_subvolume,
                       "Move writable replacement into " + plan.target_subvolume,
                       "Verify resulting Btrfs topology",
                       "Regenerate limine.conf with the existing apply workflow",
                       "Require reboot"};
    return plan;
}

} // namespace limine_manager::application
