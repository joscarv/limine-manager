#pragma once

#include "limine_manager/application/change_planner.hpp"

#include <filesystem>
#include <string>

namespace limine_manager::application {

struct ApplyResult {
    bool changed{false};
    std::filesystem::path target;
    std::filesystem::path backup;
};

class ApplyService {
  public:
    [[nodiscard]] ApplyResult apply(const ChangePlan &plan) const;
};

} // namespace limine_manager::application
