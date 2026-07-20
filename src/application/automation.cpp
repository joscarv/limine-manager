#include "limine_manager/application/automation.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace limine_manager::application {
namespace {
constexpr auto marker_name = "refresh.pending";

bool is_relevant_action(std::string_view action) {
    return action == "create-snapshot-post" || action == "modify-snapshot-post" ||
           action == "delete-snapshot-post" || action == "set-read-only-post" ||
           action == "rollback-post";
}
} // namespace

SnapperPluginEvent parse_snapper_plugin_event(const std::vector<std::string> &args) {
    SnapperPluginEvent event;
    if (!args.empty())
        event.action = args[0];
    if (args.size() > 1)
        event.subvolume = args[1];
    if (args.size() > 2)
        event.fstype = args[2];
    if (args.size() > 3)
        event.extra_arguments.assign(args.begin() + 3, args.end());
    return event;
}

bool is_relevant_snapper_event(const SnapperPluginEvent &event,
                               std::string_view expected_subvolume) {
    return is_relevant_action(event.action) && event.subvolume == expected_subvolume &&
           event.fstype == "btrfs";
}

RefreshRequest RefreshRequestService::request(std::string_view source) const {
    std::filesystem::create_directories(runtime_directory_);
    const auto marker = runtime_directory_ / marker_name;
    const bool existed = std::filesystem::exists(marker);
    std::ofstream output(marker, std::ios::app);
    if (!output)
        throw std::runtime_error("cannot create refresh pending marker: " + marker.string());
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    output << now << ' ' << source << '\n';
    return {true, existed, marker};
}

bool RefreshRequestService::pending() const {
    return std::filesystem::exists(runtime_directory_ / marker_name);
}

void RefreshRequestService::clear_pending() const {
    std::error_code ec;
    std::filesystem::remove(runtime_directory_ / marker_name, ec);
    if (ec)
        throw std::runtime_error("cannot remove refresh pending marker: " +
                                 (runtime_directory_ / marker_name).string() + ": " + ec.message());
}

} // namespace limine_manager::application
