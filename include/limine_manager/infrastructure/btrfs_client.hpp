#pragma once

#include "limine_manager/infrastructure/filesystem.hpp"
#include "limine_manager/infrastructure/process.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace limine_manager::infrastructure {

struct BtrfsSubvolume {
    unsigned long id {};
    std::string path;
};

class BtrfsClient {
  public:
    virtual ~BtrfsClient() = default;
    [[nodiscard]] virtual std::vector<BtrfsSubvolume>
    list_subvolumes(const std::filesystem::path &mount_point) const = 0;
    virtual void mount_top_level(const std::string &source,
                                 const std::filesystem::path &mount_point) const = 0;
    virtual void unmount(const std::filesystem::path &mount_point) const = 0;
    virtual void create_writable_snapshot(const std::filesystem::path &source,
                                          const std::filesystem::path &destination) const = 0;
    virtual void move_subvolume(const std::filesystem::path &source,
                                const std::filesystem::path &destination) const = 0;
    virtual void sync_filesystem(const std::filesystem::path &path) const = 0;
};

class PosixBtrfsClient final : public BtrfsClient {
  public:
    explicit PosixBtrfsClient(const ProcessRunner &runner) : runner_(runner) {}
    [[nodiscard]] std::vector<BtrfsSubvolume>
    list_subvolumes(const std::filesystem::path &mount_point) const override;
    void mount_top_level(const std::string &source,
                         const std::filesystem::path &mount_point) const override;
    void unmount(const std::filesystem::path &mount_point) const override;
    void create_writable_snapshot(const std::filesystem::path &source,
                                  const std::filesystem::path &destination) const override;
    void move_subvolume(const std::filesystem::path &source,
                        const std::filesystem::path &destination) const override;
    void sync_filesystem(const std::filesystem::path &path) const override;

  private:
    const ProcessRunner &runner_;
};

[[nodiscard]] std::string normalize_btrfs_source(std::string source);

} // namespace limine_manager::infrastructure
