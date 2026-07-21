#include "limine_manager/application/preview_service.hpp"
#include "limine_manager/application/snapshot_selector.hpp"
#include "limine_manager/config/theme.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace limine_manager::application {
namespace {
std::string limine_path(const std::filesystem::path &boot_mount,
                        const std::filesystem::path &absolute_boot_path,
                        const infrastructure::SystemInfo *system = nullptr,
                        bool hash_resource = false) {
    std::error_code ec;
    auto relative = std::filesystem::relative(absolute_boot_path, boot_mount, ec);
    if (ec || relative.empty() || relative.native().starts_with(".."))
        relative = absolute_boot_path.filename();
    auto result = "boot():/" + relative.generic_string();
    if (hash_resource && system && system->secure_boot.enabled) {
        const auto it = system->secure_boot.resource_hashes.find(absolute_boot_path);
        if (it != system->secure_boot.resource_hashes.end() && !it->second.empty())
            result += "#" + it->second;
    }
    return result;
}

std::string snapshot_cmdline(const domain::KernelCommandLine &cmdline,
                             const std::string &snapshots_subvolume, unsigned long number) {
    auto snapshot = cmdline;
    snapshot.set("rootflags",
                 "subvol=" + snapshots_subvolume + "/" + std::to_string(number) + "/snapshot");
    return snapshot.render();
}

std::string display_date(std::string date) {
    if (date.size() >= 16)
        return date.substr(0, 16);
    return date;
}

std::vector<std::string> modules_for(const infrastructure::KernelInstallation &kernel,
                                     const infrastructure::SystemInfo &system) {
    std::vector<std::string> modules;
    modules.reserve(kernel.initrds.size());
    for (const auto &initrd : kernel.initrds)
        modules.push_back(limine_path(system.boot_mount, initrd, &system, true));
    return modules;
}

std::string kernel_comment(const infrastructure::KernelInstallation &kernel) {
    if (!kernel.release.empty())
        return "Kernel " + kernel.release;
    return "Kernel release unavailable (" + kernel.package_base + ')';
}

std::vector<infrastructure::KernelInstallation>
select_kernels(const std::vector<infrastructure::KernelInstallation> &kernels,
               const config::AppConfig &config) {
    const std::unordered_set<std::string> included(config.include_kernels.begin(),
                                                   config.include_kernels.end());
    const std::unordered_set<std::string> excluded(config.exclude_kernels.begin(),
                                                   config.exclude_kernels.end());
    std::vector<infrastructure::KernelInstallation> selected;
    for (const auto &kernel : kernels) {
        if (!included.empty() && !included.contains(kernel.package_base))
            continue;
        if (excluded.contains(kernel.package_base))
            continue;
        selected.push_back(kernel);
    }
    std::unordered_map<std::string, std::size_t> rank;
    for (std::size_t i = 0; i < config.kernel_order.size(); ++i)
        rank.emplace(config.kernel_order[i], i);
    std::stable_sort(selected.begin(), selected.end(), [&](const auto &lhs, const auto &rhs) {
        const auto li = rank.find(lhs.package_base);
        const auto ri = rank.find(rhs.package_base);
        if (li != rank.end() || ri != rank.end()) {
            if (li == rank.end())
                return false;
            if (ri == rank.end())
                return true;
            return li->second < ri->second;
        }
        if (lhs.running != rhs.running)
            return lhs.running;
        return lhs.package_base < rhs.package_base;
    });
    return selected;
}
} // namespace

domain::MenuDocument
PreviewService::build(const infrastructure::SystemInfo &system,
                      const std::vector<infrastructure::SnapshotInfo> &snapshots,
                      const config::AppConfig &config) const {
    domain::MenuDocument document;
    if (const auto *theme = config::find_theme(config.theme_name)) {
        for (const auto &option : theme->options)
            document.global_options.push_back(option);
    }
    for (const auto &option : config.limine_options)
        document.global_options.push_back(option);

    const auto kernels = select_kernels(system.kernels, config);
    auto root = domain::MenuNode::directory(config.root_menu_title, config.root_menu_expanded);
    for (const auto &kernel : kernels) {
        root.add_child(domain::MenuNode::linux_entry(
            kernel.display_name, kernel_comment(kernel),
            {kernel.unified_kernel_image ? "efi" : "linux",
             limine_path(system.boot_mount, kernel.image, &system, !kernel.unified_kernel_image),
             modules_for(kernel, system), system.kernel_cmdline.render()}));
    }

    auto snapshot_dir =
        domain::MenuNode::directory(config.snapshots_menu_title, config.snapshots_menu_expanded);
    const auto selected_snapshots = select_menu_snapshots(snapshots, config);
    for (const auto &snapshot : selected_snapshots) {
        auto snapshot_node = domain::MenuNode::directory(display_date(snapshot.date), false);
        const auto description = snapshot.description.empty()
                                     ? "Snapper snapshot #" + std::to_string(snapshot.number)
                                     : snapshot.description;
        for (const auto &kernel : kernels) {
            snapshot_node.add_child(domain::MenuNode::linux_entry(
                kernel.display_name, description + " — " + kernel_comment(kernel),
                {kernel.unified_kernel_image ? "efi" : "linux",
                 limine_path(system.boot_mount, kernel.image, &system,
                             !kernel.unified_kernel_image),
                 modules_for(kernel, system),
                 snapshot_cmdline(system.kernel_cmdline, config.snapshots_subvolume,
                                  snapshot.number)}));
        }
        snapshot_dir.add_child(std::move(snapshot_node));
    }
    root.add_child(std::move(snapshot_dir));
    document.roots.push_back(std::move(root));
    return document;
}

} // namespace limine_manager::application
