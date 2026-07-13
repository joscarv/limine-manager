#include "limine_manager/application/validation_service.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace limine_manager::application {
namespace {
void validate_file(const infrastructure::FileSystem &filesystem, domain::ValidationReport &report,
                   const std::filesystem::path &path, std::string code, std::string label) {
    if (!filesystem.readable(path))
        report.error(std::move(code), label + " is missing or unreadable: " + path.string());
    else
        report.info(std::move(code), label + " found: " + path.string());
}

std::string normalize_mount_source(std::string source) {
    const auto suffix = source.find('[');
    if (suffix != std::string::npos && source.ends_with(']'))
        source.erase(suffix);
    return source;
}

bool valid_uuid(std::string_view value) {
    if (value.empty())
        return false;
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char ch) { return std::isxdigit(ch) != 0 || ch == '-'; });
}

bool cryptdevice_maps_to(std::string_view value, std::string_view mapper_name) {
    constexpr std::string_view prefix{"UUID="};
    if (!value.starts_with(prefix))
        return false;
    const auto separator = value.find(':', prefix.size());
    if (separator == std::string_view::npos)
        return false;
    return valid_uuid(value.substr(prefix.size(), separator - prefix.size())) &&
           value.substr(separator + 1) == mapper_name;
}

bool rd_luks_name_maps_to(std::string_view value, std::string_view mapper_name) {
    const auto separator = value.find('=');
    if (separator == std::string_view::npos)
        return false;
    return valid_uuid(value.substr(0, separator)) && value.substr(separator + 1) == mapper_name;
}
} // namespace

