#include "limine_manager/application/secure_boot_apply_service.hpp"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, const std::string &content) {
    std::ofstream output(path, std::ios::trunc);
    output << content;
}

class MutatingProcessRunner final : public limine_manager::infrastructure::ProcessRunner {
  public:
    MutatingProcessRunner(std::filesystem::path efi_path, std::size_t failure)
        : efi(std::move(efi_path)), fail_command(failure) {}

    std::filesystem::path efi;
    std::size_t fail_command{0};
    mutable std::size_t secure_boot_command{0};

    limine_manager::infrastructure::ProcessResult
    run(const std::vector<std::string> &arguments) const override {
        if (arguments.front() == "b2sum")
            return {0, std::string(128, 'a') + "  " + arguments.at(1) + "\n"};

        write_text(efi, "modified EFI\n");
        ++secure_boot_command;
        if (secure_boot_command == fail_command)
            return {1, "injected failure\n"};
        return {0, {}};
    }
};

limine_manager::infrastructure::SystemInfo system_for(const std::filesystem::path &efi) {
    limine_manager::infrastructure::SystemInfo system;
    system.secure_boot.enabled = true;
    system.secure_boot.efi_executable = efi;
    return system;
}

limine_manager::config::AppConfig config_for(const std::filesystem::path &runtime) {
    limine_manager::config::AppConfig config;
    config.secure_boot_protect_config = true;
    config.automation_runtime_directory = runtime;
    return config;
}

void rollback_each_secure_boot_stage_test() {
    using namespace limine_manager;

    for (std::size_t failure = 1; failure <= 4; ++failure) {
        const auto root = std::filesystem::temp_directory_path() /
                          ("limine-manager-secure-apply-" + std::to_string(::getpid()) + "-" +
                           std::to_string(failure));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);

        const auto config_path = root / "limine.conf";
        const auto efi_path = root / "BOOTX64.EFI";
        write_text(config_path, "timeout: 5\n");
        write_text(efi_path, "original EFI\n");

        application::ChangePlan plan{application::ChangeKind::update, config_path,
                                     "timeout: 5\n", "timeout: 10\n"};
        MutatingProcessRunner runner(efi_path, failure);
        application::SecureBootApplyService service(runner);

        bool failed_with_original_error = false;
        try {
            (void)service.apply(plan, system_for(efi_path), config_for(root / "run"));
        } catch (const std::runtime_error &error) {
            failed_with_original_error =
                std::string(error.what()).find("injected failure") != std::string::npos;
        }

        assert(failed_with_original_error);
        assert(read_text(config_path) == "timeout: 5\n");
        assert(read_text(efi_path) == "original EFI\n");
        std::filesystem::remove_all(root);
    }
}

void rollback_created_configuration_test() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-secure-create-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto config_path = root / "limine.conf";
    const auto efi_path = root / "BOOTX64.EFI";
    write_text(efi_path, "original EFI\n");

    application::ChangePlan plan{application::ChangeKind::create, config_path, {}, "timeout: 10\n"};
    MutatingProcessRunner runner(efi_path, 2);
    application::SecureBootApplyService service(runner);

    bool failed = false;
    try {
        (void)service.apply(plan, system_for(efi_path), config_for(root / "run"));
    } catch (const std::runtime_error &) {
        failed = true;
    }

    assert(failed);
    assert(!std::filesystem::exists(config_path));
    assert(read_text(efi_path) == "original EFI\n");
    std::filesystem::remove_all(root);
}

void rollback_between_application_stages_test() {
    using namespace limine_manager;
    using application::testing::SecureBootApplyFailurePoint;

    constexpr std::array failure_points{
        SecureBootApplyFailurePoint::after_config_apply,
        SecureBootApplyFailurePoint::after_digest,
        SecureBootApplyFailurePoint::after_efi_update,
        SecureBootApplyFailurePoint::before_commit,
    };

    for (std::size_t index = 0; index < failure_points.size(); ++index) {
        const auto root = std::filesystem::temp_directory_path() /
                          ("limine-manager-secure-checkpoint-" + std::to_string(::getpid()) + "-" +
                           std::to_string(index));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);

        const auto config_path = root / "limine.conf";
        const auto efi_path = root / "BOOTX64.EFI";
        write_text(config_path, "timeout: 5\n");
        write_text(efi_path, "original EFI\n");

        application::ChangePlan plan{application::ChangeKind::update, config_path,
                                     "timeout: 5\n", "timeout: 10\n"};
        MutatingProcessRunner runner(efi_path, 0);
        application::SecureBootApplyService service(runner);
        application::testing::inject_failure_once(failure_points.at(index));

        bool injected_failure_seen = false;
        try {
            (void)service.apply(plan, system_for(efi_path), config_for(root / "run"));
        } catch (const std::runtime_error &error) {
            injected_failure_seen =
                std::string(error.what()).find("injected Secure Boot apply failure") !=
                std::string::npos;
        }
        application::testing::clear_failure_injection();

        assert(injected_failure_seen);
        assert(read_text(config_path) == "timeout: 5\n");
        assert(read_text(efi_path) == "original EFI\n");
        std::filesystem::remove_all(root);
    }
}

void failure_injection_is_consumed_once_test() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-secure-once-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto config_path = root / "limine.conf";
    const auto efi_path = root / "BOOTX64.EFI";
    write_text(config_path, "timeout: 5\n");
    write_text(efi_path, "original EFI\n");

    application::ChangePlan plan{application::ChangeKind::update, config_path,
                                 "timeout: 5\n", "timeout: 10\n"};
    MutatingProcessRunner runner(efi_path, 0);
    application::SecureBootApplyService service(runner);

    application::testing::inject_failure_once(
        application::testing::SecureBootApplyFailurePoint::after_config_apply);
    bool first_failed = false;
    try {
        (void)service.apply(plan, system_for(efi_path), config_for(root / "run"));
    } catch (const std::runtime_error &) {
        first_failed = true;
    }
    assert(first_failed);
    assert(read_text(config_path) == "timeout: 5\n");
    assert(read_text(efi_path) == "original EFI\n");

    const auto result = service.apply(plan, system_for(efi_path), config_for(root / "run"));
    assert(result.changed);
    assert(read_text(config_path) == "timeout: 10\n");
    assert(read_text(efi_path) == "modified EFI\n");

    application::testing::clear_failure_injection();
    std::filesystem::remove_all(root);
}

void successful_commit_test() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-secure-success-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto config_path = root / "limine.conf";
    const auto efi_path = root / "BOOTX64.EFI";
    write_text(config_path, "timeout: 5\n");
    write_text(efi_path, "original EFI\n");

    application::ChangePlan plan{application::ChangeKind::update, config_path,
                                 "timeout: 5\n", "timeout: 10\n"};
    MutatingProcessRunner runner(efi_path, 0);
    application::SecureBootApplyService service(runner);

    const auto result = service.apply(plan, system_for(efi_path), config_for(root / "run"));

    assert(result.changed);
    assert(read_text(config_path) == "timeout: 10\n");
    assert(read_text(efi_path) == "modified EFI\n");
    assert(std::filesystem::exists(result.backup));
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    rollback_each_secure_boot_stage_test();
    rollback_created_configuration_test();
    rollback_between_application_stages_test();
    failure_injection_is_consumed_once_test();
    successful_commit_test();
    return 0;
}
