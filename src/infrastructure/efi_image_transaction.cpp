#include "limine_manager/infrastructure/efi_image_transaction.hpp"

#include "limine_manager/infrastructure/secure_file_ops.hpp"

#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace limine_manager::infrastructure {
namespace {

std::filesystem::path backup_name(const std::filesystem::path &image) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("limine-manager-" + image.filename().string() + "." +
            std::to_string(::getpid()) + "." + std::to_string(ticks) + ".bak");
}

struct stat inspect_image(const std::filesystem::path &image) {
    struct stat metadata {};
    if (::lstat(image.c_str(), &metadata) < 0)
        throw_errno("cannot inspect EFI image", image);
    if (S_ISLNK(metadata.st_mode) || !S_ISREG(metadata.st_mode))
        throw std::runtime_error("unsafe EFI image: " + image.string());
    return metadata;
}

} // namespace

EfiImageTransaction::EfiImageTransaction(std::filesystem::path image)
    : image_(std::move(image)), backup_(backup_name(image_)) {
    const auto metadata = inspect_image(image_);
    copy_file_secure(image_, backup_, metadata, "EFI image", "EFI image backup");
    fsync_directory(backup_.parent_path());
}

EfiImageTransaction::~EfiImageTransaction() {
    if (!active_)
        return;
    try {
        rollback();
    } catch (...) {
    }
}

void EfiImageTransaction::commit() {
    if (!active_)
        return;

    remove_regular_file_secure(backup_, "EFI image backup");
    active_ = false;
}

void EfiImageTransaction::rollback() {
    if (!active_)
        return;

    atomic_restore_file(backup_, image_, "EFI image backup", "EFI image rollback target");
    remove_regular_file_secure(backup_, "EFI image backup");
    active_ = false;
}

} // namespace limine_manager::infrastructure {
