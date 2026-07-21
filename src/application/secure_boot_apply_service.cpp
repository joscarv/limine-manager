#include "limine_manager/application/secure_boot_apply_service.hpp"

#include "limine_manager/application/secure_boot_transaction.hpp"

#include <exception>
#include <stdexcept>
#include <string>

namespace limine_manager::application {
namespace {

std::string exception_message(const std::exception_ptr &error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception &exception) {
        return exception.what();
    } catch (...) {
        return "unknown error";
    }
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

    SecureBootTransaction transaction{system.secure_boot.efi_executable};

    try {
        auto result = ApplyService{config.automation_runtime_directory}.apply(plan);
        transaction.record_apply(result);

        const auto digest = hasher_.digest(plan.target);
        (void)tools_.update_limine_image(transaction.efi_image(), digest);

        transaction.commit();
        return result;
    } catch (...) {
        const auto original_error = std::current_exception();
        try {
            transaction.rollback();
        } catch (const std::exception &rollback_error) {
            throw std::runtime_error(exception_message(original_error) +
                                     "; automatic rollback also failed: " +
                                     rollback_error.what());
        }
        std::rethrow_exception(original_error);
    }
}

} // namespace limine_manager::application
