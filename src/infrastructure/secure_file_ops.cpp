#include "limine_manager/infrastructure/secure_file_ops.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace limine_manager::infrastructure {
namespace {

thread_local std::optional<testing::SecureFileFailurePoint> injected_failure;

void fail_if_injected(testing::SecureFileFailurePoint point) {
    if (!injected_failure.has_value() || *injected_failure != point)
        return;
    injected_failure.reset();
    throw std::runtime_error("injected secure file failure");
}

} // namespace

UniqueFd::UniqueFd(int fd) noexcept : fd_(fd) {}

UniqueFd::~UniqueFd() {
    if (fd_ >= 0)
        ::close(fd_);
}

UniqueFd::UniqueFd(UniqueFd &&other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept {
    if (this != &other) {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int UniqueFd::get() const noexcept {
    return fd_;
}

[[noreturn]] void throw_errno(const std::string &operation, const std::filesystem::path &path) {
    throw std::runtime_error(operation + " '" + path.string() + "': " + std::strerror(errno));
}

void write_all(int fd, std::string_view data, const std::filesystem::path &path) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto written = ::write(fd, data.data() + offset, data.size() - offset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            throw_errno("cannot write", path);
        }
        offset += static_cast<std::size_t>(written);
    }
}

std::string read_all(int fd, const std::filesystem::path &path) {
    std::string result;
    char buffer[8192];
    while (true) {
        const auto count = ::read(fd, buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            throw_errno("cannot read", path);
        }
        result.append(buffer, static_cast<std::size_t>(count));
    }
    return result;
}

std::filesystem::path unique_sibling_path(const std::filesystem::path &target,
                                          std::string_view suffix) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return target.parent_path() / (target.filename().string() + std::string(suffix) + "." +
                                   std::to_string(::getpid()) + "." + std::to_string(ticks));
}

void fsync_directory(const std::filesystem::path &directory) {
    const auto path = directory.empty() ? std::filesystem::path(".") : directory;
    UniqueFd fd(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd.get() < 0)
        throw_errno("cannot open directory", path);
    fail_if_injected(testing::SecureFileFailurePoint::before_directory_fsync);
    if (::fsync(fd.get()) < 0)
        throw_errno("cannot fsync directory", path);
}

void copy_file_secure(const std::filesystem::path &source, const std::filesystem::path &destination,
                      const struct stat &metadata, std::string_view source_description,
                      std::string_view destination_description) {
    UniqueFd input(::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (input.get() < 0)
        throw_errno("cannot open " + std::string(source_description), source);

    UniqueFd output(::open(destination.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                           metadata.st_mode & 07777));
    if (output.get() < 0)
        throw_errno("cannot create " + std::string(destination_description), destination);

    try {
        fail_if_injected(testing::SecureFileFailurePoint::after_temporary_create);
        const auto content = read_all(input.get(), source);
        write_all(output.get(), content, destination);
        if (::fchmod(output.get(), metadata.st_mode & 07777) < 0)
            throw_errno("cannot preserve mode", destination);
        if (::fchown(output.get(), metadata.st_uid, metadata.st_gid) < 0)
            throw_errno("cannot preserve ownership", destination);
        fail_if_injected(testing::SecureFileFailurePoint::before_file_fsync);
        if (::fsync(output.get()) < 0)
            throw_errno("cannot fsync", destination);
    } catch (...) {
        ::unlink(destination.c_str());
        throw;
    }
}

void atomic_restore_file(const std::filesystem::path &backup, const std::filesystem::path &target,
                         std::string_view backup_description, std::string_view target_description,
                         std::string_view temporary_suffix) {
    struct stat backup_metadata {};
    if (::lstat(backup.c_str(), &backup_metadata) < 0)
        throw_errno("cannot inspect " + std::string(backup_description), backup);
    if (S_ISLNK(backup_metadata.st_mode) || !S_ISREG(backup_metadata.st_mode))
        throw std::runtime_error("unsafe " + std::string(backup_description) + ": " +
                                 backup.string());

    struct stat target_metadata {};
    if (::lstat(target.c_str(), &target_metadata) == 0) {
        if (S_ISLNK(target_metadata.st_mode) || !S_ISREG(target_metadata.st_mode))
            throw std::runtime_error("unsafe " + std::string(target_description) + ": " +
                                     target.string());
    } else if (errno != ENOENT) {
        throw_errno("cannot inspect " + std::string(target_description), target);
    }

    const auto temporary = unique_sibling_path(target, temporary_suffix);
    try {
        copy_file_secure(backup, temporary, backup_metadata, backup_description, "rollback file");
        fail_if_injected(testing::SecureFileFailurePoint::before_rename);
        if (::rename(temporary.c_str(), target.c_str()) < 0)
            throw_errno("cannot atomically restore", target);
        fail_if_injected(testing::SecureFileFailurePoint::after_rename);
        fsync_directory(target.parent_path());
    } catch (...) {
        ::unlink(temporary.c_str());
        throw;
    }
}

void remove_regular_file_secure(const std::filesystem::path &target, std::string_view description) {
    struct stat metadata {};
    if (::lstat(target.c_str(), &metadata) < 0) {
        if (errno == ENOENT)
            return;
        throw_errno("cannot inspect " + std::string(description), target);
    }
    if (S_ISLNK(metadata.st_mode) || !S_ISREG(metadata.st_mode))
        throw std::runtime_error("unsafe " + std::string(description) + ": " + target.string());
    fail_if_injected(testing::SecureFileFailurePoint::before_unlink);
    if (::unlink(target.c_str()) < 0)
        throw_errno("cannot remove " + std::string(description), target);
    fsync_directory(target.parent_path());
}

namespace testing {

void inject_failure_once(SecureFileFailurePoint point) noexcept {
    injected_failure = point;
}

void clear_failure_injection() noexcept {
    injected_failure.reset();
}

} // namespace testing

} // namespace limine_manager::infrastructure
