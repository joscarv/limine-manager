#include "limine_manager/application/secure_boot_transaction.hpp"
#include "limine_manager/infrastructure/secure_file_ops.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
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

mode_t mode_of(const std::filesystem::path &path) {
    struct stat metadata {};
    const int status = ::stat(path.c_str(), &metadata);
    assert(status == 0);
    return metadata.st_mode & 07777;
}

std::filesystem::path test_root(const std::string &name) {
    return std::filesystem::temp_directory_path() /
           ("limine-manager-transaction-hardening-" + name + "-" + std::to_string(::getpid()));
}

void atomic_restore_preserves_metadata_test() {
    using namespace limine_manager;
    const auto root = test_root("metadata");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto config = root / "limine.conf";
    const auto backup = root / "limine.conf.bak";
    const auto efi = root / "BOOTX64.EFI";
    write_text(config, "new configuration\n");
    write_text(backup, "original configuration\n");
    write_text(efi, "original EFI\n");
    ::chmod(backup.c_str(), 0640);

    application::SecureBootTransaction transaction(efi);
    transaction.record_apply({true, config, backup});
    write_text(efi, "modified EFI\n");
    transaction.rollback();

    assert(read_text(config) == "original configuration\n");
    assert(mode_of(config) == 0640);
    assert(read_text(efi) == "original EFI\n");

    for (const auto &entry : std::filesystem::directory_iterator(root))
        assert(entry.path().string().find(".rollback.") == std::string::npos);

    std::filesystem::remove_all(root);
}

void symlink_backup_is_rejected_but_efi_is_restored_test() {
    using namespace limine_manager;
    const auto root = test_root("backup-symlink");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto config = root / "limine.conf";
    const auto real_backup = root / "real-backup";
    const auto backup = root / "limine.conf.bak";
    const auto efi = root / "BOOTX64.EFI";
    write_text(config, "new configuration\n");
    write_text(real_backup, "original configuration\n");
    write_text(efi, "original EFI\n");
    std::filesystem::create_symlink(real_backup.filename(), backup);

    application::SecureBootTransaction transaction(efi);
    transaction.record_apply({true, config, backup});
    write_text(efi, "modified EFI\n");

    bool rejected = false;
    try {
        transaction.rollback();
    } catch (const std::runtime_error &error) {
        rejected =
            std::string(error.what()).find("unsafe configuration backup") != std::string::npos;
    }

    assert(rejected);
    assert(read_text(config) == "new configuration\n");
    assert(read_text(efi) == "original EFI\n");
    std::filesystem::remove_all(root);
}

void symlink_created_target_is_not_removed_test() {
    using namespace limine_manager;
    const auto root = test_root("target-symlink");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto protected_file = root / "protected";
    const auto config = root / "limine.conf";
    const auto efi = root / "BOOTX64.EFI";
    write_text(protected_file, "do not remove\n");
    write_text(efi, "original EFI\n");
    std::filesystem::create_symlink(protected_file.filename(), config);

    application::SecureBootTransaction transaction(efi);
    transaction.record_apply({true, config, {}});
    write_text(efi, "modified EFI\n");

    bool rejected = false;
    try {
        transaction.rollback();
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()).find("unsafe newly created configuration") !=
                   std::string::npos;
    }

    assert(rejected);
    assert(std::filesystem::is_symlink(config));
    assert(read_text(protected_file) == "do not remove\n");
    assert(read_text(efi) == "original EFI\n");
    std::filesystem::remove_all(root);
}

void configuration_rollback_can_be_retried_without_repeating_efi_test() {
    using namespace limine_manager;
    using infrastructure::testing::SecureFileFailurePoint;

    const auto root = test_root("retry-config");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto config = root / "limine.conf";
    const auto backup = root / "limine.conf.bak";
    const auto efi = root / "BOOTX64.EFI";
    write_text(config, "new configuration\n");
    write_text(backup, "original configuration\n");
    write_text(efi, "original EFI\n");

    application::SecureBootTransaction transaction(efi);
    transaction.record_apply({true, config, backup});
    write_text(efi, "modified EFI\n");

    infrastructure::testing::inject_failure_once(SecureFileFailurePoint::before_rename);

    bool first_attempt_failed = false;
    try {
        transaction.rollback();
    } catch (const std::runtime_error &error) {
        first_attempt_failed =
            std::string(error.what()).find("injected secure file failure") != std::string::npos;
    }

    assert(first_attempt_failed);
    assert(read_text(config) == "new configuration\n");
    assert(read_text(efi) == "original EFI\n");

    write_text(efi, "must not be rolled back again\n");
    transaction.rollback();

    assert(read_text(config) == "original configuration\n");
    assert(read_text(efi) == "must not be rolled back again\n");
    transaction.rollback();

    std::filesystem::remove_all(root);
}

void efi_rollback_can_be_retried_test() {
    using namespace limine_manager;
    using infrastructure::testing::SecureFileFailurePoint;

    const auto root = test_root("retry-efi");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto efi = root / "BOOTX64.EFI";
    write_text(efi, "original EFI\n");

    application::SecureBootTransaction transaction(efi);
    write_text(efi, "modified EFI\n");
    infrastructure::testing::inject_failure_once(SecureFileFailurePoint::before_rename);

    bool first_attempt_failed = false;
    try {
        transaction.rollback();
    } catch (const std::runtime_error &error) {
        first_attempt_failed =
            std::string(error.what()).find("injected secure file failure") != std::string::npos;
    }

    assert(first_attempt_failed);
    assert(read_text(efi) == "modified EFI\n");

    transaction.rollback();
    assert(read_text(efi) == "original EFI\n");
    transaction.rollback();

    std::filesystem::remove_all(root);
}

void destructor_reports_configuration_rollback_failure_test() {
    using namespace limine_manager;

    const auto root = test_root("destructor-report");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto config = root / "limine.conf";
    const auto real_backup = root / "real-backup";
    const auto backup = root / "limine.conf.bak";
    const auto efi = root / "BOOTX64.EFI";
    write_text(config, "new configuration\n");
    write_text(real_backup, "original configuration\n");
    write_text(efi, "original EFI\n");
    std::filesystem::create_symlink(real_backup.filename(), backup);

    std::vector<std::string> reports;
    {
        application::SecureBootTransaction transaction(
            efi, [&reports](std::string_view message) { reports.emplace_back(message); });
        transaction.record_apply({true, config, backup});
        write_text(efi, "modified EFI\n");
    }

    assert(reports.size() == 1);
    assert(reports.front().find("Secure Boot rollback failed during destruction") !=
           std::string::npos);
    assert(reports.front().find("unsafe configuration backup") != std::string::npos);
    assert(read_text(config) == "new configuration\n");
    assert(read_text(efi) == "original EFI\n");

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    atomic_restore_preserves_metadata_test();
    symlink_backup_is_rejected_but_efi_is_restored_test();
    symlink_created_target_is_not_removed_test();
    configuration_rollback_can_be_retried_without_repeating_efi_test();
    efi_rollback_can_be_retried_test();
    destructor_reports_configuration_rollback_failure_test();
    return 0;
}
