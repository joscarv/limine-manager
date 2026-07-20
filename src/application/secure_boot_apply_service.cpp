#include "limine_manager/application/secure_boot_apply_service.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace limine_manager::application {
namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string hash_file(const infrastructure::ProcessRunner &runner,
                      const std::filesystem::path &path) {
    const auto result = runner.run({"b2sum", path.string()});
    if (result.exit_code != 0)
        throw std::runtime_error("b2sum failed for " + path.string() + ": " + trim(result.output));
    const auto space = result.output.find_first_of(" \t");
    const auto hash = result.output.substr(0, space);
    if (hash.size() != 128)
        throw std::runtime_error("invalid BLAKE2b digest for " + path.string());
    return hash;
}
std::filesystem::path efi_backup_name(const std::filesystem::path &efi) {
    const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("limine-manager-" + efi.filename().string() + "." +
            std::to_string(::getpid()) + "." + std::to_string(ticks) + ".bak");
}
} // namespace

ApplyResult SecureBootApplyService::apply(const ChangePlan &plan,
                                           const infrastructure::SystemInfo &system,
                                           const config::AppConfig &config) const {
    if (!plan.has_changes())
        return {false, plan.target, {}};
    if (!system.secure_boot.enabled || !config.secure_boot_protect_config)
        return ApplyService{}.apply(plan);
    if (system.secure_boot.efi_executable.empty())
        throw std::runtime_error("Secure Boot is enabled but the active Limine EFI executable is unknown");

    const auto efi = system.secure_boot.efi_executable;
    const auto efi_backup = efi_backup_name(efi);
    std::filesystem::copy_file(efi, efi_backup, std::filesystem::copy_options::none);
    ApplyResult result;
    try {
        result = ApplyService{}.apply(plan);
        const auto digest = hash_file(runner_, plan.target);
        auto command = runner_.run({"sbattach", "--remove", efi.string()});
        if (command.exit_code != 0)
            throw std::runtime_error("sbattach signature removal failed: " + trim(command.output));
        command = runner_.run({"limine", "enroll-config", efi.string(), digest});
        if (command.exit_code != 0)
            throw std::runtime_error("limine enroll-config failed: " + trim(command.output));
        command = runner_.run({"sbctl", "sign", "-s", efi.string()});
        if (command.exit_code != 0)
            throw std::runtime_error("sbctl sign failed: " + trim(command.output));
        command = runner_.run({"sbctl", "verify", efi.string()});
        if (command.exit_code != 0)
            throw std::runtime_error("sbctl verify failed: " + trim(command.output));
        std::error_code cleanup_error;
        std::filesystem::remove(efi_backup, cleanup_error);
        return result;
    } catch (...) {
        std::error_code ec;
        std::filesystem::copy_file(efi_backup, efi, std::filesystem::copy_options::overwrite_existing, ec);
        if (!result.backup.empty())
            std::filesystem::copy_file(result.backup, plan.target,
                                       std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(efi_backup, ec);
        throw;
    }
}

} // namespace limine_manager::application
