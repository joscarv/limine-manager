#include "limine_manager/application/secure_boot_transaction.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace limine_manager::application {
namespace {

class UniqueFd {
  public:
    explicit UniqueFd(int fd = -1) : fd_(fd) {}
    ~UniqueFd() {
        if (fd_ >= 0)
            ::close(fd_);
    }

    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

  private:
    int fd_;
};

[[noreturn]] void fail(const std::string &operation, const std::filesystem::path &path) {
    throw std::runtime_error(operation + " '" + path.string() + "': " + std::strerror(errno));
}

std::filesystem::path unique_path(const std::filesystem::path &target, std::string_view suffix) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return target.parent_path() /
           (target.filename().string() + std::string(suffix) + "." +
            std::to_string(::getpid()) + "." + std::to_string(ticks));
}

void fsync_directory(const std::filesystem::path &directory) {
    const auto path = directory.empty() ? std::filesystem::path(".") : directory;
    UniqueFd fd(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd.get() < 0)
        fail("cannot open directory", path);
    if (::fsync(fd.get()) < 0)
        fail("cannot fsync directory", path);
}

void copy_all(int input, int output, const std::filesystem::path &source,
              const std::filesystem::path &destination) {
    char buffer[8192];
    while (true) {
        const auto count = ::read(input, buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            fail("cannot read backup", source);
        }

        std::size_t offset = 0;
        while (offset < static_cast<std::size_t>(count)) {
            const auto written =
                ::write(output, buffer + offset, static_cast<std::size_t>(count) - offset);
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                fail("cannot write rollback file", destination);
            }
            offset += static_cast<std::size_t>(written);
        }
    }
}

void restore_configuration(const std::filesystem::path &backup,
                           const std::filesystem::path &target) {
    struct stat backup_metadata {};
    if (::lstat(backup.c_str(), &backup_metadata) < 0)
        fail("cannot inspect configuration backup", backup);
    if (S_ISLNK(backup_metadata.st_mode) || !S_ISREG(backup_metadata.st_mode))
        throw std::runtime_error("unsafe configuration backup: " + backup.string());

    struct stat target_metadata {};
    if (::lstat(target.c_str(), &target_metadata) == 0) {
        if (S_ISLNK(target_metadata.st_mode) || !S_ISREG(target_metadata.st_mode))
            throw std::runtime_error("unsafe configuration rollback target: " + target.string());
    } else if (errno != ENOENT) {
        fail("cannot inspect configuration rollback target", target);
    }

    const auto temporary = unique_path(target, ".rollback");
    UniqueFd input(::open(backup.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (input.get() < 0)
        fail("cannot open configuration backup", backup);

    UniqueFd output(::open(temporary.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                           backup_metadata.st_mode & 07777));
    if (output.get() < 0)
        fail("cannot create configuration rollback file", temporary);

    try {
        copy_all(input.get(), output.get(), backup, temporary);
        if (::fchmod(output.get(), backup_metadata.st_mode & 07777) < 0)
            fail("cannot preserve configuration mode", temporary);
        if (::fchown(output.get(), backup_metadata.st_uid, backup_metadata.st_gid) < 0)
            fail("cannot preserve configuration ownership", temporary);
        if (::fsync(output.get()) < 0)
            fail("cannot fsync configuration rollback file", temporary);
        if (::rename(temporary.c_str(), target.c_str()) < 0)
            fail("cannot atomically restore configuration", target);
        fsync_directory(target.parent_path());
    } catch (...) {
        ::unlink(temporary.c_str());
        throw;
    }
}

void remove_created_configuration(const std::filesystem::path &target) {
    struct stat metadata {};
    if (::lstat(target.c_str(), &metadata) < 0) {
        if (errno == ENOENT)
            return;
        fail("cannot inspect newly created configuration", target);
    }
    if (S_ISLNK(metadata.st_mode) || !S_ISREG(metadata.st_mode))
        throw std::runtime_error("unsafe newly created configuration: " + target.string());
    if (::unlink(target.c_str()) < 0)
        fail("cannot remove newly created configuration", target);
    fsync_directory(target.parent_path());
}

} // namespace

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
        restore_configuration(apply_result_->backup, apply_result_->target);
        return;
    }

    remove_created_configuration(apply_result_->target);
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

} // namespace limine_manager::application {
