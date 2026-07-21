#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/application/automation.hpp"
#include "limine_manager/application/backup_service.hpp"
#include "limine_manager/application/change_planner.hpp"
#include "limine_manager/application/preview_service.hpp"
#include "limine_manager/application/rollback_planner.hpp"
#include "limine_manager/application/rollback_service.hpp"
#include "limine_manager/application/status_service.hpp"
#include "limine_manager/application/validation_service.hpp"
#include "limine_manager/config/config_loader.hpp"
#include "limine_manager/domain/kernel_cmdline.hpp"
#include "limine_manager/domain/menu.hpp"
#include "limine_manager/domain/rollback.hpp"
#include "limine_manager/domain/validation.hpp"
#include "limine_manager/infrastructure/btrfs_client.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"
#include "limine_manager/infrastructure/kernel_discovery.hpp"
#include "limine_manager/render/limine_renderer.hpp"
#include "limine_manager/render/unified_diff_renderer.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {

class FakeFileSystem final : public limine_manager::infrastructure::FileSystem {
  public:
    void add_directory(std::filesystem::path path) {
        directories_.insert(normalize(path));
    }
    void add_file(std::filesystem::path path, std::string content = {}, bool readable = true) {
        const auto normalized = normalize(path);
        files_[normalized] = std::move(content);
        readability_[normalized] = readable;
        for (auto parent = normalized.parent_path(); !parent.empty();) {
            directories_.insert(parent);
            const auto next = parent.parent_path();
            if (next == parent)
                break;
            parent = next;
        }
    }

    bool exists(const std::filesystem::path &path) const override {
        const auto p = normalize(path);
        return files_.contains(p) || directories_.contains(p);
    }
    bool is_regular_file(const std::filesystem::path &path) const override {
        return files_.contains(normalize(path));
    }
    bool is_directory(const std::filesystem::path &path) const override {
        return directories_.contains(normalize(path));
    }
    bool readable(const std::filesystem::path &path) const override {
        const auto p = normalize(path);
        const auto it = readability_.find(p);
        return files_.contains(p) && it != readability_.end() && it->second;
    }
    std::string read_text(const std::filesystem::path &path) const override {
        const auto it = files_.find(normalize(path));
        return it == files_.end() ? std::string{} : it->second;
    }
    std::vector<limine_manager::infrastructure::DirectoryEntry>
    list_directory(const std::filesystem::path &path) const override {
        const auto parent = normalize(path);
        std::vector<limine_manager::infrastructure::DirectoryEntry> result;
        for (const auto &directory : directories_) {
            if (directory != parent && directory.parent_path() == parent)
                result.push_back({directory, false, true});
        }
        for (const auto &[file, content] : files_) {
            (void)content;
            if (file.parent_path() == parent)
                result.push_back({file, true, false});
        }
        return result;
    }
    std::filesystem::path canonical(const std::filesystem::path &path) const override {
        return normalize(path);
    }

  private:
    static std::filesystem::path normalize(const std::filesystem::path &path) {
        return path.lexically_normal();
    }
    std::map<std::filesystem::path, std::string> files_;
    std::map<std::filesystem::path, bool> readability_;
    std::set<std::filesystem::path> directories_;
};

class FakeProcessRunner final : public limine_manager::infrastructure::ProcessRunner {
  public:
    void respond(std::vector<std::string> arguments, std::string output, int exit_code = 0) {
        responses_[key(arguments)] = {exit_code, std::move(output)};
    }
    limine_manager::infrastructure::ProcessResult
    run(const std::vector<std::string> &arguments) const override {
        const auto it = responses_.find(key(arguments));
        if (it == responses_.end())
            return {127, "unexpected command"};
        return it->second;
    }

  private:
    static std::string key(const std::vector<std::string> &values) {
        std::string result;
        for (const auto &value : values) {
            result += value;
            result.push_back('\0');
        }
        return result;
    }
    std::map<std::string, limine_manager::infrastructure::ProcessResult> responses_;
};

class FakeBtrfsClient final : public limine_manager::infrastructure::BtrfsClient {
  public:
    std::vector<limine_manager::infrastructure::BtrfsSubvolume>
    list_subvolumes(const std::filesystem::path &) const override {
        if (fail_list)
            return {};
        return subvolumes;
    }
    void mount_top_level(const std::string &,
                         const std::filesystem::path &mount_point) const override {
        mounted = true;
        mounted_at = mount_point;
    }
    void unmount(const std::filesystem::path &) const override {
        mounted = false;
    }
    void create_writable_snapshot(const std::filesystem::path &source,
                                  const std::filesystem::path &destination) const override {
        if (fail_create)
            throw std::runtime_error("create failed");
        if (!contains(rel(source)) || contains(rel(destination)))
            throw std::runtime_error("invalid snapshot operation");
        subvolumes.push_back({next_id++, rel(destination)});
    }
    void move_subvolume(const std::filesystem::path &source,
                        const std::filesystem::path &destination) const override {
        ++move_count;
        if (fail_second_move && move_count == 2)
            throw std::runtime_error("move failed");
        const auto src = rel(source);
        const auto dst = rel(destination);
        if (!contains(src) || contains(dst))
            throw std::runtime_error("invalid move operation");
        for (auto &subvolume : subvolumes) {
            if (subvolume.path == src) {
                subvolume.path = dst;
                return;
            }
        }
    }
    void sync_filesystem(const std::filesystem::path &) const override {}

    [[nodiscard]] bool contains(const std::string &path) const {
        return std::any_of(subvolumes.begin(), subvolumes.end(),
                           [&](const auto &item) { return item.path == path; });
    }

    [[nodiscard]] std::string rel(const std::filesystem::path &path) const {
        const auto relative = path.lexically_relative(mounted_at);
        if (relative.empty())
            return path.string();
        return relative.string();
    }

    mutable std::vector<limine_manager::infrastructure::BtrfsSubvolume> subvolumes{
        {256, "@"}, {257, "@snapshots/123/snapshot"}};
    mutable bool mounted{false};
    mutable std::filesystem::path mounted_at{"/"};
    mutable unsigned long next_id{300};
    mutable int move_count{0};
    bool fail_create{false};
    bool fail_second_move{false};
    bool fail_list{false};
};

limine_manager::infrastructure::KernelInstallation kernel(std::string pkgbase, std::string release,
                                                          std::string title, bool running = false) {
    return {std::move(pkgbase),
            std::move(release),
            std::move(title),
            "/boot/vmlinuz-linux",
            {"/boot/intel-ucode.img", "/boot/initramfs-linux.img"},
            false,
            running};
}

