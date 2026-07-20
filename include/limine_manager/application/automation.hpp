#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace limine_manager::application {

struct SnapperPluginEvent {
    std::string action;
    std::string subvolume;
    std::string fstype;
    std::vector<std::string> extra_arguments;
};

[[nodiscard]] SnapperPluginEvent parse_snapper_plugin_event(const std::vector<std::string> &args);
[[nodiscard]] bool is_relevant_snapper_event(const SnapperPluginEvent &event,
                                             std::string_view expected_subvolume = "/");

struct RefreshRequest {
    bool requested{false};
    bool coalesced{false};
    std::filesystem::path pending_marker;
};

class RefreshRequestService {
  public:
    explicit RefreshRequestService(std::filesystem::path runtime_directory)
        : runtime_directory_(std::move(runtime_directory)) {}

    [[nodiscard]] RefreshRequest request(std::string_view source) const;
    [[nodiscard]] bool pending() const;
    void clear_pending() const;

    [[nodiscard]] const std::filesystem::path &runtime_directory() const noexcept {
        return runtime_directory_;
    }

  private:
    std::filesystem::path runtime_directory_;
};

} // namespace limine_manager::application
