#include "limine_manager/infrastructure/efi_image_transaction.hpp"
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
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, const std::string &value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
}

std::filesystem::path test_root(const std::string &name) {
    return std::filesystem::temp_directory_path() /
           ("limine-manager-efi-transaction-" + name + "-" + std::to_string(::getpid()));
}

mode_t file_mode(const std::filesystem::path &path) {
    struct stat metadata{};
    assert(::stat(path.c_str(), &metadata) == 0);
    return metadata.st_mode & 07777;
}

void commit_test() {
    const auto root = test_root("commit");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto image = root / "BOOTX64.EFI";
    write_text(image, "original");

    std::filesystem::path backup;
    {
        limine_manager::infrastructure::EfiImageTransaction transaction(image);
        backup = transaction.backup();
        assert(transaction.active());
        assert(std::filesystem::exists(backup));
        assert(read_text(backup) == "original");

        write_text(image, "updated");
        transaction.commit();
        assert(!transaction.active());
        assert(!std::filesystem::exists(backup));
    }

    assert(read_text(image) == "updated");
    std::filesystem::remove_all(root);
}

void explicit_rollback_test() {
    const auto root = test_root("rollback");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto image = root / "BOOTX64.EFI";
    write_text(image, "original");
    assert(::chmod(image.c_str(), 0640) == 0);

    std::filesystem::path backup;
    {
        limine_manager::infrastructure::EfiImageTransaction transaction(image);
        backup = transaction.backup();
        write_text(image, "broken");
        assert(::chmod(image.c_str(), 0600) == 0);
        transaction.rollback();
        assert(!transaction.active());
        assert(!std::filesystem::exists(backup));
        assert(read_text(image) == "original");
        assert(file_mode(image) == 0640);
    }

    std::filesystem::remove_all(root);
}

void destructor_rollback_test() {
    const auto root = test_root("destructor");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto image = root / "BOOTX64.EFI";
    write_text(image, "original");

    std::filesystem::path backup;
    {
        limine_manager::infrastructure::EfiImageTransaction transaction(image);
        backup = transaction.backup();
        write_text(image, "broken");
    }

    assert(read_text(image) == "original");
    assert(!std::filesystem::exists(backup));
    std::filesystem::remove_all(root);
}

void rejects_symlink_image_test() {
    const auto root = test_root("symlink-image");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto real_image = root / "real.EFI";
    const auto image = root / "BOOTX64.EFI";
    write_text(real_image, "original");
    std::filesystem::create_symlink(real_image.filename(), image);

    bool rejected = false;
    try {
        limine_manager::infrastructure::EfiImageTransaction transaction(image);
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()).find("unsafe EFI image") != std::string::npos;
    }

    assert(rejected);
    assert(read_text(real_image) == "original");
    std::filesystem::remove_all(root);
}

void rejects_symlink_rollback_target_test() {
    const auto root = test_root("symlink-rollback-target");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto image = root / "BOOTX64.EFI";
    const auto victim = root / "victim.EFI";
    write_text(image, "original");
    write_text(victim, "protected");

    std::filesystem::path backup;
    {
        limine_manager::infrastructure::EfiImageTransaction transaction(image);
        backup = transaction.backup();
        std::filesystem::remove(image);
        std::filesystem::create_symlink(victim.filename(), image);

        bool rejected = false;
        try {
            transaction.rollback();
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).find("unsafe EFI image rollback target") !=
                       std::string::npos;
        }

        assert(rejected);
        assert(transaction.active());
        assert(std::filesystem::exists(backup));
        assert(read_text(victim) == "protected");

        std::filesystem::remove(image);
        transaction.rollback();
        assert(!transaction.active());
    }

    assert(read_text(image) == "original");
    assert(!std::filesystem::exists(backup));
    std::filesystem::remove_all(root);
}

void destructor_reports_rollback_failure_test() {
    using limine_manager::infrastructure::testing::SecureFileFailurePoint;

    const auto root = test_root("destructor-report");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto image = root / "BOOTX64.EFI";
    write_text(image, "original");

    std::vector<std::string> reports;
    std::filesystem::path backup;
    {
        limine_manager::infrastructure::EfiImageTransaction transaction(
            image, [&reports](std::string_view message) { reports.emplace_back(message); });
        backup = transaction.backup();
        write_text(image, "broken");
        limine_manager::infrastructure::testing::inject_failure_once(
            SecureFileFailurePoint::before_rename);
    }

    assert(reports.size() == 1);
    assert(reports.front().find("EFI image rollback failed during destruction") !=
           std::string::npos);
    assert(reports.front().find("injected secure file failure") != std::string::npos);
    assert(read_text(image) == "broken");
    assert(std::filesystem::exists(backup));

    std::filesystem::remove_all(root);
}

void destructor_swallows_reporter_failure_test() {
    using limine_manager::infrastructure::testing::SecureFileFailurePoint;

    const auto root = test_root("throwing-reporter");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto image = root / "BOOTX64.EFI";
    write_text(image, "original");

    {
        limine_manager::infrastructure::EfiImageTransaction transaction(
            image, [](std::string_view) { throw std::runtime_error("reporter failure"); });
        write_text(image, "broken");
        limine_manager::infrastructure::testing::inject_failure_once(
            SecureFileFailurePoint::before_rename);
    }

    assert(read_text(image) == "broken");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    commit_test();
    explicit_rollback_test();
    destructor_rollback_test();
    rejects_symlink_image_test();
    rejects_symlink_rollback_target_test();
    destructor_reports_rollback_failure_test();
    destructor_swallows_reporter_failure_test();
    return 0;
}
