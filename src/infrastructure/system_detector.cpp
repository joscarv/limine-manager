#include "limine_manager/infrastructure/system_detector.hpp"

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
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
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
} // namespace

SystemInfo SystemDetector::detect() const {
    SystemInfo info;
    info.os_name = os_pretty_name(filesystem_);
    info.running_kernel_release = require_command(runner_, {"uname", "-r"}, "uname");
    info.boot_mount = profile_.boot_mount;
    info.boot_target = mount_field(runner_, profile_.boot_mount, "TARGET");
    info.boot_source = mount_field(runner_, profile_.boot_mount, "SOURCE");
    info.boot_fstype = mount_field(runner_, profile_.boot_mount, "FSTYPE");
    info.limine_config = profile_.limine_config;
    info.kernel_cmdline_file = profile_.kernel_cmdline_file;
    info.kernel_cmdline_text = trim(filesystem_.read_text(info.kernel_cmdline_file));
    info.kernel_cmdline = domain::KernelCommandLine::parse(info.kernel_cmdline_text);
    info.kernels =
        KernelDiscovery(filesystem_, {profile_.boot_mount, profile_.modules_root, profile_.cpuinfo})
            .discover(info.running_kernel_release);
    info.root_source = mount_field(runner_, "/", "SOURCE");
    info.root_fstype = mount_field(runner_, "/", "FSTYPE");
    info.root_subvolume = option_value(mount_field(runner_, "/", "OPTIONS"), "subvol");
    if (!info.root_subvolume.empty() && info.root_subvolume.front() == '/')
        info.root_subvolume.erase(0, 1);
    return info;
}

} // namespace limine_manager::infrastructure