domain::ValidationReport
ValidationService::validate(const infrastructure::SystemInfo &system,
                            const infrastructure::SnapperConfig &snapper_config,
                            const std::vector<infrastructure::SnapshotInfo> &snapshots,
                            const config::AppConfig &config) const {
    domain::ValidationReport report;

    if (system.root_fstype != "btrfs")
        report.error("root.filesystem",
                     "Root filesystem must be Btrfs; detected " + system.root_fstype);
    else
        report.info("root.filesystem", "Root filesystem is Btrfs");

    if (system.root_subvolume.empty())
        report.error("root.subvolume", "Unable to detect the root Btrfs subvolume");
    else
        report.info("root.subvolume", "Root subvolume is " + system.root_subvolume);

    if (system.boot_target != system.boot_mount.string())
        report.error("boot.mount", system.boot_mount.string() +
                                       " is not detected as an independent mount target");
    else
        report.info("boot.mount", "Boot mount detected at " + system.boot_target + " from " +
                                      system.boot_source + " (" + system.boot_fstype + ")");

    validate_file(filesystem_, report, system.limine_config, "limine.config",
                  "Limine configuration");
    validate_file(filesystem_, report, system.kernel_cmdline_file, "kernel.cmdline_file",
                  "Kernel command line file");

    if (system.kernel_cmdline.empty())
        report.error("kernel.cmdline", "Kernel command line is empty");
    else
        report.info("kernel.cmdline", "Kernel command line parsed into " +
                                          std::to_string(system.kernel_cmdline.arguments().size()) +
                                          " argument(s)");

    if (system.kernels.empty()) {
        report.error("kernel.discovery", "No kernel images matching vmlinuz-* were discovered in " +
                                             system.boot_mount.string());
    } else {
        report.info("kernel.discovery", "Discovered " + std::to_string(system.kernels.size()) +
                                            " kernel installation(s)");
        bool running_found = false;
        for (const auto &kernel : system.kernels) {
            const auto prefix = "kernel." + kernel.package_base;
            validate_file(filesystem_, report, kernel.image, prefix + ".image",
                          kernel.display_name + " image");
            if (kernel.release.empty())
                report.warning(prefix + ".release", "Unable to map " + kernel.package_base +
                                                        " to a release under /usr/lib/modules");
            else
                report.info(prefix + ".release",
                            kernel.display_name + " release is " + kernel.release);
            if (kernel.running)
                running_found = true;
            if (kernel.initrds.empty())
                report.error(prefix + ".initrd",
                             "No initramfs or microcode images discovered for " +
                                 kernel.display_name);
            for (std::size_t index = 0; index < kernel.initrds.size(); ++index) {
                validate_file(filesystem_, report, kernel.initrds[index],
                              prefix + ".module." + std::to_string(index),
                              kernel.display_name + " module");
            }
            const auto expected_initramfs =
                system.boot_mount / ("initramfs-" + kernel.package_base + ".img");
            if (!filesystem_.readable(expected_initramfs))
                report.error(prefix + ".initramfs",
                             "Expected initramfs is missing: " + expected_initramfs.string());
        }
        if (!running_found)
            report.warning("kernel.running", "Running release " + system.running_kernel_release +
                                                 " was not mapped to a discovered kernel image");
        else
            report.info("kernel.running", "Running release mapped to a discovered kernel image");
    }

    const auto mounted_root_source = normalize_mount_source(system.root_source);
    const auto root = system.kernel_cmdline.value("root");
    if (!root)
        report.error("kernel.root", "Kernel command line has no root= option");
    else if (*root != mounted_root_source)
        report.error("kernel.root", "Kernel root= option ('" + *root +
                                        "') does not match the mounted root source ('" +
                                        system.root_source + "')");
    else
        report.info("kernel.root", "Kernel root= option matches " + mounted_root_source);

    const auto mapper_name = std::filesystem::path(mounted_root_source).filename().string();
    const auto cryptdevices = system.kernel_cmdline.values("cryptdevice");
    const auto rd_luks_names = system.kernel_cmdline.values("rd.luks.name");
    const bool mapped_by_cryptdevice =
        std::any_of(cryptdevices.begin(), cryptdevices.end(),
                    [&](const auto &value) { return cryptdevice_maps_to(value, mapper_name); });
    const bool mapped_by_sd_encrypt =
        std::any_of(rd_luks_names.begin(), rd_luks_names.end(),
                    [&](const auto &value) { return rd_luks_name_maps_to(value, mapper_name); });

    if (mapped_by_cryptdevice) {
        report.info("kernel.encryption", "cryptdevice= maps the encrypted root as " + mapper_name);
    } else if (mapped_by_sd_encrypt) {
        report.info("kernel.encryption", "rd.luks.name= maps the encrypted root as " + mapper_name);
    } else if (cryptdevices.empty() && rd_luks_names.empty()) {
        report.error(
            "kernel.encryption",
            "Kernel command line defines neither cryptdevice= nor rd.luks.name= for mapper " +
                mapper_name);
    } else {
        report.error("kernel.encryption",
                     "Kernel encryption options do not map the encrypted root as " + mapper_name);
    }

    const auto rootflags = system.kernel_cmdline.value("rootflags");
    if (!rootflags || *rootflags != "subvol=" + system.root_subvolume) {
        report.error("kernel.rootflags", "Kernel rootflags ('" + rootflags.value_or("") +
                                             "') does not match subvol=" + system.root_subvolume);
    } else
        report.info("kernel.rootflags", "Kernel rootflags matches subvol=" + system.root_subvolume);

    if (snapper_config.subvolume != "/")
        report.error("snapper.subvolume", "Snapper config '" + config.snapper_config +
                                              "' manages '" + snapper_config.subvolume +
                                              "', expected '/'");
    else
        report.info("snapper.subvolume",
                    "Snapper config '" + config.snapper_config + "' manages /");

    if (snapshots.empty())
        report.warning("snapper.snapshots", "Snapper returned no bootable snapshots");
    std::size_t missing = 0;
    for (const auto &snapshot : snapshots) {
        const auto path = config.snapshots_directory / std::to_string(snapshot.number) / "snapshot";
        if (!filesystem_.is_directory(path)) {
            ++missing;
            report.error("snapshot.path", "Snapshot #" + std::to_string(snapshot.number) +
                                              " is not accessible at " + path.string());
        }
        if (!snapshot.read_only)
            report.warning("snapshot.read_only",
                           "Snapshot #" + std::to_string(snapshot.number) + " is read-write");
    }
    if (!snapshots.empty() && missing == 0)
        report.info("snapshot.paths",
                    "All " + std::to_string(snapshots.size()) + " snapshot paths are accessible");

    return report;
}

} // namespace limine_manager::application
