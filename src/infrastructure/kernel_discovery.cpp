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
    if (pkgbase == "linux")
        return "Linux";
    if (pkgbase == "linux-lts")
        return "Linux LTS";
    if (pkgbase == "linux-zen")
        return "Linux Zen";
    if (pkgbase == "linux-hardened")
        return "Linux Hardened";
    std::string title{"Linux"};
    std::string suffix(pkgbase);
    if (suffix.rfind("linux-", 0) == 0)
        suffix.erase(0, 6);
    title += ' ';
    bool upper = true;
    for (const char ch : suffix) {
        if (ch == '-' || ch == '_') {
            title.push_back(' ');
            upper = true;
        } else {
            title.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))
                                  : ch);
            upper = false;
        }
    }
    return title;
}

std::vector<std::filesystem::path> microcode_images(const FileSystem &fs,
                                                    const KernelDiscoveryProfile &profile) {
    const auto cpuinfo = fs.read_text(profile.cpuinfo);
    std::string filename;
    if (cpuinfo.find("GenuineIntel") != std::string::npos)
        filename = "intel-ucode.img";
    else if (cpuinfo.find("AuthenticAMD") != std::string::npos)
        filename = "amd-ucode.img";
    if (filename.empty())
        return {};
    const auto path = profile.boot_mount / filename;
    return fs.is_regular_file(path) ? std::vector<std::filesystem::path>{path}
                                    : std::vector<std::filesystem::path>{};
}
} // namespace

std::vector<KernelInstallation> KernelDiscovery::discover(std::string_view running_release) const {
    std::unordered_map<std::string, std::string> releases;
    if (filesystem_.is_directory(profile_.modules_root)) {
        for (const auto &entry : filesystem_.list_directory(profile_.modules_root)) {
            if (!entry.directory)
                continue;
            const auto pkgbase = first_line(filesystem_.read_text(entry.path / "pkgbase"));
            if (!pkgbase.empty())
                releases[pkgbase] = entry.path.filename().string();
        }
    }

    const auto microcodes = microcode_images(filesystem_, profile_);
    std::vector<KernelInstallation> kernels;
    if (!filesystem_.is_directory(profile_.boot_mount))
        return kernels;
    for (const auto &entry : filesystem_.list_directory(profile_.boot_mount)) {
        if (!entry.regular_file)
            continue;
        const auto filename = entry.path.filename().string();
        constexpr std::string_view prefix{"vmlinuz-"};
        if (filename.rfind(prefix, 0) != 0 || filename.size() == prefix.size())
            continue;
        const auto pkgbase = filename.substr(prefix.size());
        KernelInstallation kernel;
        kernel.package_base = pkgbase;
        kernel.release = releases.contains(pkgbase) ? releases.at(pkgbase) : std::string{};
        kernel.display_name = title_for(pkgbase);
        kernel.image = entry.path;
        kernel.initrds = microcodes;
        const auto initramfs = profile_.boot_mount / ("initramfs-" + pkgbase + ".img");
        if (filesystem_.is_regular_file(initramfs))
            kernel.initrds.push_back(initramfs);
        kernel.running = !kernel.release.empty() && kernel.release == running_release;
        kernels.push_back(std::move(kernel));
    }
    std::sort(kernels.begin(), kernels.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.running != rhs.running)
            return lhs.running > rhs.running;
        if (lhs.package_base == "linux" || rhs.package_base == "linux")
            return lhs.package_base == "linux";
        return lhs.package_base < rhs.package_base;
    });
    return kernels;
}
} // namespace limine_manager::infrastructure
