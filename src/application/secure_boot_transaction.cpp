#include "limine_manager/application/secure_boot_transaction.hpp"
#include "limine_manager/infrastructure/secure_file_ops.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace limine_manager::application {

SecureBootTransaction::SecureBootTransaction(std::filesystem::path efi_image)
    : efi_transaction_(std::move(efi_image)) {}

SecureBootTransaction::~SecureBootTransaction() {
    if (!active_)
        return;
    try {
        rollback();
    } catch (...) {
    }
}

const std::filesystem::path &SecureBootTransaction::efi_image() const noexcept {
    return efi_transaction_.image();
}

void SecureBootTransaction::record_apply(ApplyResult result) {
    if (!active_)
        throw std::logic_error("cannot record apply result on inactive Secure Boot transaction");
    if (apply_result_.has_value())
        throw std::logic_error("apply result already recorded for Secure Boot transaction");
    apply_result_ = std::move(result);
}

void SecureBootTransaction::commit() {
    if (!active_)
        return;
    efi_transaction_.commit();
    active_ = false;
}

void SecureBootTransaction::rollback_config() {
    if (!apply_result_.has_value() || !apply_result_->changed)
        return;

    if (!apply_result_->backup.empty()) {
        infrastructure::atomic_restore_file(apply_result_->backup, apply_result_->target,
                                            "configuration backup",
                                            "configuration rollback target");
        return;
    }

    infrastructure::remove_regular_file_secure(apply_result_->target,
                                               "newly created configuration");
}

void SecureBootTransaction::rollback() {
    if (!active_)
        return;

    std::string errors;
    try {
        rollback_config();
    } catch (const std::exception &error) {
        errors = error.what();
    }

    try {
        efi_transaction_.rollback();
    } catch (const std::exception &error) {
        if (!errors.empty())
            errors += "; ";
        errors += error.what();
    }

    active_ = false;
    if (!errors.empty())
        throw std::runtime_error("Secure Boot rollback failed: " + errors);
}

} // namespace limine_manager::application
