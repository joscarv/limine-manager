#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace limine_manager::infrastructure {

struct DirectoryEntry {
    std::filesystem::path path;
    bool regular_file {false};
    bool directory {false};
};

class FileSystem {
  public:
    virtual ~FileSystem() = default;
    [[nodiscard]] virtual bool exists(const std::filesystem::path &path) const = 0;
    [[nodiscard]] virtual bool is_regular_file(const std::filesystem::path &path) const = 0;
    [[nodiscard]] virtual bool is_directory(const std::filesystem::path &path) const = 0;
    [[nodiscard]] virtual bool readable(const std::filesystem::path &path) const = 0;
    [[nodiscard]] virtual std::string read_text(const std::filesystem::path &path) const = 0;
    [[nodiscard]] virtual std::vector<DirectoryEntry>
    list_directory(const std::filesystem::path &path) const = 0;
    [[nodiscard]] virtual std::filesystem::path
    canonical(const std::filesystem::path &path) const = 0;
};

class RealFileSystem final : public FileSystem {
  public:
    [[nodiscard]] bool exists(const std::filesystem::path &path) const override;
    [[nodiscard]] bool is_regular_file(const std::filesystem::path &path) const override;
    [[nodiscard]] bool is_directory(const std::filesystem::path &path) const override;
    [[nodiscard]] bool readable(const std::filesystem::path &path) const override;
    [[nodiscard]] std::string read_text(const std::filesystem::path &path) const override;
    [[nodiscard]] std::vector<DirectoryEntry>
    list_directory(const std::filesystem::path &path) const override;
    [[nodiscard]] std::filesystem::path canonical(const std::filesystem::path &path) const override;
};

} // namespace limine_manager::infrastructure