void menu_tree_test() {
    using namespace limine_manager;
    infrastructure::SystemInfo system;
    system.running_kernel_release = "6.18.13-arch1-1";
    system.kernels = {kernel("linux", "6.18.13-arch1-1", "Linux", true)};
    system.kernel_cmdline = domain::KernelCommandLine::parse(
        "cryptdevice=UUID=demo:cryptroot root=/dev/mapper/cryptroot rootflags=subvol=@ rw");

    const std::vector<infrastructure::SnapshotInfo> snapshots{
        {42, "2026-07-01 12:01:33 +0000", "Before upgrade", true}};
    application::PreviewService service;
    render::LimineRenderer renderer;
    const auto output = renderer.render(service.build(system, snapshots, config::AppConfig{}));

    assert(output.find("/+Arch Linux") != std::string::npos);
    assert(output.find("//Linux") != std::string::npos);
    assert(output.find("comment: Kernel 6.18.13-arch1-1") != std::string::npos);
    assert(output.find("///2026-07-01 12:01") != std::string::npos);
    assert(output.find("////Linux") != std::string::npos);
    assert(output.find("rootflags=subvol=@snapshots/42/snapshot") != std::string::npos);
    assert(output.find("module_path: boot():/intel-ucode.img") <
           output.find("module_path: boot():/initramfs-linux.img"));
}

void multiple_kernel_test() {
    using namespace limine_manager;
    infrastructure::SystemInfo system;
    system.kernels = {kernel("linux", "6.18.13-arch1-1", "Linux", true),
                      {"linux-lts",
                       "6.12.40-1-lts",
                       "Linux LTS",
                       "/boot/vmlinuz-linux-lts",
                       {"/boot/initramfs-linux-lts.img"},
                       false}};
    system.kernel_cmdline =
        domain::KernelCommandLine::parse("root=/dev/mapper/cryptroot rootflags=subvol=@ rw");
    application::PreviewService service;
    render::LimineRenderer renderer;
    const auto output = renderer.render(
        service.build(system, {{7, "2026-07-01", "test", true}}, config::AppConfig{}));
    assert(output.find("//Linux LTS") != std::string::npos);
    assert(output.find("////Linux LTS") != std::string::npos);
    assert(output.find("path: boot():/vmlinuz-linux-lts") != std::string::npos);
}

void cmdline_model_test() {
    using limine_manager::domain::KernelCommandLine;
    auto cmdline = KernelCommandLine::parse(
        "quiet root=/dev/mapper/cryptroot option=\"hello world\" rootflags=subvol=@");
    assert(cmdline.value("root") == "/dev/mapper/cryptroot");
    assert(cmdline.value("option") == "hello world");
    auto repeated =
        KernelCommandLine::parse("rd.luks.name=11111111-1111-1111-1111-111111111111=data "
                                 "rd.luks.name=22222222-2222-2222-2222-222222222222=cryptroot");
    const auto luks_names = repeated.values("rd.luks.name");
    assert(luks_names.size() == 2);
    assert(luks_names.back() == "22222222-2222-2222-2222-222222222222=cryptroot");
    cmdline.set("rootflags", "subvol=@snapshots/7/snapshot");
    assert(cmdline.value("rootflags") == "subvol=@snapshots/7/snapshot");
    assert(cmdline.render().find("\"option=hello world\"") != std::string::npos);
    cmdline.erase("quiet");
    assert(!cmdline.contains("quiet"));
}

void kernel_discovery_test() {
    using namespace limine_manager::infrastructure;
    const auto root = std::filesystem::temp_directory_path() / "limine-manager-kernel-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "boot");
    std::filesystem::create_directories(root / "modules/6.18.13-arch1-1");
    std::ofstream(root / "modules/6.18.13-arch1-1/pkgbase") << "linux\n";
    std::ofstream(root / "boot/vmlinuz-linux") << "kernel";
    std::ofstream(root / "boot/initramfs-linux.img") << "initrd";
    std::ofstream(root / "boot/intel-ucode.img") << "ucode";
    std::ofstream(root / "cpuinfo") << "vendor_id\t: GenuineIntel\n";

    RealFileSystem filesystem;
    KernelDiscovery discovery(filesystem, {root / "boot", root / "modules", root / "cpuinfo"});
    const auto kernels = discovery.discover("6.18.13-arch1-1");
    assert(kernels.size() == 1);
    assert(kernels.front().package_base == "linux");
    assert(kernels.front().release == "6.18.13-arch1-1");
    assert(kernels.front().running);
    assert(kernels.front().initrds.size() == 2);
    std::filesystem::remove_all(root);
}

void uki_discovery_and_render_test() {
    using namespace limine_manager;
    FakeFileSystem filesystem;
    filesystem.add_file("/boot/vmlinuz-linux", "kernel");
    filesystem.add_file("/boot/EFI/Linux/arch-linux.efi", "uki");
    filesystem.add_file("/usr/lib/modules/7.1.3-arch1-2/pkgbase", "linux\n");
    filesystem.add_file("/proc/cpuinfo", "vendor_id : GenuineIntel\n");

    infrastructure::KernelDiscovery discovery(filesystem);
    const auto kernels = discovery.discover("7.1.3-arch1-2");
    assert(kernels.size() == 1);
    assert(kernels.front().unified_kernel_image);
    assert(kernels.front().image == "/boot/EFI/Linux/arch-linux.efi");
    assert(kernels.front().initrds.empty());

    infrastructure::SystemInfo system;
    system.boot_mount = "/boot";
    system.kernels = kernels;
    system.kernel_cmdline =
        domain::KernelCommandLine::parse("root=/dev/mapper/cryptroot rootflags=subvol=@ rw");
    application::PreviewService preview;
    render::LimineRenderer renderer;
    const auto output = renderer.render(preview.build(system, {}, config::AppConfig{}));
    assert(output.find("protocol: efi") != std::string::npos);
    assert(output.find("path: boot():/EFI/Linux/arch-linux.efi") != std::string::npos);
    assert(output.find("module_path:") == std::string::npos);
}

void nested_boot_files_test() {
    using namespace limine_manager::infrastructure;
    FakeFileSystem filesystem;
    filesystem.add_file("/boot/kernels/vmlinuz-linux", "kernel");
    filesystem.add_file("/boot/images/initramfs-linux.img", "initrd");
    filesystem.add_file("/boot/firmware/intel-ucode.img", "ucode");
    filesystem.add_file("/usr/lib/modules/7.1.3-arch1-2/pkgbase", "linux\n");
    filesystem.add_file("/proc/cpuinfo", "vendor_id : GenuineIntel\n");
    KernelDiscovery discovery(filesystem);
    const auto kernels = discovery.discover("7.1.3-arch1-2");
    assert(kernels.size() == 1);
    assert(!kernels.front().unified_kernel_image);
    assert(kernels.front().image == "/boot/kernels/vmlinuz-linux");
    assert(kernels.front().initrds.size() == 2);
}

