#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/application/automation.hpp"
#include "limine_manager/application/backup_service.hpp"
#include "limine_manager/application/change_planner.hpp"
#include "limine_manager/application/discovery_service.hpp"
#include "limine_manager/application/preview_service.hpp"
#include "limine_manager/application/refresh_service.hpp"
#include "limine_manager/application/rollback_planner.hpp"
#include "limine_manager/application/rollback_service.hpp"
#include "limine_manager/application/secure_boot_apply_service.hpp"
#include "limine_manager/application/status_service.hpp"
#include "limine_manager/application/validation_service.hpp"
#include "limine_manager/config/config_loader.hpp"
#include "limine_manager/config/theme.hpp"
#include "limine_manager/infrastructure/btrfs_client.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"
#include "limine_manager/infrastructure/process.hpp"
#include "limine_manager/infrastructure/snapper_client.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"
#include "limine_manager/render/limine_renderer.hpp"
#include "limine_manager/render/unified_diff_renderer.hpp"

#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

namespace {
struct CliOptions {
    std::string command;
    std::optional<std::filesystem::path> config_path;
    std::optional<std::filesystem::path> backup_path;
    std::vector<std::string> extra_args;
    std::string log_format {"text"};
    bool verbose {false};
    bool help {false};
    bool version {false};
};

void usage(std::ostream &output) {
    output << "Usage: limine-manager [OPTIONS] COMMAND\n\n"
              "Generate and safely maintain a Limine menu for Arch Linux and Snapper.\n\n"
              "Commands:\n"
              "  check-config    Validate configuration syntax and schema\n"
              "  validate        Validate the detected system\n"
              "  themes          List built-in visual themes\n"
              "  preview         Render the generated limine.conf\n"
              "  show-config     Print the effective manager configuration\n"
              "  status          Show complete system, boot, kernel, snapshot, and configuration "
              "status\n"
              "  plan            Classify the pending change\n"
              "  diff            Show a unified diff\n"
              "  dry-run         Show the plan and diff without writing\n"
              "  apply           Atomically install the generated configuration\n"
              "  refresh         Validate, regenerate, compare, and apply if changed\n"
              "  request-refresh Request an asynchronous refresh for automation\n"
              "  automation-status Show installed automation state\n"
              "  rollback-status Inspect Btrfs snapshot rollback eligibility\n"
              "  rollback-plan   Show the Btrfs snapshot rollback plan\n"
              "  rollback        Replace the main Btrfs root from the booted snapshot\n"
              "  list-backups    List managed backups\n"
              "  restore         Restore a managed backup\n"
              "  prune-backups   Apply the backup retention policy\n\n"
              "Options:\n"
              "  -h, --help              Show this help\n"
              "  -V, --version           Show version information\n"
              "      --config PATH       Use an alternative configuration\n"
              "      --backup PATH       Select a backup for restore\n"
              "      --verbose           Print additional diagnostics\n"
              "      --log-format FORMAT text or json (diagnostics)\n";
}

std::string json_escape(std::string_view value) {
    std::ostringstream output;
    for (const char c : value) {
        switch (c) {
        case '\\':
            output << "\\\\";
            break;
        case '"':
            output << "\\\"";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            output << c;
            break;
        }
    }
    return output.str();
}

void log_message(const CliOptions &options, std::string_view level, std::string_view event,
                 std::string_view message) {
    if (options.log_format == "json") {
        std::cerr << "{\"level\":\"" << json_escape(level) << "\",\"event\":\""
                  << json_escape(event) << "\",\"message\":\"" << json_escape(message) << "\"}\n";
    } else {
        std::cerr << '[' << level << "] " << message << '\n';
    }
}

std::optional<CliOptions> parse_cli(int argc, char **argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto parse_path = [&](std::optional<std::filesystem::path> &destination) -> bool {
            if (++i >= argc || destination)
                return false;
            destination = std::filesystem::path(argv[i]);
            return true;
        };
        if (arg == "-h" || arg == "--help") {
            options.help = true;
        } else if (arg == "-V" || arg == "--version") {
            options.version = true;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--log-format") {
            if (++i >= argc)
                return std::nullopt;
            options.log_format = argv[i];
        } else if (arg.starts_with("--log-format=")) {
            options.log_format = std::string(arg.substr(13));
        } else if (arg == "--config") {
            if (!parse_path(options.config_path))
                return std::nullopt;
        } else if (arg.starts_with("--config=")) {
            if (options.config_path)
                return std::nullopt;
            options.config_path = std::filesystem::path(std::string(arg.substr(9)));
        } else if (arg == "--backup") {
            if (!parse_path(options.backup_path))
                return std::nullopt;
        } else if (arg.starts_with("--backup=")) {
            if (options.backup_path)
                return std::nullopt;
            options.backup_path = std::filesystem::path(std::string(arg.substr(9)));
        } else if (!arg.empty() && arg.front() == '-') {
            return std::nullopt;
        } else if (options.command.empty()) {
            options.command = std::string(arg);
        } else if (options.command == "request-refresh") {
            options.extra_args.emplace_back(arg);
        } else {
            return std::nullopt;
        }
    }
    if (options.log_format != "text" && options.log_format != "json")
        return std::nullopt;
    if (options.help || options.version)
        return options;
    const bool known = options.command == "check-config" || options.command == "preview" ||
                       options.command == "validate" || options.command == "themes" ||
                       options.command == "show-config" || options.command == "status" ||
                       options.command == "plan" || options.command == "diff" ||
                       options.command == "dry-run" || options.command == "apply" ||
                       options.command == "refresh" || options.command == "request-refresh" ||
                       options.command == "automation-status" ||
                       options.command == "rollback-status" || options.command == "rollback-plan" ||
                       options.command == "rollback" || options.command == "list-backups" ||
                       options.command == "restore" || options.command == "prune-backups";
    if (!known)
        return std::nullopt;
    if (options.backup_path && options.command != "restore")
        return std::nullopt;
    return options;
}

