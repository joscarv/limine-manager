#pragma once

#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/infrastructure/efi_image_transaction.hpp"

#include <filesystem>
#include <optional>

namespace limine_manager::application {

class SecureBootTransaction {
  public:
    explicit SecureBootTransaction(std::filesystem::path efi_image);
    ~SecureBootTransaction();

    SecureBootTransaction(const SecureBootTransaction &) = delete;
    SecureBootTransaction &operator=(const SecureBootTransaction &) = delete;
    SecureBootTransaction(SecureBootTransaction &&) = delete;
    SecureBootTransaction &operator=(SecureBootTransaction &&) = delete;

    [[nodiscard]] const std::filesystem::path &efi_image() const noexcept;
    void record_apply(ApplyResult result);
    void commit();
    void rollback();

  private:
    void rollback_config();

    infrastructure::EfiImageTransaction efi_transaction_;
    std::optional<ApplyResult> apply_result_;
    bool active_{true};
};

} // namespace limine_manager::application