void config_loader_test() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() / "limine-manager-config-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / "manager.conf";
    std::ofstream(path) << "[system]\nboot_mount=/efi\nlimine_config=/efi/limine.conf\n"
                           "[snapper]\nconfig=root\nsnapshots_subvolume=@snapshots\nsnapshots_"
                           "directory=/.snapshots\nmax_snapshots=3\n"
                           "[menu]\nroot_title=My Linux\nroot_expanded=false\n"
                           "[kernels]\ninclude=linux, linux-lts\norder=linux-lts, linux\n"
                           "[theme]\nname=tokyo-night\n"
                           "[limine]\ntimeout=10\n";
    infrastructure::RealFileSystem filesystem;
    config::ConfigLoader loader(filesystem, root / "missing-default.conf");
    const auto loaded = loader.load(path);
    assert(loaded.source == path);
    assert(loaded.value.schema_version == 1);
    assert(loaded.value.system.boot_mount == "/efi");
    assert(loaded.value.max_snapshots == 3);
    assert(loaded.value.root_menu_title == "My Linux");
    assert(!loaded.value.root_menu_expanded);
    assert(loaded.value.include_kernels.size() == 2);
    assert(loaded.value.theme_name == "tokyo-night");
    assert(loaded.value.automation_enabled);
    assert(loaded.value.automation_snapper);
    assert(loaded.value.automation_pacman);
    assert(loaded.value.automation_debounce_seconds == 3);
    assert(loaded.value.limine_options.at("timeout") == "10");
    assert(loader.render(loaded).find("[theme]\nname = tokyo-night") != std::string::npos);
    assert(loader.render(loaded).find("[automation]\nenabled = true") != std::string::npos);
    assert(loader.render(loaded).find("Source: " + path.string()) != std::string::npos);

    const auto invalid_theme = root / "invalid-theme.conf";
    std::ofstream(invalid_theme) << "[theme]\nname=unknown\n";
    bool rejected_theme = false;
    try {
        (void)loader.load(invalid_theme);
    } catch (const std::runtime_error &) {
        rejected_theme = true;
    }
    assert(rejected_theme);
    std::filesystem::remove_all(root);
}

void config_schema_test() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() / "limine-manager-schema-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    infrastructure::RealFileSystem filesystem;
    config::ConfigLoader loader(filesystem, root / "missing.conf");

    const auto compatible = root / "compatible.conf";
    std::ofstream(compatible) << "[manager]\nschema_version=1\n";
    assert(loader.load(compatible).value.schema_version == 1);

    const auto legacy = root / "legacy.conf";
    std::ofstream(legacy) << "[menu]\nroot_title=Legacy Arch\n";
    assert(loader.load(legacy).value.schema_version == 1);

    const auto future = root / "future.conf";
    std::ofstream(future) << "[manager]\nschema_version=2\n";
    bool rejected = false;
    try {
        (void)loader.load(future);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    assert(rejected);

    const auto invalid_automation = root / "invalid-automation.conf";
    std::ofstream(invalid_automation) << "[automation]\ndebounce_seconds=3601\n";
    bool rejected_automation = false;
    try {
        (void)loader.load(invalid_automation);
    } catch (const std::runtime_error &) {
        rejected_automation = true;
    }
    assert(rejected_automation);
    std::filesystem::remove_all(root);
}

void automation_event_test() {
    using namespace limine_manager;
    const auto create =
        application::parse_snapper_plugin_event({"create-snapshot-post", "/", "btrfs", "42"});
    assert(create.action == "create-snapshot-post");
    assert(create.extra_arguments.size() == 1);
    assert(application::is_relevant_snapper_event(create));

    const auto pre =
        application::parse_snapper_plugin_event({"create-snapshot-pre", "/", "btrfs", "42"});
    assert(!application::is_relevant_snapper_event(pre));

    const auto home =
        application::parse_snapper_plugin_event({"delete-snapshot-post", "/home", "btrfs", "7"});
    assert(!application::is_relevant_snapper_event(home));

    const auto ext4 =
        application::parse_snapper_plugin_event({"modify-snapshot-post", "/", "ext4", "7"});
    assert(!application::is_relevant_snapper_event(ext4));

    const auto rollback =
        application::parse_snapper_plugin_event({"rollback-post", "/", "btrfs", "10", "11"});
    assert(application::is_relevant_snapper_event(rollback));
}

void refresh_request_service_test() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-refresh-request-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    application::RefreshRequestService service(root);
    assert(!service.pending());

    const auto first = service.request("snapper");
    assert(first.requested);
    assert(!first.coalesced);
    assert(service.pending());

    const auto second = service.request("pacman");
    assert(second.requested);
    assert(second.coalesced);
    assert(service.pending());

    service.clear_pending();
    assert(!service.pending());
    std::filesystem::remove_all(root);
}

void configurable_preview_test() {
    using namespace limine_manager;
    infrastructure::SystemInfo system;
    system.kernels = {kernel("linux", "6.18", "Linux", true),
                      {"linux-lts",
                       "6.12",
                       "Linux LTS",
                       "/boot/vmlinuz-linux-lts",
                       {"/boot/initramfs-linux-lts.img"},
                       false}};
    system.kernel_cmdline =
        domain::KernelCommandLine::parse("root=/dev/mapper/cryptroot rootflags=subvol=@ rw");
    config::AppConfig cfg;
    cfg.root_menu_title = "My Arch";
    cfg.max_snapshots = 1;
    cfg.include_kernels = {"linux-lts"};
    cfg.snapshots_subvolume = "@snaps";
    cfg.theme_name = "catppuccin";
    application::PreviewService service;
    render::LimineRenderer renderer;
    const auto output = renderer.render(service.build(
        system, {{2, "2026-07-02", "new", true}, {1, "2026-07-01", "old", true}}, cfg));
    assert(output.find("/+My Arch") != std::string::npos);
    assert(output.find("//Linux LTS") != std::string::npos);
    assert(output.find("//Linux\n") == std::string::npos);
    assert(output.find("@snaps/2/snapshot") != std::string::npos);
    assert(output.find("@snaps/1/snapshot") == std::string::npos);
    assert(output.find("term_background: 001E1E2E") != std::string::npos);
    assert(output.find("interface_branding_colour: 89B4FA") != std::string::npos);
    assert(output.find("term_palette: 11111B;F38BA8;A6E3A1") != std::string::npos);
}

