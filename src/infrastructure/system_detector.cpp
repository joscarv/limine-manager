#include "limine_manager/infrastructure/system_detector.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace limine_manager::infrastructure {
namespace {
std::string trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string unquote(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return value;
}

std::string os_pretty_name(const FileSystem &filesystem) {
    std::istringstream input(filesystem.read_text("/etc/os-release"));
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0)
            return unquote(line.substr(12));
    }
    return "Linux";
}

std::string require_command(const ProcessRunner &runner, std::vector<std::string> args,
                            const std::string &label) {
    auto result = runner.run(args);
    if (result.exit_code != 0)
        throw std::runtime_error(label + " failed: " + trim(result.output));
    return trim(result.output);
}

std::string optional_command(const ProcessRunner &runner, std::vector<std::string> args) {
    auto result = runner.run(args);
    return result.exit_code == 0 ? trim(result.output) : std::string{};
}

std::string mount_field(const ProcessRunner &runner, const std::filesystem::path &target,
                        const std::string &field) {
    return require_command(
        runner,
        {"findmnt", "--noheadings", "--raw", "--target", target.string(), "--output", field},
        "findmnt " + target.string() + " " + field);
}

std::string option_value(const std::string &options, const std::string &key) {
    std::size_t begin = 0;
    while (begin <= options.size()) {
        const auto end = options.find(',', begin);
        const auto item =
            options.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (item.rfind(key + "=", 0) == 0)
            return item.substr(key.size() + 1);
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return {};
}

std::string normalize_mount_source(std::string source) {
    const auto suffix = source.find('[');
    if (suffix != std::string::npos && source.ends_with(']'))
        source.erase(suffix);
    return source;
}

std::string sysfs_backing_device(const FileSystem &filesystem, const std::string &mounted_root) {
    const auto canonical_mapper = filesystem.canonical(mounted_root);
    const auto mapper_device = canonical_mapper.filename();
    if (mapper_device.empty())
        return {};

    const auto slaves = std::filesystem::path("/sys/class/block") / mapper_device / "slaves";
    if (!filesystem.is_directory(slaves))
        return {};

    const auto entries = filesystem.list_directory(slaves);
    if (entries.size() != 1)
        return {};

    const auto device_name = entries.front().path.filename().string();
    return device_name.empty() ? std::string{} : "/dev/" + device_name;
}

std::string cryptsetup_backing_device(const ProcessRunner &runner, std::string_view mapper) {
    const auto output = optional_command(runner, {"cryptsetup", "status", std::string(mapper)});
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        constexpr std::string_view prefix{"device:"};
        if (line.starts_with(prefix))
            return trim(line.substr(prefix.size()));
    }
    return {};
}

bool uses_sd_encrypt(const FileSystem &filesystem, const std::filesystem::path &config) {
    const auto text = filesystem.read_text(config);
    const auto hooks = text.find("HOOKS=");
    if (hooks == std::string::npos)
        return false;
    const auto end = text.find('\n', hooks);
    const auto line =
        text.substr(hooks, end == std::string::npos ? std::string::npos : end - hooks);
    return line.find("sd-encrypt") != std::string::npos;
}

void collect_named_files(const FileSystem &filesystem, const std::filesystem::path &directory,
                         std::string_view filename, std::vector<std::filesystem::path> &matches,
                         unsigned depth = 0) {
    if (depth > 8 || !filesystem.is_directory(directory))
        return;
    for (const auto &entry : filesystem.list_directory(directory)) {
        if (entry.regular_file && entry.path.filename() == filename)
            matches.push_back(entry.path);
        else if (entry.directory)
            collect_named_files(filesystem, entry.path, filename, matches, depth + 1);
    }
}

