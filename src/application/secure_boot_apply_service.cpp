#include "limine_manager/application/secure_boot_apply_service.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace limine_manager::application {
namespace {

std::filesystem::path efi_backup_name(const std::filesystem::path &efi) {
    const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("limine-manager-" + efi.filename().string() + "." +
            std::to_string(::getpid()) + "." + std::to_string(ticks) + ".bak");
}

} // namespace

ApplyResult SecureBootApplyService::apply(const ChangePlan &plan,
                                           const infrastructure::SystemInfo &system,
                                           const config::AppConfig &config) const {
    if (!plan.has_changes())
        return {false, plan.target, {}};
    if (!system.secure_boot.enabled || !config.secure_boot_protect_config)
        return ApplyService{config.automation_runtime_directory}.apply(plan);
    if (system.secure_boot.efi_executable.empty())
        throw std::runtime_error(
            "Secure Boot is enabled but the active Limine EFI executable is unknown");

    const auto efi = system.secure_boot.efi_executable;
    const auto efi_backup = efi_backup_name(efi);
    std::filesystem::copy_file(efi, efi_backup, std::filesystem::copy_options::none);

    ApplyResult result;
    try {
        result = ApplyService{config.automation_runtime_directory}.apply(plan);
        const auto digest = hasher_.digest(plan.target);
        (void)tools_.update_limine_image(efi, digest);

        std::error_code cleanup_error;
        std::filesystem::remove(efi_backup, cleanup_error);
        return result;
    } catch (...) {
        std::error_code ec;
        std::filesystem::copy_file(efi, efi_backup,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::copy_file(efi_backup, efi,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (!result.backup.empty())
            std::filesystem::copy_file(result.backup, plan.target,
                                       std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(efi_backup, ec);
        throw;
    }
}

} // namespace limine_manager::application