void simulated_system_integration_test() {
    using namespace limine_manager;
    FakeFileSystem filesystem;
    filesystem.add_file("/etc/os-release", "PRETTY_NAME=\"Arch Linux\"\n");
    filesystem.add_file("/etc/kernel/cmdline",
                        "rd.luks.name=49c8e2ce-ab7e-4314-bc1f-251374a105fe=cryptroot "
                        "root=/dev/mapper/cryptroot rootflags=subvol=@ rw\n");
    filesystem.add_file("/proc/cpuinfo", "vendor_id : GenuineIntel\n");
    filesystem.add_directory("/boot");
    filesystem.add_file("/boot/EFI/BOOT/limine.conf", "timeout: 5\n");
    filesystem.add_file("/boot/vmlinuz-linux", "kernel");
    filesystem.add_file("/boot/initramfs-linux.img", "initramfs");
    filesystem.add_file("/boot/intel-ucode.img", "microcode");
    filesystem.add_directory("/usr/lib/modules/6.18.13-arch1-1");
    filesystem.add_file("/usr/lib/modules/6.18.13-arch1-1/pkgbase", "linux\n");
    filesystem.add_directory("/.snapshots/42/snapshot");

    FakeProcessRunner runner;
    runner.respond({"uname", "-r"}, "6.18.13-arch1-1\n");
    const auto mount = [&](const std::string &target, const std::string &field,
                           const std::string &value) {
        runner.respond({"findmnt", "--noheadings", "--raw", "--target", target, "--output", field},
                       value + "\n");
    };
    mount("/boot", "TARGET", "/boot");
    mount("/boot", "SOURCE", "/dev/nvme0n1p1");
    mount("/boot", "FSTYPE", "vfat");
    mount("/", "SOURCE", "/dev/mapper/cryptroot[/@]");
    mount("/", "FSTYPE", "btrfs");
    mount("/", "OPTIONS", "rw,subvol=@");

    infrastructure::SystemDetector detector(runner, filesystem);
    const auto system = detector.detect();
    assert(system.os_name == "Arch Linux");
    assert(system.kernels.size() == 1);
    assert(system.kernels.front().running);
    assert(system.limine_config == "/boot/EFI/BOOT/limine.conf");

    application::ValidationService validator(filesystem);
    infrastructure::SnapperConfig snapper{"root", "/", "btrfs"};
    const std::vector<infrastructure::SnapshotInfo> snapshots{
        {42, "2026-07-01", "Before upgrade", true}};
    const auto report = validator.validate(system, snapper, snapshots, config::AppConfig{});
    assert(report.valid());

    application::PreviewService preview;
    render::LimineRenderer renderer;
    const auto output = renderer.render(preview.build(system, snapshots, config::AppConfig{}));
    assert(output.find("rootflags=subvol=@snapshots/42/snapshot") != std::string::npos);
}

void automatic_unencrypted_cmdline_test() {
    using namespace limine_manager;
    FakeFileSystem filesystem;
    filesystem.add_file("/etc/os-release", "PRETTY_NAME=\"Arch Linux\"\n");
    filesystem.add_file("/proc/cpuinfo", "vendor_id : GenuineIntel\n");
    filesystem.add_directory("/boot");
    filesystem.add_file("/boot/limine.conf", "timeout: 5\n");
    filesystem.add_file("/boot/vmlinuz-linux", "kernel");
    filesystem.add_file("/boot/initramfs-linux.img", "initramfs");
    filesystem.add_file("/boot/intel-ucode.img", "microcode");
    filesystem.add_directory("/usr/lib/modules/7.1.3-arch1-2");
    filesystem.add_file("/usr/lib/modules/7.1.3-arch1-2/pkgbase", "linux\n");
    filesystem.add_directory("/.snapshots/1/snapshot");

    FakeProcessRunner runner;
    runner.respond({"uname", "-r"}, "7.1.3-arch1-2\n");
    const auto mount = [&](const std::string &target, const std::string &field,
                           const std::string &value) {
        runner.respond({"findmnt", "--noheadings", "--raw", "--target", target, "--output", field},
                       value + "\n");
    };
    mount("/boot", "TARGET", "/boot");
    mount("/boot", "SOURCE", "/dev/nvme0n1p1");
    mount("/boot", "FSTYPE", "vfat");
    mount("/", "SOURCE", "/dev/nvme0n1p2[/@]");
    mount("/", "FSTYPE", "btrfs");
    mount("/", "OPTIONS", "rw,subvol=@");
    runner.respond({"lsblk", "--noheadings", "--raw", "--output", "TYPE", "/dev/nvme0n1p2"},
                   "part\n");
    runner.respond({"blkid", "--match-tag", "UUID", "--output", "value", "/dev/nvme0n1p2"},
                   "11111111-2222-3333-4444-555555555555\n");

    infrastructure::SystemDetector detector(runner, filesystem);
    const auto system = detector.detect();
    assert(system.kernel_cmdline_generated);
    assert(!system.root_encrypted);
    assert(system.kernel_cmdline.value("root") == "UUID=11111111-2222-3333-4444-555555555555");
    assert(system.kernel_cmdline.value("rootflags") == "subvol=@");
    assert(system.kernel_cmdline.contains("rw"));
    assert(!system.kernel_cmdline.contains("cryptdevice"));
    assert(!system.kernel_cmdline.contains("rd.luks.name"));

    application::ValidationService validator(filesystem);
    const auto report =
        validator.validate(system, infrastructure::SnapperConfig{"root", "/", "btrfs"},
                           {{1, "2026-07-15", "test", true}}, config::AppConfig{});
    assert(report.valid());
}

