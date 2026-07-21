#include "limine_manager/infrastructure/secure_file_ops.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, const std::string &content) {
    std::ofstream output(path, std::ios::trunc);
    output << content;
}

void atomic_restore_preserves_content_and_mode_test() {
    using namespace limine_manager::infrastructure;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-secure-file-ops-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto backup = root / "limine.conf.bak";
    const auto target = root / "limine.conf";
    write_text(backup, "original\n");
    write_text(target, "changed\n");
    ::chmod(backup.c_str(), 0640);

    atomic_restore_file(backup, target, "configuration backup",
                        "configuration rollback target");

    struct stat metadata {};
    assert(::stat(target.c_str(), &metadata) == 0);
    assert(read_text(target) == "original\n");
    assert((metadata.st_mode & 0777) == 0640);
    std::filesystem::remove_all(root);
}

void symlink_backup_is_rejected_test() {
    using namespace limine_manager::infrastructure;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-secure-file-symlink-" + std::to_string(::getpid()));
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
        rejected = std::string(error.what()).find("unsafe configuration backup") !=
                   std::string::npos;
    }

    assert(rejected);
    assert(read_text(target) == "safe\n");
    std::filesystem::remove_all(root);
}

void secure_remove_rejects_symlink_test() {
    using namespace limine_manager::infrastructure;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-secure-remove-" + std::to_string(::getpid()));
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

} // namespace

int main() {
    atomic_restore_preserves_content_and_mode_test();
    symlink_backup_is_rejected_test();
    secure_remove_rejects_symlink_test();
    return 0;
}
