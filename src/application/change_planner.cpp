#include "limine_manager/application/change_planner.hpp"

namespace limine_manager::application {
namespace {
std::string normalize_newline(std::string value) {
    if (!value.empty() && value.back() != '\n') value.push_back('\n');
    return value;
}
}

ChangePlan ChangePlanner::build(const std::filesystem::path& target, std::string generated) const {
    generated = normalize_newline(std::move(generated));
    if (!filesystem_.exists(target)) {
        return {ChangeKind::create, target, {}, std::move(generated)};
    }
    const auto installed = normalize_newline(filesystem_.read_text(target));
    return {installed == generated ? ChangeKind::unchanged : ChangeKind::update,
            target, installed, std::move(generated)};
}

std::string to_string(ChangeKind kind) {
    switch (kind) {
        case ChangeKind::unchanged: return "unchanged";
        case ChangeKind::create: return "create";
        case ChangeKind::update: return "update";
    }
    return "unknown";
}
} // namespace limine_manager::application