void automatic_encrypted_cmdline_test() {
    using namespace limine_manager;
    const auto run_case = [](bool sd_encrypt) {
        FakeFileSystem filesystem;
        filesystem.add_file("/etc/os-release", "PRETTY_NAME=\"Arch Linux\"\n");
        filesystem.add_file("/proc/cpuinfo", "vendor_id : GenuineIntel\n");
        if (sd_encrypt)
            filesystem.add_file("/etc/mkinitcpio.conf",
                                "HOOKS=(base systemd autodetect sd-encrypt filesystems)\n");
        else
            filesystem.add_file("/etc/mkinitcpio.conf",
                                "HOOKS=(base udev autodetect encrypt filesystems)\n");
        filesystem.add_directory("/boot");
        filesystem.add_file("/boot/limine.conf", "timeout: 5\n");
        filesystem.add_file("/boot/vmlinuz-linux", "kernel");
        filesystem.add_file("/boot/initramfs-linux.img", "initramfs");
        filesystem.add_file("/boot/intel-ucode.img", "microcode");
        filesystem.add_directory("/usr/lib/modules/7.1.3-arch1-2");
        filesystem.add_file("/usr/lib/modules/7.1.3-arch1-2/pkgbase", "linux\n");

        FakeProcessRunner runner;
        runner.respond({"uname", "-r"}, "7.1.3-arch1-2\n");
        const auto mount = [&](const std::string &target, const std::string &field,
                               const std::string &value) {
            runner.respond(
                {"findmnt", "--noheadings", "--raw", "--target", target, "--output", field},
                value + "\n");
        };
        mount("/boot", "TARGET", "/boot");
        mount("/boot", "SOURCE", "/dev/nvme0n1p1");
        mount("/boot", "FSTYPE", "vfat");
        mount("/", "SOURCE", "/dev/mapper/cryptroot[/@]");
        mount("/", "FSTYPE", "btrfs");
        mount("/", "OPTIONS", "rw,subvol=@");
        runner.respond(
            {"lsblk", "--noheadings", "--raw", "--output", "TYPE", "/dev/mapper/cryptroot"},
            "crypt\n");
        runner.respond(
            {"cryptsetup", "status", "cryptroot"},
            "/dev/mapper/cryptroot is active.\n  type: LUKS2\n  device: /dev/nvme0n1p2\n");
        runner.respond(
            {"blkid", "--match-tag", "UUID", "--output", "value", "/dev/mapper/cryptroot"},
            "bbbbbbbb-cccc-dddd-eeee-ffffffffffff\n");
        runner.respond({"blkid", "--match-tag", "UUID", "--output", "value", "/dev/nvme0n1p2"},
                       "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n");
        runner.respond({"blkid", "--match-tag", "PARTUUID", "--output", "value", "/dev/nvme0n1p2"},
                       "11111111-2222-3333-4444-555555555555\n");

        infrastructure::SystemDetector detector(runner, filesystem);
        const auto system = detector.detect();
        assert(system.kernel_cmdline_generated);
        assert(system.root_encrypted);
        assert(system.encrypted_backing_device == "/dev/nvme0n1p2");
        assert(system.luks_uuid == "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        assert(system.encrypted_backing_partuuid == "11111111-2222-3333-4444-555555555555");
        assert(system.kernel_cmdline.value("root") == "/dev/mapper/cryptroot");
        if (sd_encrypt) {
            assert(system.kernel_cmdline.value("rd.luks.name") ==
                   "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee=cryptroot");
            assert(!system.kernel_cmdline.contains("cryptdevice"));
        } else {
            assert(system.kernel_cmdline.value("cryptdevice") ==
                   "UUID=aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee:cryptroot");
            assert(!system.kernel_cmdline.contains("rd.luks.name"));
        }
    };
    run_case(false);
    run_case(true);
}

void unprivileged_sysfs_encrypted_discovery_test() {
    using namespace limine_manager;
    FakeFileSystem filesystem;
    filesystem.add_file("/etc/os-release", "PRETTY_NAME=\"Arch Linux\"\n");
    filesystem.add_file("/etc/kernel/cmdline",
                        "cryptdevice=UUID=aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee:cryptroot "
                        "root=/dev/mapper/cryptroot rootflags=subvol=@ rw\n");
    filesystem.add_file("/proc/cpuinfo", "vendor_id : GenuineIntel\n");
    filesystem.add_file("/etc/mkinitcpio.conf", "HOOKS=(base udev encrypt filesystems)\n");
    filesystem.add_directory("/boot");
    filesystem.add_file("/boot/limine.conf", "timeout: 5\n");
    filesystem.add_file("/boot/vmlinuz-linux", "kernel");
    filesystem.add_file("/boot/initramfs-linux.img", "initramfs");
    filesystem.add_directory("/usr/lib/modules/7.1.3-arch1-3");
    filesystem.add_file("/usr/lib/modules/7.1.3-arch1-3/pkgbase", "linux\n");
    filesystem.add_directory("/sys/class/block/cryptroot/slaves");
    filesystem.add_file("/sys/class/block/cryptroot/slaves/nvme0n1p2");

    FakeProcessRunner runner;
    runner.respond({"uname", "-r"}, "7.1.3-arch1-3\n");
    const auto mount = [&](const std::string &target, const std::string &field,
                           const std::string &value) {
        runner.respond({"findmnt", "--noheadings", "--raw", "--target", target, "--output", field},
                       value + "\n");
    };
    mount("/boot", "TARGET", "/boot");
    mount("/boot", "SOURCE", "/dev/nvme0n1p1");
    mount("/boot", "FSTYPE", "vfat");
    mount("/", "SOURCE", "/dev/mapper/cryptroot[/@]");
    mount("/", "FSTYPE", "btrfs");
    mount("/", "OPTIONS", "rw,subvol=@");
    runner.respond({"lsblk", "--noheadings", "--raw", "--output", "TYPE", "/dev/mapper/cryptroot"},
                   "crypt\n");
    runner.respond({"cryptsetup", "status", "cryptroot"}, "Permission denied\n", 4);
    runner.respond({"blkid", "--match-tag", "UUID", "--output", "value", "/dev/mapper/cryptroot"},
                   "116435db-6d6d-495a-814a-0f3253207821\n");
    runner.respond({"blkid", "--match-tag", "UUID", "--output", "value", "/dev/nvme0n1p2"},
                   "49c8e2ce-ab7e-4314-bc1f-251374a105fe\n");
    runner.respond({"blkid", "--match-tag", "PARTUUID", "--output", "value", "/dev/nvme0n1p2"},
                   "f4b73def-3326-4d8c-ab8d-8ead4a2b97d1\n");

    infrastructure::SystemDetector detector(runner, filesystem);
    const auto system = detector.detect();
    assert(system.encrypted_backing_device == "/dev/nvme0n1p2");
    assert(system.luks_uuid == "49c8e2ce-ab7e-4314-bc1f-251374a105fe");
    assert(system.encrypted_backing_partuuid == "f4b73def-3326-4d8c-ab8d-8ead4a2b97d1");
}

void traditional_encrypt_validation_test() {
    using namespace limine_manager;
    FakeFileSystem filesystem;
    filesystem.add_file("/boot/limine.conf", "timeout: 5\n");
    filesystem.add_file("/etc/kernel/cmdline", "cmdline\n");
    filesystem.add_file("/boot/vmlinuz-linux", "kernel");
    filesystem.add_file("/boot/initramfs-linux.img", "initramfs");
    filesystem.add_file("/boot/intel-ucode.img", "microcode");
    filesystem.add_directory("/.snapshots/1/snapshot");

    infrastructure::SystemInfo system;
    system.root_fstype = "btrfs";
    system.root_subvolume = "@";
    system.root_source = "/dev/mapper/cryptroot[/@]";
    system.root_encrypted = true;
    system.root_mapper_name = "cryptroot";
    system.boot_mount = "/boot";
    system.boot_target = "/boot";
    system.boot_source = "/dev/nvme0n1p1";
    system.boot_fstype = "vfat";
    system.limine_config = "/boot/limine.conf";
    system.kernel_cmdline_file = "/etc/kernel/cmdline";
    system.running_kernel_release = "7.1.3-arch1-2";
    system.kernel_cmdline = domain::KernelCommandLine::parse(
        "cryptdevice=UUID=49c8e2ce-ab7e-4314-bc1f-251374a105fe:cryptroot "
        "root=/dev/mapper/cryptroot rootflags=subvol=@ rw");
    system.kernels = {kernel("linux", "7.1.3-arch1-2", "Linux", true)};

    application::ValidationService validator(filesystem);
    const auto report =
        validator.validate(system, infrastructure::SnapperConfig{"root", "/", "btrfs"},
                           {{1, "2026-07-13", "test", true}}, config::AppConfig{});
    assert(report.valid());
}

void archinstall_luks_uuid_validation_test() {
    using namespace limine_manager;
    FakeFileSystem filesystem;
    filesystem.add_file("/boot/EFI/BOOT/limine.conf", "timeout: 5\n");
    filesystem.add_file("/etc/kernel/cmdline", "cmdline\n");
    filesystem.add_file("/boot/EFI/Linux/arch-linux.efi", "uki");
    filesystem.add_directory("/.snapshots/1/snapshot");

    infrastructure::SystemInfo system;
    system.root_fstype = "btrfs";
    system.root_subvolume = "@";
    system.root_source = "/dev/mapper/root[/@]";
    system.root_encrypted = true;
    system.root_mapper_name = "root";
    system.root_uuid = "bbbbbbbb-cccc-dddd-eeee-ffffffffffff";
    system.luks_uuid = "49c8e2ce-ab7e-4314-bc1f-251374a105fe";
    system.boot_mount = "/boot";
    system.boot_target = "/boot";
    system.boot_source = "/dev/nvme0n1p1";
    system.boot_fstype = "vfat";
    system.limine_config = "/boot/EFI/BOOT/limine.conf";
    system.kernel_cmdline_file = "/etc/kernel/cmdline";
    system.running_kernel_release = "7.1.3-arch1-2";
    system.kernel_cmdline = domain::KernelCommandLine::parse(
        "rd.luks.name=49c8e2ce-ab7e-4314-bc1f-251374a105fe=cryptroot "
        "root=UUID=bbbbbbbb-cccc-dddd-eeee-ffffffffffff rootflags=subvol=@ rw");
    infrastructure::KernelInstallation uki;
    uki.package_base = "linux";
    uki.release = "7.1.3-arch1-2";
    uki.display_name = "Linux";
    uki.image = "/boot/EFI/Linux/arch-linux.efi";
    uki.running = true;
    uki.unified_kernel_image = true;
    system.kernels = {uki};

    application::ValidationService validator(filesystem);
    const auto report =
        validator.validate(system, infrastructure::SnapperConfig{"root", "/", "btrfs"},
                           {{1, "2026-07-16", "archinstall", true}}, config::AppConfig{});
    assert(report.valid());
}

void archinstall_partuuid_validation_test() {
    using namespace limine_manager;
    FakeFileSystem filesystem;
    filesystem.add_file("/boot/EFI/BOOT/limine.conf", "timeout: 5\n");
    filesystem.add_file("/etc/kernel/cmdline", "cmdline\n");
    filesystem.add_file("/boot/EFI/Linux/arch-linux.efi", "uki");
    filesystem.add_directory("/.snapshots/1/snapshot");

    infrastructure::SystemInfo system;
    system.root_fstype = "btrfs";
    system.root_subvolume = "@";
    system.root_source = "/dev/mapper/root[/@]";
    system.root_encrypted = true;
    system.root_mapper_name = "root";
    system.root_uuid = "88e972db-24f7-47b2-a7dd-43076f272f5a";
    system.luks_uuid = "35b65068-4baf-4adc-93cf-322948cc9cb1";
    system.encrypted_backing_device = "/dev/vda2";
    system.encrypted_backing_partuuid = "da5d4d02-603b-48f8-a0e7-fe49a9c9a43a";
    system.boot_mount = "/boot";
    system.boot_target = "/boot";
    system.boot_source = "/dev/vda1";
    system.boot_fstype = "vfat";
    system.limine_config = "/boot/EFI/BOOT/limine.conf";
    system.kernel_cmdline_file = "/etc/kernel/cmdline";
    system.running_kernel_release = "7.1.3-arch1-3";
    system.kernel_cmdline = domain::KernelCommandLine::parse(
        "cryptdevice=PARTUUID=da5d4d02-603b-48f8-a0e7-fe49a9c9a43a:cryptroot "
        "root=/dev/mapper/root zswap.enabled=0 rootflags=subvol=@ rw rootfstype=btrfs quiet "
        "splash");
    infrastructure::KernelInstallation uki;
    uki.package_base = "linux";
    uki.release = "7.1.3-arch1-3";
    uki.display_name = "Linux";
    uki.image = "/boot/EFI/Linux/arch-linux.efi";
    uki.running = true;
    uki.unified_kernel_image = true;
    system.kernels = {uki};

    application::ValidationService validator(filesystem);
    const auto report =
        validator.validate(system, infrastructure::SnapperConfig{"root", "/", "btrfs"},
                           {{1, "2026-07-16", "archinstall", true}}, config::AppConfig{});
    assert(report.valid());
}

void change_planner_test() {
    using namespace limine_manager;
    FakeFileSystem filesystem;
    filesystem.add_file("/boot/limine.conf", "timeout: 5\n");
    application::ChangePlanner planner(filesystem);

    const auto unchanged = planner.build("/boot/limine.conf", "timeout: 5\n");
    assert(unchanged.kind == application::ChangeKind::unchanged);
    assert(!unchanged.has_changes());

    const auto update = planner.build("/boot/limine.conf", "timeout: 10\n");
    assert(update.kind == application::ChangeKind::update);
    render::UnifiedDiffRenderer renderer;
    const auto diff = renderer.render(update);
    assert(diff.find("-timeout: 5") != std::string::npos);
    assert(diff.find("+timeout: 10") != std::string::npos);

    const auto create = planner.build("/boot/new.conf", "timeout: 5");
    assert(create.kind == application::ChangeKind::create);
    assert(renderer.render(create).find("--- /dev/null") != std::string::npos);
}

void apply_service_test() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-apply-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto target = root / "limine.conf";
    {
        std::ofstream output(target);
        output << "timeout: 5\n";
    }
    ::chmod(target.c_str(), 0640);

    infrastructure::RealFileSystem filesystem;
    application::ChangePlanner planner(filesystem);
    application::ApplyService service;
    const auto plan = planner.build(target, "timeout: 10\n");
    const auto result = service.apply(plan);

    assert(result.changed);
    assert(!result.backup.empty());
    assert(std::filesystem::exists(result.backup));
    assert(filesystem.read_text(target) == "timeout: 10\n");
    assert(filesystem.read_text(result.backup) == "timeout: 5\n");

    struct stat metadata{};
    assert(::stat(target.c_str(), &metadata) == 0);
    assert((metadata.st_mode & 0777) == 0640);

    const auto new_target = root / "new.conf";
    const auto create_plan = planner.build(new_target, "timeout: 3\n");
    const auto created = service.apply(create_plan);
    assert(created.changed);
    assert(created.backup.empty());
    assert(filesystem.read_text(new_target) == "timeout: 3\n");

    const auto stale_plan = planner.build(target, "timeout: 20\n");
    {
        std::ofstream output(target);
        output << "externally changed\n";
    }
    bool stale_rejected = false;
    try {
        (void)service.apply(stale_plan);
    } catch (const std::runtime_error &) {
        stale_rejected = true;
    }
    assert(stale_rejected);
    assert(filesystem.read_text(target) == "externally changed\n");

    std::filesystem::remove_all(root);
}

