#pragma once

#include "limine_manager/domain/menu.hpp"
#include <string>

namespace limine_manager::render {

class LimineRenderer {
public:
    std::string render(const domain::MenuDocument& document) const;
};

} // namespace limine_manager::render
