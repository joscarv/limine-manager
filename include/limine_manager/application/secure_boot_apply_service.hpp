#pragma once

#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/config/config.hpp"
#include "limine_manager/infrastructure/process.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"

namespace limine_manager::application {

class SecureBootApplyService {
  public:
    explicit SecureBootApplyService(const infrastructure::ProcessRunner &runner) : runner_(runner) {}
    [[nodiscard]] ApplyResult apply(const ChangePlan &plan,
                                    const infrastructure::SystemInfo &system,
                                    const config::AppConfig &config) const;

  private:
    const infrastructure::ProcessRunner &runner_;
};

} // namespace limine_manager::application