void require_root(std::string_view operation) {
    if (::geteuid() != 0) {
        throw std::runtime_error(std::string(operation) +
                                 " requires root privileges; re-run with sudo");
    }
}

class RefreshLock {
  public:
    explicit RefreshLock(const std::filesystem::path &runtime_directory) {
        std::filesystem::create_directories(runtime_directory);
        path_ = runtime_directory / "refresh.lock";
        fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd_ < 0)
            throw std::runtime_error("cannot open refresh lock: " + path_.string());
        if (::flock(fd_, LOCK_EX | LOCK_NB) < 0) {
            if (errno == EWOULDBLOCK)
                throw std::runtime_error("another limine-manager refresh operation is active");
            throw std::runtime_error("cannot lock refresh: " + path_.string());
        }
    }

    ~RefreshLock() {
        if (fd_ >= 0)
            ::close(fd_);
    }

    RefreshLock(const RefreshLock &) = delete;
    RefreshLock &operator=(const RefreshLock &) = delete;

  private:
    int fd_ {-1};
    std::filesystem::path path_;
};

void print_system_summary(const limine_manager::infrastructure::SystemInfo &system,
                          std::size_t snapshot_count,
                          const limine_manager::config::LoadedConfig &config,
                          const CliOptions &options) {
    if (!options.verbose)
        return;
    if (options.log_format == "json") {
        std::ostringstream message;
        message << "config=" << (config.source ? config.source->string() : "built-in defaults")
                << ", os=" << system.os_name << ", running=" << system.running_kernel_release
                << ", kernels=" << system.kernels.size() << ", snapshots=" << snapshot_count;
        log_message(options, "debug", "system.detected", message.str());
        return;
    }
    std::cerr << "Config:    " << (config.source ? config.source->string() : "built-in defaults")
              << '\n'
              << "Detected:  " << system.os_name << '\n'
              << "Running:   " << system.running_kernel_release << '\n'
              << "Kernels:   " << system.kernels.size() << '\n'
              << "Root:      " << system.root_source << " (" << system.root_fstype
              << ", subvol=" << system.root_subvolume << ")\n"
              << "Boot:      " << system.boot_source << " mounted at " << system.boot_target << " ("
              << system.boot_fstype << ")\n"
              << "Limine:    " << system.limine_config << '\n'
              << "Snapshots: " << snapshot_count << "\n\n";
}

