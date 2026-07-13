#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/application/backup_service.hpp"
#include "limine_manager/application/change_planner.hpp"
#include "limine_manager/application/preview_service.hpp"
#include "limine_manager/application/validation_service.hpp"
#include "limine_manager/config/config_loader.hpp"
#include "limine_manager/domain/kernel_cmdline.hpp"
#include "limine_manager/domain/menu.hpp"
#include "limine_manager/domain/validation.hpp"
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

limine_manager::infrastructure::KernelInstallation kernel(std::string pkgbase, std::string release,
                                                          std::string title, bool running = false) {
    return {std::move(pkgbase),
            std::move(release),
            std::move(title),
            "/boot/vmlinuz-linux",
            {"/boot/intel-ucode.img", "/boot/initramfs-linux.img"},
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
    assert(loaded.value.limine_options.at("timeout") == "10");
    assert(loader.render(loaded).find("Source: " + path.string()) != std::string::npos);
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
    application::PreviewService service;
    render::LimineRenderer renderer;
    const auto output = renderer.render(service.build(
        system, {{2, "2026-07-02", "new", true}, {1, "2026-07-01", "old", true}}, cfg));
    assert(output.find("/+My Arch") != std::string::npos);
    assert(output.find("//Linux LTS") != std::string::npos);
    assert(output.find("//Linux\n") == std::string::npos);
    assert(output.find("@snaps/2/snapshot") != std::string::npos);
    assert(output.find("@snaps/1/snapshot") == std::string::npos);
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
    filesystem.add_file("/boot/limine.conf", "timeout: 5\n");
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
    auto entry = MenuNode::linux_entry("Linux", "test", {"kernel", {}, ""});
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
} // namespace

int main() {
    menu_tree_test();
    multiple_kernel_test();
    cmdline_model_test();
    kernel_discovery_test();
    config_loader_test();
    config_schema_test();
    configurable_preview_test();
    simulated_system_integration_test();
    traditional_encrypt_validation_test();
    change_planner_test();
    apply_service_test();
    backup_service_test();
    deterministic_render_test();
    domain_invariant_test();
    validation_report_test();
    fixture_config_test();
    std::cout << "All tests passed\n";
}
