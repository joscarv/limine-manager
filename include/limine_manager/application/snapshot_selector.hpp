#pragma once

#include "limine_manager/config/config.hpp"
#include "limine_manager/infrastructure/snapper_client.hpp"

#include <algorithm>
#include <vector>

namespace limine_manager::application {

inline std::vector<infrastructure::SnapshotInfo>
select_menu_snapshots(std::vector<infrastructure::SnapshotInfo> snapshots,
                      const config::AppConfig &config) {
    std::sort(snapshots.begin(), snapshots.end(),
              [](const auto &lhs, const auto &rhs) { return lhs.number > rhs.number; });

    std::vector<infrastructure::SnapshotInfo> selected;
    selected.reserve(snapshots.size());
    for (const auto &snapshot : snapshots) {
        if (!snapshot.read_only && !config.include_read_write_snapshots)
            continue;
        if (config.max_snapshots != 0 && selected.size() >= config.max_snapshots)
            break;
        selected.push_back(snapshot);
    }
    return selected;
}

} // namespace limine_manager::application
