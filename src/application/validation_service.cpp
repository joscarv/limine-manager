#include "limine_manager/application/validation_service.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
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

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

bool same_uuid(std::string_view left, std::string_view right) {
    return !left.empty() && !right.empty() && lowercase(left) == lowercase(right);
}

enum class DeviceReferenceKind { uuid, partuuid, path, unknown };

struct DeviceReference {
    DeviceReferenceKind kind{DeviceReferenceKind::unknown};
    std::string value;
};

struct EncryptionMapping {
    DeviceReference source;
    std::string mapper;
};

DeviceReference parse_device_reference(std::string_view value) {
    constexpr std::string_view uuid_prefix{"UUID="};
    constexpr std::string_view partuuid_prefix{"PARTUUID="};
    if (value.starts_with(uuid_prefix))
        return {DeviceReferenceKind::uuid, std::string(value.substr(uuid_prefix.size()))};
    if (value.starts_with(partuuid_prefix))
        return {DeviceReferenceKind::partuuid, std::string(value.substr(partuuid_prefix.size()))};
    if (value.starts_with("/dev/"))
        return {DeviceReferenceKind::path, std::string(value)};
    return {DeviceReferenceKind::unknown, std::string(value)};
}

std::optional<EncryptionMapping> parse_cryptdevice(std::string_view value) {
    const auto separator = value.find(':');
    if (separator == std::string_view::npos)
        return std::nullopt;
    auto source = parse_device_reference(value.substr(0, separator));
    if (source.kind == DeviceReferenceKind::unknown || source.value.empty())
        return std::nullopt;
    const auto mapper_end = value.find(':', separator + 1);
    auto mapper = std::string(value.substr(separator + 1, mapper_end - separator - 1));
    if (mapper.empty())
        return std::nullopt;
    return EncryptionMapping{std::move(source), std::move(mapper)};
}

std::optional<EncryptionMapping> parse_rd_luks_name(std::string_view value) {
    const auto separator = value.find('=');
    const auto uuid = separator == std::string_view::npos ? value : value.substr(0, separator);
    if (!valid_uuid(uuid))
        return std::nullopt;
    return EncryptionMapping{{DeviceReferenceKind::uuid, std::string(uuid)},
                             separator == std::string_view::npos
                                 ? std::string{}
                                 : std::string(value.substr(separator + 1))};
}

bool same_device_path(const infrastructure::FileSystem &filesystem, std::string_view left,
                      std::string_view right) {
    if (left.empty() || right.empty())
        return false;
    try {
        return filesystem.canonical(std::filesystem::path(left)) ==
               filesystem.canonical(std::filesystem::path(right));
    } catch (...) {
        return left == right;
    }
}

bool mapping_identifies_root(const EncryptionMapping &mapping, std::string_view mapper_name,
                             std::string_view luks_uuid, std::string_view backing_partuuid,
                             std::string_view backing_device,
                             const infrastructure::FileSystem &filesystem) {
    if (!mapping.mapper.empty() && mapping.mapper == mapper_name)
        return true;

    switch (mapping.source.kind) {
    case DeviceReferenceKind::uuid:
        return same_uuid(mapping.source.value, luks_uuid);
    case DeviceReferenceKind::partuuid:
        return same_uuid(mapping.source.value, backing_partuuid);
    case DeviceReferenceKind::path:
        return same_device_path(filesystem, mapping.source.value, backing_device);
    case DeviceReferenceKind::unknown:
        return false;
    }
    return false;
}
} // namespace

