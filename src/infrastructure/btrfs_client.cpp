#include "limine_manager/infrastructure/btrfs_client.hpp"

#include <charconv>
#include <filesystem>
#include <stdexcept>

namespace limine_manager::infrastructure {
namespace {
std::string trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

void require_success(const ProcessResult &result, const std::string &operation) {
    if (result.exit_code != 0)
        throw std::runtime_error(operation + " failed: " + trim(result.output));
}

template <typename Callback> void for_each_line(const std::string &text, Callback callback) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find('\n', begin);
        auto line = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            callback(line);
        begin = end == std::string::npos ? text.size() : end + 1;
    }
}

std::string field_after(const std::string &line, const std::string &marker) {
    const auto pos = line.find(marker);
    if (pos == std::string::npos)
        return {};
    return line.substr(pos + marker.size());
}
} // namespace

std::string normalize_btrfs_source(std::string source) {
    const auto suffix = source.find('[');
    if (suffix != std::string::npos && source.ends_with(']'))
        source.erase(suffix);
    return source;
}

std::vector<BtrfsSubvolume>
PosixBtrfsClient::list_subvolumes(const std::filesystem::path &mount_point) const {
    const auto result = runner_.run({"btrfs", "subvolume", "list", mount_point.string()});
    require_success(result, "btrfs subvolume list");

    std::vector<BtrfsSubvolume> subvolumes;
    for_each_line(result.output, [&](const std::string &line) {
        const auto id_text = field_after(line, "ID ");
        if (id_text.empty())
            return;
        const auto id_end = id_text.find(' ');
        if (id_end == std::string::npos)
            return;
        unsigned long id {};
        const auto id_value = id_text.substr(0, id_end);
        const auto parsed = std::from_chars(id_value.data(), id_value.data() + id_value.size(), id);
        if (parsed.ec != std::errc {} || parsed.ptr != id_value.data() + id_value.size())
            return;

        auto path = field_after(line, " path ");
        if (path.empty())
            return;
        if (!path.empty() && path.front() == '/')
            path.erase(0, 1);
        subvolumes.push_back({id, path});
    });
    return subvolumes;
}

void PosixBtrfsClient::mount_top_level(const std::string &source,
                                       const std::filesystem::path &mount_point) const {
    std::filesystem::create_directories(mount_point);
    require_success(runner_.run({"mount", "-o", "subvolid=5", source, mount_point.string()}),
                    "mount Btrfs top-level");
}

void PosixBtrfsClient::unmount(const std::filesystem::path &mount_point) const {
    require_success(runner_.run({"umount", mount_point.string()}), "umount Btrfs top-level");
}

void PosixBtrfsClient::create_writable_snapshot(const std::filesystem::path &source,
                                                const std::filesystem::path &destination) const {
    require_success(
        runner_.run({"btrfs", "subvolume", "snapshot", source.string(), destination.string()}),
        "btrfs subvolume snapshot");
}

void PosixBtrfsClient::move_subvolume(const std::filesystem::path &source,
                                      const std::filesystem::path &destination) const {
    std::filesystem::rename(source, destination);
}

void PosixBtrfsClient::sync_filesystem(const std::filesystem::path &path) const {
    require_success(runner_.run({"btrfs", "filesystem", "sync", path.string()}),
                    "btrfs filesystem sync");
}

} // namespace limine_manager::infrastructure