void print_plan(const limine_manager::application::ChangePlan &plan) {
    std::cout << "Target: " << plan.target << '\n'
              << "Action: " << limine_manager::application::to_string(plan.kind) << '\n'
              << "Write operations: 0 (dry-run only)\n";
}

void print_rollback_status(const limine_manager::domain::RollbackPlan &plan) {
    std::cout << "Rollback status\n\n"
              << "Boot mode:          " << limine_manager::domain::to_string(plan.boot_mode) << '\n'
              << "Eligible:           " << (plan.eligible ? "yes" : "no") << '\n'
              << "Btrfs source:       " << plan.btrfs_source << '\n'
              << "Current subvolume:  " << plan.current_subvolume << '\n'
              << "Target subvolume:   " << plan.target_subvolume << '\n';
    if (plan.snapshot_number)
        std::cout << "Snapshot ID:        " << *plan.snapshot_number << '\n';
    if (!plan.source_snapshot_subvolume.empty())
        std::cout << "Source snapshot:    " << plan.source_snapshot_subvolume << '\n';
    std::cout << "\nDiagnostics:\n";
    for (const auto &item : plan.diagnostics) {
        std::cout << '[' << limine_manager::domain::to_string(item.severity) << "] " << item.code
                  << ": " << item.message << '\n';
    }
}

void print_rollback_plan(const limine_manager::domain::RollbackPlan &plan) {
    print_rollback_status(plan);
    std::cout << "\nRollback plan\n\n"
              << "Source snapshot:    "
              << (plan.snapshot_number ? std::to_string(*plan.snapshot_number) : "unknown") << '\n'
              << "Current booted root: " << plan.current_subvolume << '\n'
              << "Target root:         " << plan.target_subvolume << '\n'
              << "Preserved root:      " << plan.preserved_subvolume << '\n'
              << "Replacement root:    " << plan.replacement_subvolume << "\n\n"
              << "Operations:\n";
    for (std::size_t i = 0; i < plan.operations.size(); ++i)
        std::cout << (i + 1) << ". " << plan.operations[i] << '\n';
}
} // namespace