std::filesystem::path discover_limine_config(const FileSystem &filesystem,
                                             const std::filesystem::path &configured,
                                             const std::filesystem::path &boot_mount) {
    if (filesystem.is_regular_file(configured))
        return configured;
    std::vector<std::filesystem::path> matches;
    collect_named_files(filesystem, boot_mount, "limine.conf", matches);
    if (matches.empty())
        return configured;
    std::sort(matches.begin(), matches.end(), [&](const auto &lhs, const auto &rhs) {
        const auto preferred = boot_mount / "EFI/BOOT/limine.conf";
        if (lhs == preferred || rhs == preferred)
            return lhs == preferred;
        return lhs.generic_string() < rhs.generic_string();
    });
    return matches.front();
}

std::filesystem::path discover_limine_efi(const FileSystem &filesystem,
                                          const std::filesystem::path &boot_mount,
                                          const std::filesystem::path &configured) {
    if (!configured.empty() && filesystem.is_regular_file(configured))
        return configured;
    const std::vector<std::filesystem::path> candidates{boot_mount / "EFI/BOOT/BOOTX64.EFI",
                                                        boot_mount / "EFI/limine/limine_x64.efi",
                                                        boot_mount / "limine_x64.efi"};
    for (const auto &candidate : candidates) {
        if (filesystem.is_regular_file(candidate))
            return candidate;
    }
    return {};
}

std::string blake2b(const ProcessRunner &runner, const std::filesystem::path &path) {
    const auto result = runner.run({"b2sum", path.string()});
    if (result.exit_code != 0)
        return {};
    const auto split = result.output.find_first_of(" \t\r\n");
    const auto digest = result.output.substr(0, split);
    return digest.size() == 128 ? digest : std::string{};
}

SecureBootInfo detect_secure_boot(const ProcessRunner &runner, const FileSystem &filesystem,
                                  const SystemProfile &profile,
                                  const std::vector<KernelInstallation> &kernels) {
    SecureBootInfo info;
    const auto status = runner.run({"sbctl", "status"});
    info.sbctl_available = status.exit_code == 0;
    if (info.sbctl_available) {
        std::istringstream lines(status.output);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.find("Secure Boot:") != std::string::npos)
                info.enabled = line.find("Enabled") != std::string::npos;
            else if (line.find("Setup Mode:") != std::string::npos)
                info.setup_mode = line.find("Enabled") != std::string::npos;
        }
    }
    info.limine_available = runner.run({"limine", "--version"}).exit_code == 0;
    info.sbattach_available = runner.run({"sbattach", "--help"}).exit_code == 0;
    info.efi_executable =
        discover_limine_efi(filesystem, profile.boot_mount, profile.limine_efi_executable);
    if (!info.efi_executable.empty() && info.sbctl_available) {
        const auto verify = runner.run({"sbctl", "verify", info.efi_executable.string()});
        info.efi_signature_detail = verify.output;
        if (verify.exit_code == 0) {
            info.efi_signature = SignatureVerificationState::verified;
        } else if (verify.output.find("permission denied") != std::string::npos ||
                   verify.output.find("requires root") != std::string::npos) {
            info.efi_signature = SignatureVerificationState::unavailable;
        } else {
            info.efi_signature = SignatureVerificationState::unsigned_file;
        }
    }
    if (info.enabled) {
        for (const auto &kernel : kernels) {
            if (!kernel.unified_kernel_image)
                info.resource_hashes[kernel.image] = blake2b(runner, kernel.image);
            for (const auto &initrd : kernel.initrds)
                info.resource_hashes[initrd] = blake2b(runner, initrd);
        }
    }
    return info;
}

std::string generate_cmdline(const SystemInfo &info, bool sd_encrypt) {
    std::string result;
    if (info.root_encrypted) {
        if (info.luks_uuid.empty())
            throw std::runtime_error("Unable to determine the LUKS UUID for encrypted root " +
                                     info.root_source);
        if (sd_encrypt)
            result = "rd.luks.name=" + info.luks_uuid + "=" + info.root_mapper_name;
        else
            result = "cryptdevice=UUID=" + info.luks_uuid + ":" + info.root_mapper_name;
        result += " root=/dev/mapper/" + info.root_mapper_name;
    } else {
        if (info.root_uuid.empty())
            throw std::runtime_error("Unable to determine the filesystem UUID for root " +
                                     info.root_source);
        result = "root=UUID=" + info.root_uuid;
    }
    if (!info.root_subvolume.empty())
        result += " rootflags=subvol=" + info.root_subvolume;
    result += " rw";
    return result;
}

} // namespace

