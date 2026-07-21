#include "limine_manager/infrastructure/secure_file_ops.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using limine_manager::infrastructure::testing::SecureFileFailurePoint;

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, const std::string &content) {
    std::ofstream output(path, std::ios::trunc);
    output << content;
}

std::filesystem::path test_root(const std::string &name) {
    return std::filesystem::temp_directory_path() /
           ("limine-manager-secure-file-ops-" + name + "-" + std::to_string(::getpid()));
}

bool has_rollback_temporary(const std::filesystem::path &root,
                            const std::filesystem::path &target) {
    const auto prefix = target.filename().string() + ".rollback.";
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().filename().string().starts_with(prefix))
            return true;
    }
    return false;
}

void atomic_restore_preserves_content_and_mode_test() {
    using namespace limine_manager::infrastructure;
    const auto root = test_root("restore");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto backup = root / "limine.conf.bak";
    const auto target = root / "limine.conf";
    write_text(backup, "original\n");
    write_text(target, "changed\n");
    ::chmod(backup.c_str(), 0640);

    atomic_restore_file(backup, target, "configuration backup", "configuration rollback target");

    struct stat metadata{};
    assert(::stat(target.c_str(), &metadata) == 0);
    assert(read_text(target) == "original\n");
    assert((metadata.st_mode & 0777) == 0640);
    std::filesystem::remove_all(root);
}

void symlink_backup_is_rejected_test() {
    using namespace limine_manager::infrastructure;
    const auto root = test_root("symlink");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto source = root / "source";
    const auto backup = root / "backup";
    const auto target = root / "target";
    write_text(source, "attacker\n");
    write_text(target, "safe\n");
    std::filesystem::create_symlink(source, backup);

    bool rejected = false;
    try {
        atomic_restore_file(backup, target, "configuration backup",
                            "configuration rollback target");
    } catch (const std::runtime_error &error) {
        rejected =
            std::string(error.what()).find("unsafe configuration backup") != std::string::npos;
    }

    assert(rejected);
    assert(read_text(target) == "safe\n");
    std::filesystem::remove_all(root);
}

void secure_remove_rejects_symlink_test() {
    using namespace limine_manager::infrastructure;
    const auto root = test_root("remove-symlink");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto victim = root / "victim";
    const auto target = root / "target";
    write_text(victim, "keep\n");
    std::filesystem::create_symlink(victim, target);

    bool rejected = false;
    try {
        remove_regular_file_secure(target, "newly created configuration");
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()).find("unsafe newly created configuration") !=
                   std::string::npos;
    }

    assert(rejected);
    assert(std::filesystem::is_symlink(target));
    assert(read_text(victim) == "keep\n");
    std::filesystem::remove_all(root);
}

void pre_rename_failures_preserve_target_and_clean_temporary_test() {
    using namespace limine_manager::infrastructure;
    const SecureFileFailurePoint points[] = {
        SecureFileFailurePoint::after_temporary_create,
        SecureFileFailurePoint::before_file_fsync,
        SecureFileFailurePoint::before_rename,
    };

    for (const auto point : points) {
        const auto root = test_root("pre-rename-" + std::to_string(static_cast<int>(point)));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        const auto backup = root / "backup";
        const auto target = root / "target";
        write_text(backup, "original\n");
        write_text(target, "changed\n");

        testing::inject_failure_once(point);
        bool failed = false;
        try {
            atomic_restore_file(backup, target);
        } catch (const std::runtime_error &error) {
            failed = std::string(error.what()) == "injected secure file failure";
        }
        testing::clear_failure_injection();

        assert(failed);
        assert(read_text(target) == "changed\n");
        assert(read_text(backup) == "original\n");
        assert(!has_rollback_temporary(root, target));
        std::filesystem::remove_all(root);
    }
}

void post_rename_failure_reports_uncertain_durability_without_losing_restore_test() {
    using namespace limine_manager::infrastructure;
    const auto root = test_root("post-rename");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto backup = root / "backup";
    const auto target = root / "target";
    write_text(backup, "original\n");
    write_text(target, "changed\n");

    testing::inject_failure_once(SecureFileFailurePoint::after_rename);
    bool failed = false;
    try {
        atomic_restore_file(backup, target);
    } catch (const std::runtime_error &error) {
        failed = std::string(error.what()) == "injected secure file failure";
    }
    testing::clear_failure_injection();

    assert(failed);
    assert(read_text(target) == "original\n");
    assert(read_text(backup) == "original\n");
    assert(!has_rollback_temporary(root, target));
    std::filesystem::remove_all(root);
}

void directory_fsync_failure_keeps_restored_target_and_backup_test() {
    using namespace limine_manager::infrastructure;
    const auto root = test_root("directory-fsync");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto backup = root / "backup";
    const auto target = root / "target";
    write_text(backup, "original\n");
    write_text(target, "changed\n");

    testing::inject_failure_once(SecureFileFailurePoint::before_directory_fsync);
    bool failed = false;
    try {
        atomic_restore_file(backup, target);
    } catch (const std::runtime_error &error) {
        failed = std::string(error.what()) == "injected secure file failure";
    }
    testing::clear_failure_injection();

    assert(failed);
    assert(read_text(target) == "original\n");
    assert(read_text(backup) == "original\n");
    assert(!has_rollback_temporary(root, target));
    std::filesystem::remove_all(root);
}

void unlink_failure_preserves_file_for_retry_test() {
    using namespace limine_manager::infrastructure;
    const auto root = test_root("unlink");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto backup = root / "backup";
    write_text(backup, "original\n");

    testing::inject_failure_once(SecureFileFailurePoint::before_unlink);
    bool failed = false;
    try {
        remove_regular_file_secure(backup, "backup");
    } catch (const std::runtime_error &error) {
        failed = std::string(error.what()) == "injected secure file failure";
    }
    testing::clear_failure_injection();

    assert(failed);
    assert(read_text(backup) == "original\n");
    remove_regular_file_secure(backup, "backup");
    assert(!std::filesystem::exists(backup));
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    atomic_restore_preserves_content_and_mode_test();
    symlink_backup_is_rejected_test();
    secure_remove_rejects_symlink_test();
    pre_rename_failures_preserve_target_and_clean_temporary_test();
    post_rename_failure_reports_uncertain_durability_without_losing_restore_test();
    directory_fsync_failure_keeps_restored_target_and_backup_test();
    unlink_failure_preserves_file_for_retry_test();
    return 0;
}
