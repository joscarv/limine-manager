#include "limine_manager/infrastructure/secure_boot_tools.hpp"

#include <stdexcept>
#include <string>

namespace limine_manager::infrastructure {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

void require_success(const ProcessResult &result, const std::string &operation) {
    if (result.exit_code != 0)
        throw std::runtime_error(operation + ": " + trim(result.output));
}

} // namespace

Blake2bDigest Blake2bHasher::digest(const std::filesystem::path &path) const {
    const auto result = runner_.run({"b2sum", path.string()});
    require_success(result, "b2sum failed for " + path.string());

    const auto separator = result.output.find_first_of(" \t");
    const auto value = result.output.substr(0, separator);
    if (value.size() != 128)
        throw std::runtime_error("invalid BLAKE2b digest for " + path.string());

    return {value};
}

SecureBootUpdateResult
SecureBootTools::update_limine_image(const std::filesystem::path &efi,
                                     const Blake2bDigest &digest) const {
    SecureBootUpdateResult result;

    require_success(runner_.run({"sbattach", "--remove", efi.string()}),
                    "sbattach signature removal failed");
    result.signature_removed = true;

    require_success(runner_.run({"limine", "enroll-config", efi.string(), digest.value}),
                    "limine enroll-config failed");
    result.configuration_enrolled = true;

    require_success(runner_.run({"sbctl", "sign", "-s", efi.string()}), "sbctl sign failed");
    result.image_signed = true;

    require_success(runner_.run({"sbctl", "verify", efi.string()}), "sbctl verify failed");
    result.signature_verified = true;

    return result;
}

} // namespace limine_manager::infrastructure
