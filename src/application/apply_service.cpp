#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/infrastructure/secure_file_ops.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace limine_manager::application {
namespace {

using infrastructure::copy_file_secure;
using infrastructure::fsync_directory;
using infrastructure::read_all;
using infrastructure::throw_errno;
using infrastructure::unique_sibling_path;
using infrastructure::UniqueFd;
using infrastructure::write_all;

std::filesystem::path default_runtime_directory() {
    if (::geteuid() == 0)
        return "/run/limine-manager";
    if (const char *xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
        xdg_runtime != nullptr && *xdg_runtime != '\0')
        return std::filesystem::path(xdg_runtime) / "limine-manager";
    return std::filesystem::temp_directory_path() /
           ("limine-manager-" + std::to_string(::geteuid()));
}

std::string normalize_newline(std::string value) {
    if (!value.empty() && value.back() != '\n')
        value.push_back('\n');
    return value;
}

void restore_from_backup(const std::filesystem::path &backup, const std::filesystem::path &target,
                         const struct stat &metadata) {
    const auto rollback = unique_sibling_path(target, ".rollback");
    try {
        copy_file_secure(backup, rollback, metadata, "backup", "rollback file");
        if (::rename(rollback.c_str(), target.c_str()) < 0)
            throw_errno("cannot restore backup", target);
        fsync_directory(target.parent_path());
    } catch (...) {
        ::unlink(rollback.c_str());
        throw;
    }
}

} // namespace

ApplyService::ApplyService() : runtime_directory_(default_runtime_directory()) {}

ApplyResult ApplyService::apply(const ChangePlan &plan) const {
    if (!plan.has_changes())
        return {false, plan.target, {}};
    if (plan.target.empty() || plan.target.filename().empty())
        throw std::runtime_error("invalid empty target path");

    const auto directory =
        plan.target.parent_path().empty() ? std::filesystem::path(".") : plan.target.parent_path();
    if (runtime_directory_.empty())
        throw std::runtime_error("apply runtime directory cannot be empty");

    std::error_code runtime_error;
    std::filesystem::create_directories(runtime_directory_, runtime_error);
    if (runtime_error)
        throw std::runtime_error("cannot create runtime directory '" + runtime_directory_.string() +
                                 "': " + runtime_error.message());

    struct stat runtime_metadata{};
    if (::lstat(runtime_directory_.c_str(), &runtime_metadata) < 0)
        throw_errno("cannot inspect runtime directory", runtime_directory_);
    if (S_ISLNK(runtime_metadata.st_mode) || !S_ISDIR(runtime_metadata.st_mode))
        throw std::runtime_error("unsafe apply runtime directory: " + runtime_directory_.string());

    const auto lock_path = runtime_directory_ / "apply.lock";
    UniqueFd lock_fd(::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (lock_fd.get() < 0)
        throw_errno("cannot open lock", lock_path);
    if (::flock(lock_fd.get(), LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK)
            throw std::runtime_error("another limine-manager apply operation is active");
        throw_errno("cannot lock", lock_path);
    }

    struct stat target_metadata{};
    bool target_exists = false;
    if (::lstat(plan.target.c_str(), &target_metadata) == 0) {
        target_exists = true;
        if (S_ISLNK(target_metadata.st_mode))
            throw std::runtime_error("refusing to replace symbolic link: " + plan.target.string());
        if (!S_ISREG(target_metadata.st_mode))
            throw std::runtime_error("target is not a regular file: " + plan.target.string());
    } else if (errno != ENOENT) {
        throw_errno("cannot inspect target", plan.target);
    }

    if (plan.kind == ChangeKind::create && target_exists)
        throw std::runtime_error("target appeared after planning; run plan again");
    if (plan.kind == ChangeKind::update && !target_exists)
        throw std::runtime_error("target disappeared after planning; run plan again");

    if (target_exists) {
        UniqueFd current(::open(plan.target.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (current.get() < 0)
            throw_errno("cannot open target", plan.target);
        if (normalize_newline(read_all(current.get(), plan.target)) != plan.installed)
            throw std::runtime_error("target changed after planning; refusing to overwrite");
    }

    const mode_t mode = target_exists ? (target_metadata.st_mode & 07777) : 0644;
    const uid_t uid = target_exists ? target_metadata.st_uid : ::geteuid();
    const gid_t gid = target_exists ? target_metadata.st_gid : ::getegid();
    const auto temporary = unique_sibling_path(plan.target, ".tmp");
    std::filesystem::path backup;

    try {
        if (target_exists) {
            const auto backup_candidate = unique_sibling_path(plan.target, ".bak");
            copy_file_secure(plan.target, backup_candidate, target_metadata, "source", "backup");
            fsync_directory(directory);
            backup = backup_candidate;
        }

        UniqueFd output(
            ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode));
        if (output.get() < 0)
            throw_errno("cannot create temporary file", temporary);
        write_all(output.get(), plan.generated, temporary);
        if (::fchmod(output.get(), mode) < 0)
            throw_errno("cannot set mode", temporary);
        struct stat temporary_metadata{};
        if (::fstat(output.get(), &temporary_metadata) < 0)
            throw_errno("cannot inspect temporary file", temporary);
        if (temporary_metadata.st_uid != uid || temporary_metadata.st_gid != gid) {
            if (::fchown(output.get(), uid, gid) < 0)
                throw_errno("cannot set ownership", temporary);
        }
        if (::fsync(output.get()) < 0)
            throw_errno("cannot fsync", temporary);

        if (::rename(temporary.c_str(), plan.target.c_str()) < 0)
            throw_errno("cannot atomically replace target", plan.target);
        fsync_directory(directory);

        UniqueFd verify(::open(plan.target.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (verify.get() < 0)
            throw_errno("cannot reopen target", plan.target);
        if (read_all(verify.get(), plan.target) != plan.generated)
            throw std::runtime_error("post-write verification failed for " + plan.target.string());

        return {true, plan.target, backup};
    } catch (...) {
        ::unlink(temporary.c_str());
        if (target_exists && !backup.empty()) {
            try {
                restore_from_backup(backup, plan.target, target_metadata);
            } catch (...) {
                throw std::runtime_error(
                    "apply failed and automatic rollback also failed; backup: " + backup.string());
            }
        } else if (!target_exists) {
            ::unlink(plan.target.c_str());
            try {
                fsync_directory(directory);
            } catch (...) {
            }
        }
        throw;
    }
}

} // namespace limine_manager::application
