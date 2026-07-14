#include "limine_manager/application/rollback_service.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <unistd.h>

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
    UniqueFd(UniqueFd &&other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    UniqueFd &operator=(UniqueFd &&other) noexcept {
        if (this != &other) {
            if (fd_ >= 0)
                ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    [[nodiscard]] int get() const {
        return fd_;
    }

  private:
    int fd_;
};

[[noreturn]] void fail_errno(const std::string &operation, const std::filesystem::path &path) {
    throw std::runtime_error(operation + " '" + path.string() + "': " + std::strerror(errno));
}

bool contains_subvolume(const std::vector<infrastructure::BtrfsSubvolume> &subvolumes,
                        const std::string &path) {
    return std::any_of(subvolumes.begin(), subvolumes.end(),
                       [&](const auto &item) { return item.path == path; });
}

std::filesystem::path under(const std::filesystem::path &root, const std::string &relative) {
    return root / std::filesystem::path(relative).relative_path();
}

void try_move(const infrastructure::BtrfsClient &btrfs, const std::filesystem::path &source,
              const std::filesystem::path &destination) noexcept {
    try {
        btrfs.move_subvolume(source, destination);
    } catch (...) {
    }
}
} // namespace

RollbackResult RollbackService::execute(const domain::RollbackPlan &plan) const {
    if (!plan.eligible)
        throw std::runtime_error("rollback plan is not eligible");
    if (plan.btrfs_source.empty() || plan.source_snapshot_subvolume.empty() ||
        plan.target_subvolume.empty() || plan.preserved_subvolume.empty() ||
        plan.replacement_subvolume.empty()) {
        throw std::runtime_error("rollback plan is incomplete");
    }

    std::filesystem::create_directories(options_.runtime_directory);
    const auto lock_path = options_.runtime_directory / "rollback.lock";
    UniqueFd lock_fd(::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (lock_fd.get() < 0)
        fail_errno("cannot open rollback lock", lock_path);
    if (::flock(lock_fd.get(), LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK)
            throw std::runtime_error("another limine-manager rollback operation is active");
        fail_errno("cannot lock rollback", lock_path);
    }

    const auto mount_point = options_.runtime_directory / "btrfs-top";
    bool mounted = false;
    bool replacement_created = false;
    bool target_preserved = false;
    bool target_switched = false;

    const auto source_path = under(mount_point, plan.source_snapshot_subvolume);
    const auto target_path = under(mount_point, plan.target_subvolume);
    const auto preserved_path = under(mount_point, plan.preserved_subvolume);
    const auto replacement_path = under(mount_point, plan.replacement_subvolume);
    const auto failed_path =
        under(mount_point, plan.target_subvolume + ".limine-manager.failed-replacement");

    try {
        btrfs_.mount_top_level(plan.btrfs_source, mount_point);
        mounted = true;

        btrfs_.create_writable_snapshot(source_path, replacement_path);
        replacement_created = true;

        btrfs_.move_subvolume(target_path, preserved_path);
        target_preserved = true;

        btrfs_.move_subvolume(replacement_path, target_path);
        target_switched = true;

        const auto subvolumes = btrfs_.list_subvolumes(mount_point);
        if (!contains_subvolume(subvolumes, plan.target_subvolume) ||
            !contains_subvolume(subvolumes, plan.preserved_subvolume)) {
            throw std::runtime_error("post-rollback Btrfs topology verification failed");
        }
        btrfs_.sync_filesystem(mount_point);
        btrfs_.unmount(mount_point);
        mounted = false;

        return {mount_point, plan.preserved_subvolume, plan.target_subvolume};
    } catch (...) {
        if (mounted) {
            if (target_switched) {
                try_move(btrfs_, target_path, failed_path);
                try_move(btrfs_, preserved_path, target_path);
            } else if (target_preserved) {
                if (replacement_created)
                    try_move(btrfs_, replacement_path, failed_path);
                try_move(btrfs_, preserved_path, target_path);
            } else if (replacement_created) {
                try_move(btrfs_, replacement_path, failed_path);
            }
            try {
                btrfs_.sync_filesystem(mount_point);
            } catch (...) {
            }
            try {
                btrfs_.unmount(mount_point);
            } catch (...) {
            }
        }
        throw;
    }
}

} // namespace limine_manager::application
