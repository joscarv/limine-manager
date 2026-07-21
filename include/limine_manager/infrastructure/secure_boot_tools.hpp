#pragma once

#include "limine_manager/infrastructure/process.hpp"

#include <filesystem>
#include <string>

namespace limine_manager::infrastructure {

struct Blake2bDigest {
    std::string value;
};

struct SecureBootUpdateResult {
    bool signature_removed{false};
    bool configuration_enrolled{false};
    bool image_signed{false};
    bool signature_verified{false};
};

class Blake2bHasher {
  public:
    explicit Blake2bHasher(const ProcessRunner &runner) : runner_(runner) {}

    [[nodiscard]] Blake2bDigest digest(const std::filesystem::path &path) const;

  private:
    const ProcessRunner &runner_;
};

class SecureBootTools {
  public:
    explicit SecureBootTools(const ProcessRunner &runner) : runner_(runner) {}

    [[nodiscard]] SecureBootUpdateResult
    update_limine_image(const std::filesystem::path &efi, const Blake2bDigest &digest) const;

  private:
    const ProcessRunner &runner_;
};

} // namespace limine_manager::infrastructure
