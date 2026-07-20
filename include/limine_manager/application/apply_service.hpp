#pragma once

#include "limine_manager/application/change_planner.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace limine_manager::application {

struct ApplyResult {
    bool changed{false};
    std::filesystem::path target;
    std::filesystem::path backup;
};

class ApplyService {
  public:
    ApplyService();
    explicit ApplyService(std::filesystem::path runtime_directory)
        : runtime_directory_(std::move(runtime_directory)) {}

    [[nodiscard]] ApplyResult apply(const ChangePlan &plan) const;

  private:
    std::filesystem::path runtime_directory_;
};

} // namespace limine_manager::application
