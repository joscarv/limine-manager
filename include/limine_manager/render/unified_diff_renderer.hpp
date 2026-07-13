#pragma once

#include "limine_manager/application/change_planner.hpp"
#include <string>

namespace limine_manager::render {
class UnifiedDiffRenderer {
public:
    [[nodiscard]] std::string render(const application::ChangePlan& plan) const;
};
} // namespace limine_manager::render
