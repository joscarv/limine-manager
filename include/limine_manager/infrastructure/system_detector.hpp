#pragma once

#include "limine_manager/domain/kernel_cmdline.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"
#include "limine_manager/infrastructure/kernel_discovery.hpp"
#include "limine_manager/infrastructure/process.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace limine_manager::infrastructure {

struct SystemProfile {
    std::filesystem::path boot_mount{"/boot"};
    std::filesystem::path limine_config{"/boot/limine.conf"};
    std::filesystem::path kernel_cmdline_file{"/etc/kernel/cmdline"};
    std::filesystem::path modules_root{"/usr/lib/modules"};
    std::filesystem::path cpuinfo{"/proc/cpuinfo"};
};

struct SystemInfo {
    std::string os_name;
    std::string running_kernel_release;
    std::filesystem::path boot_mount;
    std::string boot_target;
    std::string boot_source;
    std::string boot_fstype;
    std::filesystem::path limine_config;
    std::filesystem::path kernel_cmdline_file;
    std::string kernel_cmdline_text;
    domain::KernelCommandLine kernel_cmdline{domain::KernelCommandLine::parse("")};
    std::vector<KernelInstallation> kernels;
    std::string root_source;
    std::string root_fstype;
    std::string root_subvolume;
};

class SystemDetector {
  public:
    SystemDetector(const ProcessRunner &runner, const FileSystem &filesystem,
                   SystemProfile profile = {})
        : runner_(runner), filesystem_(filesystem), profile_(std::move(profile)) {}
    [[nodiscard]] SystemInfo detect() const;

  private:
    const ProcessRunner &runner_;
    const FileSystem &filesystem_;
    SystemProfile profile_;
};

} // namespace limine_manager::infrastructure
