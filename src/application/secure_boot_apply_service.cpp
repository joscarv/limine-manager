#include "limine_manager/application/secure_boot_apply_service.hpp"

#include "limine_manager/infrastructure/efi_image_transaction.hpp"

#include <filesystem>
#include <stdexcept>

namespace limine_manager::application {

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

    infrastructure::EfiImageTransaction efi_transaction{
        system.secure_boot.efi_executable};
    ApplyResult result;

    try {
        result = ApplyService{config.automation_runtime_directory}.apply(plan);
        const auto digest = hasher_.digest(plan.target);
        (void)tools_.update_limine_image(efi_transaction.image(), digest);
        efi_transaction.commit();
        return result;
    } catch (...) {
        efi_transaction.rollback();

        if (!result.backup.empty()) {
            std::error_code ec;
            const bool restored = std::filesystem::copy_file(
                result.backup, plan.target,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (!restored || ec)
                throw std::runtime_error("failed to restore Limine configuration " +
                                         plan.target.string() + ": " + ec.message());
        }
        throw;
    }
}

} // namespace limine_manager::application