domain::ValidationReport
ValidationService::validate(const infrastructure::SystemInfo &system,
                            const infrastructure::SnapperConfig &snapper_config,
                            const std::vector<infrastructure::SnapshotInfo> &snapshots,
                            const config::AppConfig &config) const {
    domain::ValidationReport report;

    if (system.secure_boot.enabled) {
        report.info("secure_boot.enabled", "UEFI Secure Boot is enabled");
        if (!config.secure_boot_protect_config)
            report.error(
                "secure_boot.protection_disabled",
                "Secure Boot is enabled but protected Limine configuration generation is disabled");
        if (!system.secure_boot.sbctl_available)
            report.error("secure_boot.sbctl", "sbctl is required when Secure Boot is enabled");
        if (!system.secure_boot.limine_available)
            report.error("secure_boot.limine", "limine enroll-config is unavailable");
        if (!system.secure_boot.sbattach_available)
            report.error("secure_boot.sbattach", "sbattach from sbsigntools is required to safely "
                                                 "re-sign the Limine EFI executable");
        if (system.secure_boot.efi_executable.empty())
            report.error("secure_boot.efi", "Unable to locate the active Limine EFI executable");
        else if (system.secure_boot.efi_signature ==
                 infrastructure::SignatureVerificationState::unsigned_file)
            report.error("secure_boot.signature",
                         "Limine EFI executable is not verified by sbctl: " +
                             system.secure_boot.efi_executable.string());
        else if (system.secure_boot.efi_signature ==
                 infrastructure::SignatureVerificationState::unavailable)
            report.warning(
                "secure_boot.signature_unavailable",
                "Unable to verify the Limine EFI signature without sufficient privileges; "
                "run limine-manager with sudo before applying changes");
        for (const auto &kernel : system.kernels) {
            if (!kernel.unified_kernel_image) {
                const auto it = system.secure_boot.resource_hashes.find(kernel.image);
                if (it == system.secure_boot.resource_hashes.end() || it->second.empty())
                    report.error("secure_boot.hash",
                                 "Unable to calculate BLAKE2b for " + kernel.image.string());
            }
            for (const auto &initrd : kernel.initrds) {
                const auto it = system.secure_boot.resource_hashes.find(initrd);
                if (it == system.secure_boot.resource_hashes.end() || it->second.empty())
                    report.error("secure_boot.hash",
                                 "Unable to calculate BLAKE2b for " + initrd.string());
            }
        }
    } else {
        report.info("secure_boot.disabled", "UEFI Secure Boot is disabled or not detected");
    }

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
    if (system.kernel_cmdline_generated)
        report.info("kernel.cmdline_file", "Kernel command line generated automatically because " +
                                               system.kernel_cmdline_file.string() +
                                               " is missing or empty");
    else
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
            if (kernel.unified_kernel_image) {
                report.info(prefix + ".uki",
                            "Unified Kernel Image detected: " + kernel.image.string());
            } else {
                if (kernel.initrds.empty())
                    report.error(prefix + ".initrd",
                                 "No initramfs image discovered for " + kernel.display_name);
                for (std::size_t index = 0; index < kernel.initrds.size(); ++index) {
                    validate_file(filesystem_, report, kernel.initrds[index],
                                  prefix + ".module." + std::to_string(index),
                                  kernel.display_name + " module");
                }
                const bool has_initramfs = std::any_of(
                    kernel.initrds.begin(), kernel.initrds.end(), [&](const auto &path) {
                        return path.filename() == ("initramfs-" + kernel.package_base + ".img");
                    });
                if (!has_initramfs)
                    report.error(prefix + ".initramfs",
                                 "No matching initramfs was discovered under " +
                                     system.boot_mount.string());
            }
        }
        if (!running_found)
            report.warning("kernel.running", "Running release " + system.running_kernel_release +
                                                 " was not mapped to a discovered kernel image");
        else
            report.info("kernel.running", "Running release mapped to a discovered kernel image");
    }

    const auto mounted_root_source = normalize_mount_source(system.root_source);
    const auto root = system.kernel_cmdline.value("root");
    const auto uuid_root = system.root_uuid.empty() ? std::string{} : "UUID=" + system.root_uuid;
    if (!root)
        report.error("kernel.root", "Kernel command line has no root= option");
    else if (*root != mounted_root_source && (uuid_root.empty() || *root != uuid_root))
        report.error("kernel.root", "Kernel root= option ('" + *root +
                                        "') does not match the mounted root source ('" +
                                        system.root_source + "') or its filesystem UUID");
    else
        report.info("kernel.root", "Kernel root= option identifies the mounted root filesystem");

    const auto mapper_name = system.root_mapper_name.empty()
                                 ? std::filesystem::path(mounted_root_source).filename().string()
                                 : system.root_mapper_name;
    const auto cryptdevices = system.kernel_cmdline.values("cryptdevice");
    const auto rd_luks_names = system.kernel_cmdline.values("rd.luks.name");
    const bool mapped_by_cryptdevice =
        std::any_of(cryptdevices.begin(), cryptdevices.end(), [&](const auto &value) {
            const auto mapping = parse_cryptdevice(value);
            return mapping && mapping_identifies_root(*mapping, mapper_name, system.luks_uuid,
                                                      system.encrypted_backing_partuuid,
                                                      system.encrypted_backing_device, filesystem_);
        });
    const bool mapped_by_sd_encrypt =
        std::any_of(rd_luks_names.begin(), rd_luks_names.end(), [&](const auto &value) {
            const auto mapping = parse_rd_luks_name(value);
            return mapping && mapping_identifies_root(*mapping, mapper_name, system.luks_uuid,
                                                      system.encrypted_backing_partuuid,
                                                      system.encrypted_backing_device, filesystem_);
        });

    if (!system.root_encrypted) {
        if (!cryptdevices.empty() || !rd_luks_names.empty())
            report.warning("kernel.encryption",
                           "Encryption parameters are present although the mounted root is not a "
                           "dm-crypt mapping");
        else
            report.info("kernel.encryption", "Root filesystem is not encrypted");
    } else if (mapped_by_cryptdevice) {
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