void backup_service_test() {
    using namespace limine_manager::application;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-backup-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto target = root / "limine.conf";
    std::ofstream(root / "limine.conf.bak.20260713-000001.1.1") << "one\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::ofstream(root / "limine.conf.bak.20260713-000002.1.2") << "two\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::ofstream(root / "limine.conf.bak.20260713-000003.1.3") << "three\n";
    std::ofstream(root / "unrelated.bak") << "ignore\n";

    BackupService service;
    const auto backups = service.list(target);
    assert(backups.size() == 3);
    assert(service.latest(target).has_value());
    assert(service.prune(target, 2) == 1);
    assert(service.list(target).size() == 2);
    assert(service.prune(target, 0) == 0);
    std::filesystem::remove_all(root);
}

void deterministic_render_test() {
    using namespace limine_manager;
    infrastructure::SystemInfo system;
    system.kernels = {kernel("linux", "6.18", "Linux", true)};
    system.kernel_cmdline =
        domain::KernelCommandLine::parse("root=/dev/mapper/cryptroot rootflags=subvol=@ rw");
    application::PreviewService preview;
    render::LimineRenderer renderer;
    const auto first = renderer.render(preview.build(system, {}, config::AppConfig{}));
    const auto second = renderer.render(preview.build(system, {}, config::AppConfig{}));
    assert(first == second);
    assert(first.find("Generated by limine-manager") != std::string::npos);
}

