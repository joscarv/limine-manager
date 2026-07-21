#pragma once

#include "limine_manager/infrastructure/filesystem.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace limine_manager::infrastructure {

struct KernelInstallation {
    std::string package_base;
    std::string release;
    std::string display_name;
    std::filesystem::path image;
    std::vector<std::filesystem::path> initrds;
    bool unified_kernel_image {false};
    bool running {false};
};

struct KernelDiscoveryProfile {
    std::filesystem::path boot_mount {"/boot"};
    std::filesystem::path modules_root {"/usr/lib/modules"};
    std::filesystem::path cpuinfo {"/proc/cpuinfo"};
};

class KernelDiscovery {
  public:
    KernelDiscovery(const FileSystem &filesystem, KernelDiscoveryProfile profile = {})
        : filesystem_(filesystem), profile_(std::move(profile)) {}
    [[nodiscard]] std::vector<KernelInstallation> discover(std::string_view running_release) const;

  private:
    const FileSystem &filesystem_;
    KernelDiscoveryProfile profile_;
};

} // namespace limine_manager::infrastructure