SystemInfo SystemDetector::detect() const {
    SystemInfo info;
    info.os_name = os_pretty_name(filesystem_);
    info.running_kernel_release = require_command(runner_, {"uname", "-r"}, "uname");
    info.boot_mount = profile_.boot_mount;
    info.boot_target = mount_field(runner_, profile_.boot_mount, "TARGET");
    info.boot_source = mount_field(runner_, profile_.boot_mount, "SOURCE");
    info.boot_fstype = mount_field(runner_, profile_.boot_mount, "FSTYPE");
    info.limine_config =
        discover_limine_config(filesystem_, profile_.limine_config, profile_.boot_mount);
    info.kernel_cmdline_file = profile_.kernel_cmdline_file;
    info.kernels =
        KernelDiscovery(filesystem_, {profile_.boot_mount, profile_.modules_root, profile_.cpuinfo})
            .discover(info.running_kernel_release);
    info.secure_boot = detect_secure_boot(runner_, filesystem_, profile_, info.kernels);
    info.root_source = mount_field(runner_, "/", "SOURCE");
    info.root_fstype = mount_field(runner_, "/", "FSTYPE");
    info.root_subvolume = option_value(mount_field(runner_, "/", "OPTIONS"), "subvol");
    if (!info.root_subvolume.empty() && info.root_subvolume.front() == '/')
        info.root_subvolume.erase(0, 1);

    const auto mounted_root = normalize_mount_source(info.root_source);
    const auto block_type = optional_command(
        runner_, {"lsblk", "--noheadings", "--raw", "--output", "TYPE", mounted_root});
    info.root_encrypted =
        block_type.empty() ? mounted_root.starts_with("/dev/mapper/") : block_type == "crypt";

    if (info.root_encrypted) {
        info.root_mapper_name = std::filesystem::path(mounted_root).filename().string();
        info.root_uuid = optional_command(
            runner_, {"blkid", "--match-tag", "UUID", "--output", "value", mounted_root});
        auto parent = sysfs_backing_device(filesystem_, mounted_root);
        if (parent.empty())
            parent = cryptsetup_backing_device(runner_, info.root_mapper_name);
        if (parent.empty()) {
            parent = optional_command(
                runner_, {"lsblk", "--noheadings", "--raw", "--output", "PKNAME", mounted_root});
            if (!parent.empty() && !parent.starts_with("/dev/"))
                parent = "/dev/" + parent;
        }
        info.encrypted_backing_device = parent;
        if (!parent.empty()) {
            info.luks_uuid = optional_command(
                runner_, {"blkid", "--match-tag", "UUID", "--output", "value", parent});
            info.encrypted_backing_partuuid = optional_command(
                runner_, {"blkid", "--match-tag", "PARTUUID", "--output", "value", parent});
        }
    } else {
        info.root_uuid = optional_command(
            runner_, {"blkid", "--match-tag", "UUID", "--output", "value", mounted_root});
    }

    info.kernel_cmdline_text = trim(filesystem_.read_text(info.kernel_cmdline_file));
    if (info.kernel_cmdline_text.empty()) {
        info.kernel_cmdline_text =
            generate_cmdline(info, uses_sd_encrypt(filesystem_, profile_.mkinitcpio_config));
        info.kernel_cmdline_generated = true;
    }
    info.kernel_cmdline = domain::KernelCommandLine::parse(info.kernel_cmdline_text);
    return info;
}

} // namespace limine_manager::infrastructure
