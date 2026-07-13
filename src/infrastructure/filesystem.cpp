#include "limine_manager/infrastructure/filesystem.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace limine_manager::infrastructure {

bool RealFileSystem::exists(const std::filesystem::path& path) const {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

bool RealFileSystem::is_regular_file(const std::filesystem::path& path) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool RealFileSystem::is_directory(const std::filesystem::path& path) const {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec) && !ec;
}

bool RealFileSystem::readable(const std::filesystem::path& path) const {
    if (!is_regular_file(path)) return false;
    std::ifstream input(path);
    return input.good();
}

std::string RealFileSystem::read_text(const std::filesystem::path& path) const {
    std::ifstream input(path);
    if (!input) return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::vector<DirectoryEntry> RealFileSystem::list_directory(const std::filesystem::path& path) const {
    std::vector<DirectoryEntry> result;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        if (ec) break;
        std::error_code type_error;
        result.push_back({entry.path(), entry.is_regular_file(type_error) && !type_error,
                          entry.is_directory(type_error) && !type_error});
    }
    return result;
}

std::filesystem::path RealFileSystem::canonical(const std::filesystem::path& path) const {
    std::error_code ec;
    auto resolved = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : resolved;
}

} // namespace limine_manager::infrastructure
