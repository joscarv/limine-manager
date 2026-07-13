#include "limine_manager/application/backup_service.hpp"

#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace limine_manager::application {
namespace {
std::string prefix_for(const std::filesystem::path &target) {
    return target.filename().string() + ".bak.";
}

bool is_backup(const std::filesystem::directory_entry &entry, const std::string &prefix) {
    std::error_code ec;
    if (!entry.is_regular_file(ec) || ec)
        return false;
    const auto name = entry.path().filename().string();
    return name.starts_with(prefix);
}
} // namespace

std::vector<BackupInfo> BackupService::list(const std::filesystem::path &target) const {
    const auto directory =
        target.parent_path().empty() ? std::filesystem::path(".") : target.parent_path();
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec)
        return {};

    std::vector<BackupInfo> result;
    const auto prefix = prefix_for(target);
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!is_backup(entry, prefix))
            continue;
        std::error_code size_error;
        std::error_code time_error;
        const auto size = entry.file_size(size_error);
        const auto modified = entry.last_write_time(time_error);
        if (size_error || time_error)
            continue;
        result.push_back({entry.path(), size, modified});
    }
    std::sort(result.begin(), result.end(), [](const BackupInfo &left, const BackupInfo &right) {
        if (left.modified != right.modified)
            return left.modified > right.modified;
        return left.path.filename().string() > right.path.filename().string();
    });
    return result;
}

std::optional<BackupInfo> BackupService::latest(const std::filesystem::path &target) const {
    auto backups = list(target);
    if (backups.empty())
        return std::nullopt;
    return backups.front();
}

std::size_t BackupService::prune(const std::filesystem::path &target, std::size_t retain) const {
    auto backups = list(target);
    if (retain == 0 || backups.size() <= retain)
        return 0;
    std::size_t removed = 0;
    for (std::size_t i = retain; i < backups.size(); ++i) {
        std::error_code ec;
        if (!std::filesystem::remove(backups[i].path, ec) || ec) {
            throw std::runtime_error("cannot remove old backup '" + backups[i].path.string() +
                                     "': " + ec.message());
        }
        ++removed;
    }
    return removed;
}

} // namespace limine_manager::application
