#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/application/change_planner.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-runtime-lock-test-" + std::to_string(::getpid()));
    const auto boot = root / "boot";
    const auto runtime = root / "run";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(boot);

    const auto target = boot / "limine.conf";
    std::ofstream(target) << "timeout: 5\n";

    infrastructure::RealFileSystem filesystem;
    application::ChangePlanner planner(filesystem);
    application::ApplyService service(runtime);
    const auto result = service.apply(planner.build(target, "timeout: 10\n"));

    assert(result.changed);
    assert(std::filesystem::exists(runtime / "apply.lock"));
    assert(!std::filesystem::exists(boot / "limine.conf.lock"));
    assert(filesystem.read_text(target) == "timeout: 10\n");

    std::filesystem::remove_all(root);
    return 0;
}