int main(int argc, char **argv) {
    const auto cli = parse_cli(argc, argv);
    if (!cli) {
        usage(std::cerr);
        return 2;
    }
    if (cli->help) {
        usage(std::cout);
        return 0;
    }
    if (cli->version) {
        std::cout << "limine-manager " << LIMINE_MANAGER_VERSION << '\n';
        return 0;
    }

    auto rollback_reporter = [&](std::string_view message) {
        log_message(*cli, "error", "secure_boot.rollback_failed", message);
    };

    try {
        using namespace limine_manager;
        infrastructure::RealFileSystem filesystem;
        config::ConfigLoader loader(filesystem);
        const auto loaded = loader.load(cli->config_path);
        if (cli->command == "show-config") {
            std::cout << loader.render(loaded);
            return 0;
        }
        if (cli->command == "check-config") {
            std::cout << "Configuration valid (schema " << loaded.value.schema_version
                      << "): " << (loaded.source ? loaded.source->string() : "built-in defaults")
                      << "\n";
            return 0;
        }
        if (cli->command == "themes") {
            std::cout << "Built-in themes:\n";
            for (const auto &theme : config::available_themes())
                std::cout << "  " << theme.name << " - " << theme.display_name << '\n';
            return 0;
        }
        if (cli->command == "automation-status") {
            application::RefreshRequestService requests(loaded.value.automation_runtime_directory);
            std::cout << "Automation: "
                      << (loaded.value.automation_enabled ? "enabled" : "disabled") << '\n'
                      << "Snapper integration: "
                      << (loaded.value.automation_snapper ? "enabled" : "disabled") << '\n'
                      << "Pacman hook: "
                      << (loaded.value.automation_pacman ? "enabled" : "disabled") << '\n'
                      << "Debounce: " << loaded.value.automation_debounce_seconds << " second(s)\n"
                      << "Runtime directory: " << requests.runtime_directory() << '\n'
                      << "Pending refresh: " << (requests.pending() ? "yes" : "no") << '\n'
                      << "Refresh service unit: "
                      << (filesystem.exists(
                              "/usr/lib/systemd/system/limine-manager-refresh.service")
                              ? "installed"
                              : "not detected")
                      << '\n'
                      << "Refresh timer unit: "
                      << (filesystem.exists("/usr/lib/systemd/system/limine-manager-refresh.timer")
                              ? "installed"
                              : "not detected")
                      << '\n'
                      << "Snapper plugin: "
                      << (filesystem.exists("/usr/lib/snapper/plugins/90-limine-manager")
                              ? "installed"
                              : "not detected")
                      << '\n'
                      << "Managed pacman hook: "
                      << (filesystem.exists("/etc/pacman.d/hooks/90-limine-manager.hook")
                              ? "present"
                              : "not detected")
                      << '\n';
            return 0;
        }
        if (cli->command == "request-refresh") {
            if (!loaded.value.automation_enabled) {
                log_message(*cli, "info", "refresh.ignored", "automation is disabled");
                return 0;
            }
            const auto source =
                cli->extra_args.empty() ? std::string("manual") : cli->extra_args[0];
            if (source == "snapper") {
                if (!loaded.value.automation_snapper) {
                    log_message(*cli, "info", "refresh.ignored", "snapper automation is disabled");
                    return 0;
                }
                std::vector<std::string> event_args;
                if (cli->extra_args.size() > 1)
                    event_args.assign(cli->extra_args.begin() + 1, cli->extra_args.end());
                const auto event = application::parse_snapper_plugin_event(event_args);
                if (!application::is_relevant_snapper_event(event)) {
                    log_message(*cli, "info", "refresh.ignored", "snapper event is not relevant");
                    return 0;
                }
            } else if (source == "pacman" && !loaded.value.automation_pacman) {
                log_message(*cli, "info", "refresh.ignored", "pacman automation is disabled");
                return 0;
            }

            application::RefreshRequestService requests(loaded.value.automation_runtime_directory);
            const auto request = requests.request(source);
            log_message(*cli, "info", request.coalesced ? "refresh.coalesced" : "refresh.requested",
                        request.coalesced ? "refresh already pending" : "refresh requested");

            infrastructure::PosixProcessRunner runner;
            const auto timer = runner.run({"systemctl", "restart", "limine-manager-refresh.timer"});
            if (timer.exit_code != 0) {
                log_message(*cli, "warning", "refresh.timer",
                            "could not restart limine-manager-refresh.timer; pending marker kept");
            }
            return 0;
        }

        application::BackupService backup_service;
        const auto target = loaded.value.system.limine_config;
        if (cli->command == "list-backups") {
            const auto backups = backup_service.list(target);
            if (backups.empty()) {
                std::cout << "No backups found for " << target << ".\n";
                return 0;
            }
            for (std::size_t i = 0; i < backups.size(); ++i) {
                std::cout << (i + 1) << ". " << backups[i].path << " (" << backups[i].size
                          << " bytes)\n";
            }
            return 0;
        }
        if (cli->command == "prune-backups") {
            require_root("prune-backups");
            const auto removed = backup_service.prune(target, loaded.value.backup_retention);
            std::cout << "Removed " << removed << " old backup(s); retained "
                      << loaded.value.backup_retention << ".\n";
            return 0;
        }
        if (cli->command == "restore") {
            require_root("restore");
            const auto backups = backup_service.list(target);
            if (backups.empty())
                throw std::runtime_error("no backups available for " + target.string());
            std::filesystem::path selected = cli->backup_path.value_or(backups.front().path);
            selected = std::filesystem::weakly_canonical(selected);
            bool managed = false;
            for (const auto &backup : backups) {
                if (std::filesystem::weakly_canonical(backup.path) == selected) {
                    managed = true;
                    break;
                }
            }
            if (!managed)
                throw std::runtime_error("selected file is not a managed backup for " +
                                         target.string());
            application::ChangePlanner planner(filesystem);
            const auto plan = planner.build(target, filesystem.read_text(selected));
            if (!plan.has_changes()) {
                std::cout << "No changes. Backup content is already active.\n";
                return 0;
            }
            application::ApplyService apply_service;
            const auto result = apply_service.apply(plan);
            const auto removed = backup_service.prune(target, loaded.value.backup_retention);
            std::cout << "Restored: " << selected << "\nTarget:   " << result.target << '\n';
            if (!result.backup.empty())
                std::cout << "Previous active configuration saved as: " << result.backup << '\n';
            if (removed)
                std::cout << "Pruned:   " << removed << " old backup(s)\n";
            return 0;
        }

        infrastructure::PosixProcessRunner runner;
        application::DiscoveryService discovery(runner, filesystem);
        infrastructure::PosixBtrfsClient btrfs(runner);
        const auto discovered = discovery.discover(loaded.value);
        const auto &system = discovered.system;
        const auto &snapper_config = discovered.snapper;
        const auto &available_snapshots = discovered.snapshots.available;
        const auto &menu_snapshots = discovered.snapshots.selected;
        print_system_summary(system, menu_snapshots.size(), loaded, *cli);

        if (cli->command == "refresh") {
            require_root("refresh");
            RefreshLock lock(loaded.value.automation_runtime_directory);
            application::RefreshService refresh(filesystem, runner);
            application::RefreshRequestService requests(loaded.value.automation_runtime_directory);
            bool any_changed = false;
            std::filesystem::path latest_target;
            std::filesystem::path latest_backup;
            std::size_t total_pruned = 0;
            do {
                const bool consumed_pending = requests.pending();
                if (consumed_pending)
                    requests.clear_pending();
                const auto refresh_model = discovery.discover(loaded.value);
                log_message(*cli, "info", "refresh.validation", "validation started");
                const auto result = refresh.run(refresh_model.system, refresh_model.snapper,
                                                refresh_model.snapshots.selected, loaded.value);
                if (!result.validation_passed) {
                    if (consumed_pending)
                        (void)requests.request("retry");
                    log_message(*cli, "error", "refresh.validation_failed",
                                "validation failed; configuration was not changed");
                    std::cerr << "Refresh failed: validation found " << result.error_count
                              << " error(s). Run 'limine-manager validate' for details.\n";
                    return 1;
                }
                if (result.secure_boot_activation_required) {
                    if (consumed_pending)
                        (void)requests.request("retry");
                    std::cerr << "Refresh skipped: Secure Boot automatic apply is disabled. "
                                 "Run a successful manual apply, reboot-test it, then set "
                                 "[secure_boot] automatic_apply = true.\n";
                    return 1;
                }
                if (result.changed) {
                    any_changed = true;
                    latest_target = result.apply.target;
                    latest_backup = result.apply.backup;
                    total_pruned += result.pruned_backups;
                    log_message(*cli, "info", "refresh.applied", "changes applied");
                } else {
                    log_message(*cli, "info", "refresh.unchanged", "no changes required");
                }
                if (requests.pending())
                    log_message(*cli, "info", "refresh.coalesced",
                                "another pending refresh will be processed");
            } while (requests.pending());

            if (!any_changed) {
                std::cout << "No changes. " << system.limine_config << " is already up to date.\n";
                return 0;
            }
            std::cout << "Refreshed: " << latest_target << '\n';
            if (!latest_backup.empty())
                std::cout << "Backup:    " << latest_backup << '\n';
            if (total_pruned)
                std::cout << "Pruned:    " << total_pruned << " old backup(s)\n";
            return 0;
        }

        if (cli->command == "rollback-status" || cli->command == "rollback-plan" ||
            cli->command == "rollback") {
            application::RollbackPlanner rollback_planner(btrfs);
            const auto rollback_plan =
                rollback_planner.build(system, snapper_config, available_snapshots, loaded.value);
            if (cli->command == "rollback-status") {
                print_rollback_status(rollback_plan);
                return 0;
            }
            if (cli->command == "rollback-plan") {
                print_rollback_plan(rollback_plan);
                return rollback_plan.eligible ? 0 : 1;
            }

            require_root("rollback");
            if (!rollback_plan.eligible) {
                print_rollback_plan(rollback_plan);
                return 1;
            }
            application::RollbackService rollback_service(btrfs);
            const auto rollback_result = rollback_service.execute(rollback_plan);

            application::PreviewService preview;
            render::LimineRenderer renderer;
            const auto generated =
                renderer.render(preview.build(system, menu_snapshots, loaded.value));
            application::ChangePlanner planner(filesystem);
            const auto plan = planner.build(system.limine_config, generated);
            application::SecureBootApplyService apply_service(runner, rollback_reporter);
            const auto result = apply_service.apply(plan, system, loaded.value);
            const auto removed =
                backup_service.prune(system.limine_config, loaded.value.backup_retention);

            std::cout << "Rollback completed.\n"
                      << "Active root:      " << rollback_result.active_subvolume << '\n'
                      << "Preserved root:   " << rollback_result.preserved_subvolume << '\n';
            if (result.changed) {
                std::cout << "Limine updated:   " << result.target << '\n';
                if (!result.backup.empty())
                    std::cout << "Limine backup:    " << result.backup << '\n';
            } else {
                std::cout << "Limine updated:   no changes required\n";
            }
            if (removed)
                std::cout << "Pruned backups:   " << removed << '\n';
            std::cout << "Reboot required:  yes\n";
            return 0;
        }

        application::ValidationService validation_service(filesystem);
        const auto report =
            validation_service.validate(system, snapper_config, menu_snapshots, loaded.value);
        if (cli->command == "validate") {
            for (const auto &item : report.diagnostics()) {
                std::cout << '[' << domain::to_string(item.severity) << "] " << item.code << ": "
                          << item.message << '\n';
            }
            std::cout << "\nValidation " << (report.valid() ? "passed" : "failed") << ": "
                      << report.error_count() << " error(s), " << report.warning_count()
                      << " warning(s).\n";
            return report.valid() ? 0 : 1;
        }
        application::PreviewService preview;
        render::LimineRenderer renderer;
        const auto generated = renderer.render(preview.build(system, menu_snapshots, loaded.value));

        if (cli->command == "status") {
            application::ChangePlanner status_planner(filesystem);
            const auto status_plan = status_planner.build(system.limine_config, generated);
            const auto backups = backup_service.list(status_plan.target);
            application::StatusService status_service;
            const auto status =
                status_service.build(discovered, report, status_plan, backups, loaded.value);
            std::cout << status.text;
            if (!status.healthy)
                return 1;
            if (status.degraded)
                return 2;
            return status.changes_pending ? 3 : 0;
        }

        if (!report.valid()) {
            std::cerr << "Generation aborted because validation found " << report.error_count()
                      << " error(s). Run 'limine-manager validate' for details.\n";
            return 1;
        }
        if (cli->command == "preview") {
            std::cout << generated;
            return 0;
        }

        application::ChangePlanner planner(filesystem);
        const auto plan = planner.build(system.limine_config, generated);
        if (cli->command == "plan") {
            print_plan(plan);
            return plan.has_changes() ? 3 : 0;
        }

        render::UnifiedDiffRenderer diff_renderer;
        if (cli->command == "diff") {
            if (!plan.has_changes()) {
                std::cout << "No changes.\n";
                return 0;
            }
            std::cout << diff_renderer.render(plan);
            return 3;
        }
        if (cli->command == "dry-run") {
            print_plan(plan);
            if (plan.has_changes())
                std::cout << '\n' << diff_renderer.render(plan);
            else
                std::cout << "No changes would be applied.\n";
            return plan.has_changes() ? 3 : 0;
        }

        if (!plan.has_changes()) {
            std::cout << "No changes. " << plan.target << " is already up to date.\n";
            return 0;
        }
        require_root("apply");
        application::SecureBootApplyService apply_service(runner, rollback_reporter);
        const auto result = apply_service.apply(plan, system, loaded.value);
        const auto removed = backup_service.prune(plan.target, loaded.value.backup_retention);
        std::cout << "Applied: " << result.target << '\n';
        if (!result.backup.empty())
            std::cout << "Backup:  " << result.backup << '\n';
        else
            std::cout << "Backup:  not required (new file)\n";
        if (removed)
            std::cout << "Pruned:  " << removed << " old backup(s)\n";
        return 0;
    } catch (const std::exception &error) {
        log_message(*cli, "error", "command.failed", error.what());
        return 1;
    }
}
