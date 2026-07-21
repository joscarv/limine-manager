#include "limine_manager/application/discovery_service.hpp"

#include "limine_manager/application/snapshot_selector.hpp"
#include "limine_manager/infrastructure/snapper_client.hpp"
#include "limine_manager/infrastructure/system_detector.hpp"

namespace limine_manager::application {

model::SystemModel DiscoveryService::discover(const config::AppConfig &config) const {
    infrastructure::SystemDetector system_detector(runner_, filesystem_, config.system);
    infrastructure::SnapperClient snapper_client(runner_, config.snapper_config);
    auto available = snapper_client.list();
    auto selected = select_menu_snapshots(available, config);
    return {.system = system_detector.detect(),
            .snapper = snapper_client.get_config(),
            .snapshots = {.available = std::move(available),
                          .selected = std::move(selected),
                          .maximum = config.max_snapshots}};
}

} // namespace limine_manager::application
