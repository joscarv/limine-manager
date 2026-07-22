#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace limine_manager::infrastructure {

class UniqueFd {
  public:
    explicit UniqueFd(int fd = -1) noexcept;
    ~UniqueFd();

    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;
    UniqueFd(UniqueFd &&other) noexcept;
    UniqueFd &operator=(UniqueFd &&other) noexcept;

    [[nodiscard]] int get() const noexcept;

  private:
    int fd_;
};

[[noreturn]] void throw_errno(const std::string &operation, const std::filesystem::path &path);
void write_all(int fd, std::string_view data, const std::filesystem::path &path);
[[nodiscard]] std::string read_all(int fd, const std::filesystem::path &path);
[[nodiscard]] std::filesystem::path unique_sibling_path(const std::filesystem::path &target,
                                                        std::string_view suffix);
void fsync_directory(const std::filesystem::path &directory);

void copy_file_secure(const std::filesystem::path &source, const std::filesystem::path &destination,
                      const struct stat &metadata, std::string_view source_description = "source",
                      std::string_view destination_description = "destination");

void atomic_restore_file(const std::filesystem::path &backup, const std::filesystem::path &target,
                         std::string_view backup_description = "backup",
                         std::string_view target_description = "rollback target",
                         std::string_view temporary_suffix = ".rollback");

void remove_regular_file_secure(const std::filesystem::path &target, std::string_view description);

namespace testing {

enum class SecureFileFailurePoint {
    after_temporary_create,
    before_file_fsync,
    before_rename,
    after_rename,
    before_directory_fsync,
    before_unlink,
};

void inject_failure_once(SecureFileFailurePoint point) noexcept;
void clear_failure_injection() noexcept;

} // namespace testing

} // namespace limine_manager::infrastructure
