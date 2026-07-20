#pragma once

#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/config/config.hpp"
#include "limine_manager/infrastructure/process.hpp"
#include "limine_manager/infrastructure/secure_boot_tools.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"

namespace limine_manager::application {

class SecureBootApplyService {
  public:
    explicit SecureBootApplyService(const infrastructure::ProcessRunner &runner)
        : hasher_(runner), tools_(runner) {}

    [[nodiscard]] ApplyResult apply(const ChangePlan &plan,
                                    const infrastructure::SystemInfo &system,
                                    const config::AppConfig &config) const;

  private:
    infrastructure::Blake2bHasher hasher_;
    infrastructure::SecureBootTools tools_;
};

} // namespace limine_manager::application
