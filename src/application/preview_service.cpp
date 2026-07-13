#include "limine_manager/application/preview_service.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace limine_manager::application {
namespace {
std::string limine_path(const std::filesystem::path& absolute_boot_path) {
    return "boot():/" + absolute_boot_path.filename().string();
}

std::string snapshot_cmdline(const domain::KernelCommandLine& cmdline,
                             const std::string& snapshots_subvolume,
                             unsigned long number) {
    auto snapshot = cmdline;
    snapshot.set("rootflags", "subvol=" + snapshots_subvolume + "/" + std::to_string(number) + "/snapshot");
    return snapshot.render();
}

std::string display_date(std::string date) {
    if (date.size() >= 16) return date.substr(0, 16);
    return date;
}

std::vector<std::string> modules_for(const infrastructure::KernelInstallation& kernel) {
    std::vector<std::string> modules;
    modules.reserve(kernel.initrds.size());
    for (const auto& initrd : kernel.initrds) modules.push_back(limine_path(initrd));
    return modules;
}

std::string kernel_comment(const infrastructure::KernelInstallation& kernel) {
    if (!kernel.release.empty()) return "Kernel " + kernel.release;
    return "Kernel release unavailable (" + kernel.package_base + ')';
}

std::vector<infrastructure::KernelInstallation> select_kernels(
    const std::vector<infrastructure::KernelInstallation>& kernels,
    const config::AppConfig& config) {
    const std::unordered_set<std::string> included(config.include_kernels.begin(), config.include_kernels.end());
    const std::unordered_set<std::string> excluded(config.exclude_kernels.begin(), config.exclude_kernels.end());
    std::vector<infrastructure::KernelInstallation> selected;
    for (const auto& kernel : kernels) {
        if (!included.empty() && !included.contains(kernel.package_base)) continue;
        if (excluded.contains(kernel.package_base)) continue;
        selected.push_back(kernel);
    }
    std::unordered_map<std::string, std::size_t> rank;
    for (std::size_t i = 0; i < config.kernel_order.size(); ++i) rank.emplace(config.kernel_order[i], i);
    std::stable_sort(selected.begin(), selected.end(), [&](const auto& lhs, const auto& rhs) {
        const auto li = rank.find(lhs.package_base);
        const auto ri = rank.find(rhs.package_base);
        if (li != rank.end() || ri != rank.end()) {
            if (li == rank.end()) return false;
            if (ri == rank.end()) return true;
            return li->second < ri->second;
        }
        if (lhs.running != rhs.running) return lhs.running;
        return lhs.package_base < rhs.package_base;
    });
    return selected;
}
}

domain::MenuDocument PreviewService::build(
    const infrastructure::SystemInfo& system,
    const std::vector<infrastructure::SnapshotInfo>& snapshots,
    const config::AppConfig& config) const {
    domain::MenuDocument document;
    for (const auto& option : config.limine_options) document.global_options.push_back(option);

    const auto kernels = select_kernels(system.kernels, config);
    auto root = domain::MenuNode::directory(config.root_menu_title, config.root_menu_expanded);
    for (const auto& kernel : kernels) {
        root.add_child(domain::MenuNode::linux_entry(
            kernel.display_name, kernel_comment(kernel),
            {limine_path(kernel.image), modules_for(kernel), system.kernel_cmdline.render()}));
    }

    auto snapshot_dir = domain::MenuNode::directory(config.snapshots_menu_title, config.snapshots_menu_expanded);
    auto ordered = snapshots;
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) { return lhs.number > rhs.number; });
    std::size_t emitted = 0;
    for (const auto& snapshot : ordered) {
        if (!snapshot.read_only && !config.include_read_write_snapshots) continue;
        if (config.max_snapshots != 0 && emitted >= config.max_snapshots) break;
        auto snapshot_node = domain::MenuNode::directory(display_date(snapshot.date), false);
        const auto description = snapshot.description.empty() ? "Snapper snapshot #" + std::to_string(snapshot.number)
                                                               : snapshot.description;
        for (const auto& kernel : kernels) {
            snapshot_node.add_child(domain::MenuNode::linux_entry(
                kernel.display_name, description + " — " + kernel_comment(kernel),
                {limine_path(kernel.image), modules_for(kernel),
                 snapshot_cmdline(system.kernel_cmdline, config.snapshots_subvolume, snapshot.number)}));
        }
        snapshot_dir.add_child(std::move(snapshot_node));
        ++emitted;
    }
    root.add_child(std::move(snapshot_dir));
    document.roots.push_back(std::move(root));
    return document;
}

} // namespace limine_manager::application
