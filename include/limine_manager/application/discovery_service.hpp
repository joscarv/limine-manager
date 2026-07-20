#pragma once

#include "limine_manager/config/config.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"
#include "limine_manager/infrastructure/process.hpp"
#include "limine_manager/model/system_model.hpp"

namespace limine_manager::application {

class DiscoveryService {
  public:
    DiscoveryService(const infrastructure::ProcessRunner &runner,
                     const infrastructure::FileSystem &filesystem)
        : runner_(runner), filesystem_(filesystem) {}

    [[nodiscard]] model::SystemModel discover(const config::AppConfig &config) const;

  private:
    const infrastructure::ProcessRunner &runner_;
    const infrastructure::FileSystem &filesystem_;
};

} // namespace limine_manager::application
