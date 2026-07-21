#include "limine_manager/application/secure_boot_apply_service.hpp"

#include "limine_manager/application/secure_boot_transaction.hpp"

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace limine_manager::application {
namespace {

thread_local std::optional<testing::SecureBootApplyFailurePoint> injected_failure;

void maybe_fail(testing::SecureBootApplyFailurePoint point) {
    if (!injected_failure.has_value() || *injected_failure != point)
        return;
    injected_failure.reset();
    throw std::runtime_error("injected Secure Boot apply failure");
}

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

namespace testing {

void inject_failure_once(SecureBootApplyFailurePoint point) noexcept {
    injected_failure = point;
}

void clear_failure_injection() noexcept {
    injected_failure.reset();
}

} // namespace testing

SecureBootApplyService::SecureBootApplyService(
    const infrastructure::ProcessRunner &runner,
    infrastructure::RollbackErrorReporter rollback_error_reporter)
    : hasher_(runner), tools_(runner),
      rollback_error_reporter_(std::move(rollback_error_reporter)) {}

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

    SecureBootTransaction transaction{system.secure_boot.efi_executable, rollback_error_reporter_};

    try {
        auto result = ApplyService{config.automation_runtime_directory}.apply(plan);
        transaction.record_apply(result);
        maybe_fail(testing::SecureBootApplyFailurePoint::after_config_apply);

        const auto digest = hasher_.digest(plan.target);
        maybe_fail(testing::SecureBootApplyFailurePoint::after_digest);

        (void)tools_.update_limine_image(transaction.efi_image(), digest);
        maybe_fail(testing::SecureBootApplyFailurePoint::after_efi_update);
        maybe_fail(testing::SecureBootApplyFailurePoint::before_commit);

        transaction.commit();
        return result;
    } catch (...) {
        const auto original_error = std::current_exception();
        try {
            transaction.rollback();
        } catch (const std::exception &rollback_error) {
            throw std::runtime_error(exception_message(original_error) +
                                     "; automatic rollback also failed: " + rollback_error.what());
        }
        std::rethrow_exception(original_error);
    }
}

} // namespace limine_manager::application
