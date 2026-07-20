#include "limine_manager/infrastructure/efi_image_transaction.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

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
           ("limine-manager-efi-transaction-" + name + "-" +
            std::to_string(::getpid()));
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

    std::filesystem::path backup;
    {
        limine_manager::infrastructure::EfiImageTransaction transaction(image);
        backup = transaction.backup();
        write_text(image, "broken");
        transaction.rollback();
        assert(!transaction.active());
        assert(!std::filesystem::exists(backup));
        assert(read_text(image) == "original");
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

} // namespace

int main() {
    commit_test();
    explicit_rollback_test();
    destructor_rollback_test();
    return 0;
}
