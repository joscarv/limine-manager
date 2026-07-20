#include "limine_manager/infrastructure/efi_image_transaction.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace limine_manager::infrastructure {
namespace {

std::filesystem::path backup_name(const std::filesystem::path &image) {
    const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("limine-manager-" + image.filename().string() + "." +
            std::to_string(::getpid()) + "." + std::to_string(ticks) + ".bak");
}

} // namespace

EfiImageTransaction::EfiImageTransaction(std::filesystem::path image)
    : image_(std::move(image)), backup_(backup_name(image_)) {
    std::error_code ec;
    const bool copied = std::filesystem::copy_file(
        image_, backup_, std::filesystem::copy_options::none, ec);
    if (!copied || ec)
        throw std::runtime_error("failed to back up EFI image " + image_.string() +
                                 ": " + ec.message());
}

EfiImageTransaction::~EfiImageTransaction() {
    if (!active_)
        return;

    std::error_code ec;
    std::filesystem::copy_file(backup_, image_,
                               std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(backup_, ec);
}

void EfiImageTransaction::commit() {
    if (!active_)
        return;

    std::error_code ec;
    const bool removed = std::filesystem::remove(backup_, ec);
    if (!removed || ec)
        throw std::runtime_error("failed to remove EFI image backup " + backup_.string() +
                                 ": " + ec.message());
    active_ = false;
}

void EfiImageTransaction::rollback() {
    if (!active_)
        return;

    std::error_code ec;
    const bool restored = std::filesystem::copy_file(
        backup_, image_, std::filesystem::copy_options::overwrite_existing, ec);
    if (!restored || ec)
        throw std::runtime_error("failed to restore EFI image " + image_.string() +
                                 ": " + ec.message());

    ec.clear();
    const bool removed = std::filesystem::remove(backup_, ec);
    if (!removed || ec)
        throw std::runtime_error("failed to remove EFI image backup " + backup_.string() +
                                 ": " + ec.message());

    active_ = false;
}

} // namespace limine_manager::infrastructure
