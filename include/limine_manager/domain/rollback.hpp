#pragma once

#include <optional>
#include <string>
#include <vector>

namespace limine_manager::domain {

enum class RollbackBootMode { normal_root, managed_snapshot, unknown };

enum class RollbackSeverity { info, warning, error };

struct RollbackDiagnostic {
    RollbackSeverity severity {RollbackSeverity::info};
    std::string code;
    std::string message;
};

struct RollbackPlan {
    bool eligible {false};
    RollbackBootMode boot_mode {RollbackBootMode::unknown};
    std::string btrfs_source;
    std::string current_subvolume;
    std::string target_subvolume;
    std::string source_snapshot_subvolume;
    std::optional<unsigned long> snapshot_number;
    bool source_snapshot_read_only {false};
    std::string preserved_subvolume;
    std::string replacement_subvolume;
    std::vector<std::string> operations;
    std::vector<RollbackDiagnostic> diagnostics;
};

[[nodiscard]] std::string to_string(RollbackBootMode mode);
[[nodiscard]] std::string to_string(RollbackSeverity severity);

} // namespace limine_manager::domain