void domain_invariant_test() {
    using namespace limine_manager::domain;
    auto entry = MenuNode::linux_entry("Linux", "test", {"linux", "kernel", {}, ""});
    bool threw = false;
    try {
        entry.add_child(MenuNode::directory("invalid"));
    } catch (const std::logic_error &) {
        threw = true;
    }
    assert(threw);
}

void fixture_config_test() {
    using namespace limine_manager;
    const auto fixture =
        std::filesystem::path(LIMINE_MANAGER_SOURCE_DIR) / "tests/fixtures/config/custom.conf";
    infrastructure::RealFileSystem filesystem;
    config::ConfigLoader loader(filesystem);
    const auto loaded = loader.load(fixture);
    assert(loaded.value.max_snapshots == 2);
    assert(loaded.value.backup_retention == 3);
    assert(loaded.value.include_kernels.size() == 2);
    assert(loaded.value.kernel_order.front() == "linux-lts");
}

void validation_report_test() {
    using namespace limine_manager::domain;
    ValidationReport report;
    report.info("i", "info");
    report.warning("w", "warning");
    assert(report.valid());
    assert(report.warning_count() == 1);
    report.error("e", "error");
    assert(!report.valid());
    assert(report.error_count() == 1);
}

limine_manager::infrastructure::SystemInfo rollback_system(std::string subvolume) {
    limine_manager::infrastructure::SystemInfo system;
    system.root_fstype = "btrfs";
    system.root_source = "/dev/mapper/cryptroot[/@]";
    system.root_subvolume = std::move(subvolume);
    system.kernel_cmdline = limine_manager::domain::KernelCommandLine::parse(
        "root=/dev/mapper/cryptroot rootflags=subvol=@ rw");
    return system;
}

limine_manager::domain::RollbackPlan
rollback_plan_for(const limine_manager::infrastructure::SystemInfo &system,
                  const FakeBtrfsClient &btrfs) {
    using namespace limine_manager;
    application::RollbackPlanner planner(btrfs, {"testtx"});
    return planner.build(system, {"root", "/", "btrfs"}, {{123, "2026-07-13", "before", true}},
                         config::AppConfig{});
}

void rollback_planner_test() {
    using namespace limine_manager;
    FakeBtrfsClient btrfs;

    const auto normal = rollback_plan_for(rollback_system("@"), btrfs);
    assert(!normal.eligible);
    assert(normal.boot_mode == domain::RollbackBootMode::normal_root);

    const auto eligible = rollback_plan_for(rollback_system("@snapshots/123/snapshot"), btrfs);
    assert(eligible.eligible);
    assert(eligible.boot_mode == domain::RollbackBootMode::managed_snapshot);
    assert(eligible.snapshot_number == 123);
    assert(eligible.target_subvolume == "@");
    assert(eligible.preserved_subvolume == "@.limine-manager.rollback.123.testtx");
    assert(eligible.replacement_subvolume == "@.limine-manager.new.123.testtx");
    assert(eligible.source_snapshot_read_only);

    const auto unknown = rollback_plan_for(rollback_system("@snapshots/999/snapshot"), btrfs);
    assert(!unknown.eligible);
    assert(unknown.snapshot_number == 999);

    FakeBtrfsClient missing_target;
    missing_target.subvolumes = {{257, "@snapshots/123/snapshot"}};
    const auto no_target =
        rollback_plan_for(rollback_system("@snapshots/123/snapshot"), missing_target);
    assert(!no_target.eligible);

    FakeBtrfsClient conflict;
    conflict.subvolumes = {{256, "@"},
                           {257, "@snapshots/123/snapshot"},
                           {300, "@.limine-manager.rollback.123.testtx"}};
    const auto conflicted = rollback_plan_for(rollback_system("@snapshots/123/snapshot"), conflict);
    assert(!conflicted.eligible);
}

