#include "limine_manager/application/secure_boot_transaction.hpp"
#include "limine_manager/infrastructure/secure_file_ops.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace limine_manager::application {

SecureBootTransaction::SecureBootTransaction(std::filesystem::path efi_image,
                                             infrastructure::RollbackErrorReporter error_reporter)
    : error_reporter_(std::move(error_reporter)),
      efi_transaction_(std::move(efi_image), error_reporter_) {}

SecureBootTransaction::~SecureBootTransaction() noexcept {
    if (committed_ || !rollback_pending())
        return;
    try {
        rollback();
    } catch (const std::exception &error) {
        report_destructor_error(std::string("Secure Boot rollback failed during destruction: ") +
                                error.what());
    } catch (...) {
        report_destructor_error("Secure Boot rollback failed during destruction: unknown error");
    }
}

void SecureBootTransaction::report_destructor_error(std::string_view message) const noexcept {
    if (!error_reporter_)
        return;
    try {
        error_reporter_(message);
    } catch (...) {
    }
}

const std::filesystem::path &SecureBootTransaction::efi_image() const noexcept {
    return efi_transaction_.image();
}

void SecureBootTransaction::record_apply(ApplyResult result) {
    if (committed_ || !efi_rollback_pending_)
        throw std::logic_error("cannot record apply result on inactive Secure Boot transaction");
    if (apply_result_.has_value())
        throw std::logic_error("apply result already recorded for Secure Boot transaction");

    config_rollback_pending_ = result.changed;
    apply_result_ = std::move(result);
}

void SecureBootTransaction::commit() {
    if (committed_)
        return;
    if (!rollback_pending())
        throw std::logic_error("cannot commit a rolled back Secure Boot transaction");

    efi_transaction_.commit();
    efi_rollback_pending_ = false;
    config_rollback_pending_ = false;
    committed_ = true;
}

void SecureBootTransaction::rollback_config() {
    if (!config_rollback_pending_)
        return;
    if (!apply_result_.has_value() || !apply_result_->changed)
        throw std::logic_error("configuration rollback state is inconsistent");

    if (!apply_result_->backup.empty()) {
        infrastructure::atomic_restore_file(apply_result_->backup, apply_result_->target,
                                            "configuration backup",
                                            "configuration rollback target");
    } else {
        infrastructure::remove_regular_file_secure(apply_result_->target,
                                                   "newly created configuration");
    }

    config_rollback_pending_ = false;
}

bool SecureBootTransaction::rollback_pending() const noexcept {
    return config_rollback_pending_ || efi_rollback_pending_;
}

void SecureBootTransaction::rollback() {
    if (committed_ || !rollback_pending())
        return;

    std::string errors;
    if (config_rollback_pending_) {
        try {
            rollback_config();
        } catch (const std::exception &error) {
            errors = error.what();
        }
    }

    if (efi_rollback_pending_) {
        try {
            efi_transaction_.rollback();
            efi_rollback_pending_ = false;
        } catch (const std::exception &error) {
            if (!errors.empty())
                errors += "; ";
            errors += error.what();
        }
    }

    if (!errors.empty())
        throw std::runtime_error("Secure Boot rollback failed: " + errors);
}

} // namespace limine_manager::application
