#include "limine_manager/infrastructure/kernel_discovery.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace limine_manager::infrastructure {
namespace {
std::string first_line(std::string value) {
    const auto end = value.find_first_of("\r\n");
    if (end != std::string::npos)
        value.resize(end);
    return value;
}

std::string title_for(std::string_view pkgbase) {
    if (pkgbase == "linux") return "Linux";
    if (pkgbase == "linux-lts") return "Linux LTS";
    if (pkgbase == "linux-zen") return "Linux Zen";
    if (pkgbase == "linux-hardened") return "Linux Hardened";
    std::string title{"Linux"};
    std::string suffix(pkgbase);
    if (suffix.rfind("linux-", 0) == 0) suffix.erase(0, 6);
    title += ' ';
    bool upper = true;
    for (const char ch : suffix) {
        if (ch == '-' || ch == '_') { title.push_back(' '); upper = true; }
        else {
            title.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch);
            upper = false;
        }
    }
    return title;
}

void collect_files(const FileSystem &fs, const std::filesystem::path &directory,
                   std::vector<std::filesystem::path> &files, unsigned depth = 0) {
    if (depth > 8 || !fs.is_directory(directory)) return;
    for (const auto &entry : fs.list_directory(directory)) {
        if (entry.regular_file) files.push_back(entry.path);
        else if (entry.directory) collect_files(fs, entry.path, files, depth + 1);
    }
}

std::vector<std::filesystem::path> named_files(const std::vector<std::filesystem::path> &files,
                                                std::string_view filename) {
    std::vector<std::filesystem::path> matches;
    for (const auto &path : files)
        if (path.filename() == filename) matches.push_back(path);
    std::sort(matches.begin(), matches.end());
    return matches;
}

std::filesystem::path find_uki(const std::vector<std::filesystem::path> &files,
                               std::string_view pkgbase) {
    const std::vector<std::string> preferred = {
        "arch-" + std::string(pkgbase) + ".efi",
        std::string(pkgbase) + ".efi",
        "vmlinuz-" + std::string(pkgbase) + ".efi"};
    for (const auto &name : preferred) {
        for (const auto &path : files) {
            auto extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".efi" && path.filename() == name)
                return path;
        }
    }
    return {};
}

std::vector<std::filesystem::path> microcode_images(const FileSystem &fs,
                                                    const KernelDiscoveryProfile &profile,
                                                    const std::vector<std::filesystem::path> &files) {
    const auto cpuinfo = fs.read_text(profile.cpuinfo);
    std::string filename;
    if (cpuinfo.find("GenuineIntel") != std::string::npos) filename = "intel-ucode.img";
    else if (cpuinfo.find("AuthenticAMD") != std::string::npos) filename = "amd-ucode.img";
    if (filename.empty()) return {};
    return named_files(files, filename);
}
} // namespace

std::vector<KernelInstallation> KernelDiscovery::discover(std::string_view running_release) const {
    std::unordered_map<std::string, std::string> releases;
    if (filesystem_.is_directory(profile_.modules_root)) {
        for (const auto &entry : filesystem_.list_directory(profile_.modules_root)) {
            if (!entry.directory) continue;
            const auto pkgbase = first_line(filesystem_.read_text(entry.path / "pkgbase"));
            if (!pkgbase.empty()) releases[pkgbase] = entry.path.filename().string();
        }
    }

    std::vector<std::filesystem::path> boot_files;
    collect_files(filesystem_, profile_.boot_mount, boot_files);
    const auto microcodes = microcode_images(filesystem_, profile_, boot_files);
    std::vector<KernelInstallation> kernels;
    for (const auto &path : boot_files) {
        const auto filename = path.filename().string();
        constexpr std::string_view prefix{"vmlinuz-"};
        if (filename.rfind(prefix, 0) != 0 || filename.size() == prefix.size()) continue;
        const auto pkgbase = filename.substr(prefix.size());
        KernelInstallation kernel;
        kernel.package_base = pkgbase;
        kernel.release = releases.contains(pkgbase) ? releases.at(pkgbase) : std::string{};
        kernel.display_name = title_for(pkgbase);
        kernel.image = path;
        kernel.initrds = microcodes;
        const auto initramfs_name = "initramfs-" + pkgbase + ".img";
        const auto initramfs_matches = named_files(boot_files, initramfs_name);
        if (!initramfs_matches.empty()) {
            kernel.initrds.push_back(initramfs_matches.front());
        } else if (const auto uki = find_uki(boot_files, pkgbase); !uki.empty()) {
            kernel.image = uki;
            kernel.initrds.clear();
            kernel.unified_kernel_image = true;
        }
        kernel.running = !kernel.release.empty() && kernel.release == running_release;
        kernels.push_back(std::move(kernel));
    }
    std::sort(kernels.begin(), kernels.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.running != rhs.running) return lhs.running > rhs.running;
        if (lhs.package_base == "linux" || rhs.package_base == "linux") return lhs.package_base == "linux";
        return lhs.package_base < rhs.package_base;
    });
    return kernels;
}
} // namespace limine_manager::infrastructure
