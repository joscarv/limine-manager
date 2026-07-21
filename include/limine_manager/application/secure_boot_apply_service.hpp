#pragma once

#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/config/config.hpp"
#include "limine_manager/infrastructure/efi_image_transaction.hpp"
#include "limine_manager/infrastructure/process.hpp"
#include "limine_manager/infrastructure/secure_boot_tools.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"

namespace limine_manager::application {

namespace testing {

enum class SecureBootApplyFailurePoint {
    after_config_apply,
    after_digest,
    after_efi_update,
    before_commit,
};

void inject_failure_once(SecureBootApplyFailurePoint point) noexcept;
void clear_failure_injection() noexcept;

} // namespace testing

class SecureBootApplyService {
  public:
    explicit SecureBootApplyService(
        const infrastructure::ProcessRunner &runner,
        infrastructure::RollbackErrorReporter rollback_error_reporter = {});

    [[nodiscard]] ApplyResult apply(const ChangePlan &plan,
                                    const infrastructure::SystemInfo &system,
                                    const config::AppConfig &config) const;

  private:
    infrastructure::Blake2bHasher hasher_;
    infrastructure::SecureBootTools tools_;
    infrastructure::RollbackErrorReporter rollback_error_reporter_;
};

} // namespace limine_manager::application
