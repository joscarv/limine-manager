#include "limine_manager/infrastructure/secure_boot_tools.hpp"

#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeProcessRunner final : public limine_manager::infrastructure::ProcessRunner {
  public:
    mutable std::vector<std::vector<std::string>> commands;
    mutable std::vector<limine_manager::infrastructure::ProcessResult> results;

    limine_manager::infrastructure::ProcessResult
    run(const std::vector<std::string> &arguments) const override {
        commands.push_back(arguments);
        if (results.empty())
            throw std::runtime_error("missing fake process result");
        const auto result = results.front();
        results.erase(results.begin());
        return result;
    }
};

void hashing_test() {
    using namespace limine_manager::infrastructure;
    FakeProcessRunner runner;
    runner.results.push_back({0, std::string(128, 'a') + "  /boot/limine.conf\n"});

    const Blake2bHasher hasher(runner);
    const auto digest = hasher.digest("/boot/limine.conf");

    assert(digest.value == std::string(128, 'a'));
    assert(runner.commands.size() == 1);
    assert((runner.commands[0] == std::vector<std::string>{"b2sum", "/boot/limine.conf"}));
}

void invalid_digest_test() {
    using namespace limine_manager::infrastructure;
    FakeProcessRunner runner;
    runner.results.push_back({0, "too-short  /boot/limine.conf\n"});

    bool rejected = false;
    try {
        (void)Blake2bHasher(runner).digest("/boot/limine.conf");
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()).find("invalid BLAKE2b digest") != std::string::npos;
    }
    assert(rejected);
}

void secure_boot_update_test() {
    using namespace limine_manager::infrastructure;
    FakeProcessRunner runner;
    runner.results = {{0, {}}, {0, {}}, {0, {}}, {0, {}}};

    const SecureBootTools tools(runner);
    const auto result =
        tools.update_limine_image("/boot/EFI/BOOT/BOOTX64.EFI", {std::string(128, 'b')});

    assert(result.signature_removed);
    assert(result.configuration_enrolled);
    assert(result.image_signed);
    assert(result.signature_verified);
    assert(runner.commands.size() == 4);
    assert((runner.commands[0] ==
            std::vector<std::string>{"sbattach", "--remove", "/boot/EFI/BOOT/BOOTX64.EFI"}));
    assert((runner.commands[1] == std::vector<std::string>{"limine", "enroll-config",
                                                           "/boot/EFI/BOOT/BOOTX64.EFI",
                                                           std::string(128, 'b')}));
    assert((runner.commands[2] ==
            std::vector<std::string>{"sbctl", "sign", "-s", "/boot/EFI/BOOT/BOOTX64.EFI"}));
    assert((runner.commands[3] ==
            std::vector<std::string>{"sbctl", "verify", "/boot/EFI/BOOT/BOOTX64.EFI"}));
}

void stop_on_failure_test() {
    using namespace limine_manager::infrastructure;
    FakeProcessRunner runner;
    runner.results = {{0, {}}, {1, "enrollment rejected\n"}};

    bool rejected = false;
    try {
        (void)SecureBootTools(runner).update_limine_image("/boot/limine.efi",
                                                          {std::string(128, 'c')});
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()) == "limine enroll-config failed: enrollment rejected";
    }

    assert(rejected);
    assert(runner.commands.size() == 2);
}

} // namespace

int main() {
    hashing_test();
    invalid_digest_test();
    secure_boot_update_test();
    stop_on_failure_test();
    return 0;
}
