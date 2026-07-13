#pragma once

#include "limine_manager/infrastructure/filesystem.hpp"

#include <filesystem>
#include <string>

namespace limine_manager::application {

enum class ChangeKind { unchanged, create, update };

struct ChangePlan {
    ChangeKind kind{ChangeKind::unchanged};
    std::filesystem::path target;
    std::string installed;
    std::string generated;

    [[nodiscard]] bool has_changes() const noexcept {
        return kind != ChangeKind::unchanged;
    }
};

class ChangePlanner {
  public:
    explicit ChangePlanner(const infrastructure::FileSystem &filesystem)
        : filesystem_(filesystem) {}
    [[nodiscard]] ChangePlan build(const std::filesystem::path &target,
                                   std::string generated) const;

  private:
    const infrastructure::FileSystem &filesystem_;
};

[[nodiscard]] std::string to_string(ChangeKind kind);

} // namespace limine_manager::application
