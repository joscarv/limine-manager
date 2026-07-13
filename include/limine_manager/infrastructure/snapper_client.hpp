#pragma once

#include "limine_manager/infrastructure/process.hpp"

#include <string>
#include <vector>

namespace limine_manager::infrastructure {

struct SnapperConfig {
    std::string name;
    std::string subvolume;
    std::string filesystem_type;
};

struct SnapshotInfo {
    unsigned long number{};
    std::string date;
    std::string description;
    bool read_only{};
};

class SnapperClient {
  public:
    explicit SnapperClient(const ProcessRunner &runner, std::string config = "root")
        : runner_(runner), config_(std::move(config)) {}
    [[nodiscard]] SnapperConfig get_config() const;
    [[nodiscard]] std::vector<SnapshotInfo> list() const;

  private:
    const ProcessRunner &runner_;
    std::string config_;
};

} // namespace limine_manager::infrastructure