void status_service_test() {
    using namespace limine_manager;
    model::SystemModel model;
    model.system.os_name = "Arch Linux";
    model.system.root_fstype = "btrfs";
    model.system.root_source = "/dev/mapper/root[/@]";
    model.system.root_subvolume = "@";
    model.system.root_encrypted = true;
    model.system.root_mapper_name = "root";
    model.system.encrypted_backing_device = "/dev/vda2";
    model.system.encrypted_backing_partuuid = "partuuid";
    model.system.luks_uuid = "luks-uuid";
    model.system.boot_mount = "/boot";
    model.system.boot_source = "/dev/vda1";
    model.system.boot_fstype = "vfat";
    model.system.limine_config = "/boot/EFI/BOOT/limine.conf";
    model.system.kernel_cmdline_file = "/etc/kernel/cmdline";
    model.system.kernels.push_back(
        {"linux", "7.1.3", "Linux", "/boot/EFI/Linux/arch-linux.efi", {}, true, true});
    model.snapper = {"root", "/", "btrfs"};
    model.snapshots.available = {{1, "2026-07-16", "test", true}, {2, "2026-07-15", "older", true}};
    model.snapshots.selected = {{1, "2026-07-16", "test", true}};
    model.snapshots.maximum = 1;

    domain::ValidationReport validation;
    validation.info("system", "valid");
    application::ChangePlan plan{
        application::ChangeKind::unchanged, model.system.limine_config, {}, {}};
    config::AppConfig config;
    config.theme_name = "catppuccin";
    application::StatusService service;
    const auto status = service.build(model, validation, plan, {}, config);
    assert(status.healthy);
    assert(!status.changes_pending);
    assert(status.text.find("System health: healthy") != std::string::npos);
    assert(status.text.find("arch-linux.efi") != std::string::npos);
    assert(status.text.find("Available snapshots: 2") != std::string::npos);
    assert(status.text.find("Menu snapshots: 1") != std::string::npos);
    assert(status.text.find("Maximum configured: 1") != std::string::npos);
    assert(status.text.find("Theme: catppuccin") != std::string::npos);
    assert(status.text.find("Mount: /boot") != std::string::npos);
    assert(status.text.find("Root device: /dev/mapper/root") != std::string::npos);
    assert(status.text.find("Configuration: synchronized") != std::string::npos);

    auto incomplete = model;
    incomplete.system.luks_uuid.clear();
    incomplete.system.encrypted_backing_device.clear();
    const auto degraded = service.build(incomplete, validation, plan, {}, config);
    assert(degraded.healthy);
    assert(degraded.degraded);
    assert(degraded.text.find("System health: degraded") != std::string::npos);
    assert(degraded.text.find("encryption.discovery") != std::string::npos);

    auto limited = model;
    limited.snapshots.available.resize(150, {1, "2026-07-16", "test", true});
    limited.snapshots.selected.resize(10, {1, "2026-07-16", "test", true});
    limited.snapshots.maximum = 10;
    const auto limited_status = service.build(limited, validation, plan, {}, config);
    assert(!limited_status.degraded);
    assert(limited_status.text.find("Available snapshots: 150") != std::string::npos);
    assert(limited_status.text.find("Menu snapshots: 10") != std::string::npos);
    assert(limited_status.text.find("Large snapshot menu") == std::string::npos);

    validation.error("kernel", "broken");
    const auto failed = service.build(model, validation, plan, {}, config);
    assert(!failed.healthy);
    assert(failed.text.find("Problems") != std::string::npos);
    assert(failed.text.find("broken") != std::string::npos);
}

void secure_boot_render_test() {
    using namespace limine_manager;
    infrastructure::SystemInfo system;
    system.boot_mount = "/boot";
    system.kernel_cmdline = domain::KernelCommandLine::parse("root=UUID=test rw");
    infrastructure::KernelInstallation kernel;
    kernel.package_base = "linux";
    kernel.display_name = "Linux";
    kernel.image = "/boot/vmlinuz-linux";
    kernel.initrds = {"/boot/intel-ucode.img", "/boot/initramfs-linux.img"};
    system.kernels = {kernel};
    system.secure_boot.enabled = true;
    system.secure_boot.resource_hashes[kernel.image] = std::string(128, 'a');
    system.secure_boot.resource_hashes[kernel.initrds[0]] = std::string(128, 'b');
    system.secure_boot.resource_hashes[kernel.initrds[1]] = std::string(128, 'c');

    application::PreviewService preview;
    render::LimineRenderer renderer;
    const auto output = renderer.render(preview.build(system, {}, config::AppConfig{}));
    assert(output.find("path: boot():/vmlinuz-linux#" + std::string(128, 'a')) !=
           std::string::npos);
    assert(output.find("module_path: boot():/intel-ucode.img#" + std::string(128, 'b')) !=
           std::string::npos);
    assert(output.find("module_path: boot():/initramfs-linux.img#" + std::string(128, 'c')) !=
           std::string::npos);
}

void rollback_service_test() {
    using namespace limine_manager;
    FakeBtrfsClient btrfs;
    const auto plan = rollback_plan_for(rollback_system("@snapshots/123/snapshot"), btrfs);
    assert(plan.eligible);

    const auto runtime = std::filesystem::temp_directory_path() /
                         ("limine-manager-rollback-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(runtime);
    application::RollbackService service(btrfs, {runtime});
    const auto result = service.execute(plan);
    assert(result.active_subvolume == "@");
    assert(result.preserved_subvolume == "@.limine-manager.rollback.123.testtx");
    assert(btrfs.contains("@"));
    assert(btrfs.contains("@.limine-manager.rollback.123.testtx"));
    assert(!btrfs.contains("@.limine-manager.new.123.testtx"));
    assert(!btrfs.mounted);
    std::filesystem::remove_all(runtime);

    FakeBtrfsClient failing;
    failing.fail_second_move = true;
    const auto failing_plan =
        rollback_plan_for(rollback_system("@snapshots/123/snapshot"), failing);
    application::RollbackService failing_service(failing, {runtime});
    bool rejected = false;
    try {
        (void)failing_service.execute(failing_plan);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    assert(rejected);
    assert(failing.contains("@"));
    assert(!failing.contains("@.limine-manager.rollback.123.testtx"));
    assert(failing.contains("@.limine-manager.failed-replacement"));
    assert(!failing.mounted);
    std::filesystem::remove_all(runtime);
}
} // namespace

int main() {
    secure_boot_render_test();
    menu_tree_test();
    multiple_kernel_test();
    cmdline_model_test();
    kernel_discovery_test();
    uki_discovery_and_render_test();
    nested_boot_files_test();
    config_loader_test();
    config_schema_test();
    automation_event_test();
    refresh_request_service_test();
    configurable_preview_test();
    simulated_system_integration_test();
    automatic_unencrypted_cmdline_test();
    automatic_encrypted_cmdline_test();
    unprivileged_sysfs_encrypted_discovery_test();
    traditional_encrypt_validation_test();
    archinstall_luks_uuid_validation_test();
    archinstall_partuuid_validation_test();
    change_planner_test();
    apply_service_test();
    backup_service_test();
    deterministic_render_test();
    domain_invariant_test();
    validation_report_test();
    status_service_test();
    fixture_config_test();
    rollback_planner_test();
    rollback_service_test();
    std::cout << "All tests passed\n";
}
