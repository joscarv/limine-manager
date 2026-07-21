#pragma once

#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/infrastructure/efi_image_transaction.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace limine_manager::application {

class SecureBootTransaction {
  public:
    explicit SecureBootTransaction(std::filesystem::path efi_image,
                                   infrastructure::RollbackErrorReporter error_reporter = {});
    ~SecureBootTransaction() noexcept;

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
    void report_destructor_error(std::string_view message) const noexcept;
    [[nodiscard]] bool rollback_pending() const noexcept;

    infrastructure::RollbackErrorReporter error_reporter_;
    infrastructure::EfiImageTransaction efi_transaction_;
    std::optional<ApplyResult> apply_result_;
    bool config_rollback_pending_{false};
    bool efi_rollback_pending_{true};
    bool committed_{false};
};

} // namespace limine_manager::application
