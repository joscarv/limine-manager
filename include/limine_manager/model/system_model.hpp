#pragma once

#include "limine_manager/infrastructure/snapper_client.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"

#include <cstddef>
#include <vector>

namespace limine_manager::model {

struct SnapshotModel {
    std::vector<infrastructure::SnapshotInfo> available;
    std::vector<infrastructure::SnapshotInfo> selected;
    std::size_t maximum{0};
};

// Immutable snapshot of the host state consumed by validation and generation.
// Discovery is the only phase that should execute system inspection commands.
struct SystemModel {
    infrastructure::SystemInfo system;
    infrastructure::SnapperConfig snapper;
    SnapshotModel snapshots;
};

} // namespace limine_manager::model
