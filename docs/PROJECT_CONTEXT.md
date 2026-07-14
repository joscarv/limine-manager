# PROJECT_CONTEXT.md

```yaml
project: limine-manager
context_version: 2
project_version: 1.1.0
last_updated: 2026-07-13
status: active
primary_platform: Arch Linux
language: C++20
build_system: CMake
current_focus: Implement and validate guarded Btrfs rollback from a booted Snapper root snapshot
source_of_truth:
  implementation: repository
  agent_rules: AGENTS.md
  current_state: docs/PROJECT_CONTEXT.md
  architectural_decisions: docs/DECISIONS.md
```

---

# 1. Project Summary

`limine-manager` is a C++20 utility for managing a structured Limine boot configuration on Arch Linux systems using Btrfs and Snapper.

The project originated from the need to generate a boot menu conceptually structured as:

```text
Arch Linux
├── Linux
└── Snapshots
    ├── 2026-07-01 12:01
    │   └── Linux
    ├── ...
```

The normal `Linux` entry boots the current Arch Linux installation.

The `Snapshots` submenu exposes bootable Btrfs snapshots discovered through Snapper.

The project must prioritize boot safety and must not blindly overwrite `/boot/limine.conf`.

---

# 2. Project History

The first design direction considered implementing the manager primarily as shell code.

During development, this approach was abandoned and the project was restarted from scratch with a hybrid architecture.

The accepted architecture is:

```text
C++:
- system detection
- kernel discovery
- kernel command-line parsing
- Snapper discovery
- domain modeling
- validation
- Limine rendering
- change planning
- diff generation
- safe apply
- backup handling

Shell:
- installation helpers
- package integration
- hooks where appropriate
```

The core application must remain in C++.

---

# 3. Current Release

The current reconstructed release state is:

```text
Version: 1.1.0
Tag: v1.1.0
```

The principal purpose of `v1.1.0` is to add a guarded Btrfs rollback flow after booting a Snapper root snapshot from Limine.

The new commands are:

```text
rollback-status
rollback-plan
rollback
```

`rollback-status` and `rollback-plan` are non-destructive inspection commands.

`rollback` is destructive and requires root. It refuses to run from the normal root subvolume, creates a writable replacement from the currently booted managed Snapper snapshot, preserves the previous main root with a unique recovery name, moves the replacement into `@`, verifies topology, regenerates `limine.conf`, and requires a reboot.

This operation is phased and recoverable, not globally atomic. Btrfs does not provide a single transaction covering every phase and Limine regeneration.

The previous `v1.0.1` purpose was to correct validation failures discovered after installing and executing the previous implementation on the real Arch Linux target system.

The reported real-system detection included:

```text
[INFO] root.filesystem: Root filesystem is Btrfs
[INFO] root.subvolume: Root subvolume is @
[INFO] boot.mount: Boot mount detected at /boot from /dev/nvme0n1p1 (vfat)
[INFO] limine.config: Limine configuration found: /boot/limine.conf
[INFO] kernel.cmdline_file: Kernel command line file found: /etc/kernel/cmdline
[INFO] kernel.cmdline: Kernel command line parsed into 7 argument(s)
[INFO] kernel.discovery: Discovered 1 kernel installation(s)
[INFO] kernel.linux.image: Linux image found: /boot/vmlinuz-linux
[INFO] kernel.linux.release: Linux release is 7.1.3-arch1-2
[INFO] kernel.linux.module.0: Linux module found: /boot/intel-ucode.img
[INFO] kernel.linux.module.1: Linux module found: ...
```

Most environment discovery was therefore working.

Two validation errors were found after installation.

The relevant fixes incorporated into the `v1.0.1` state were:

1. normalize Btrfs source paths reported by `findmnt`;
2. recognize additional valid encrypted-root kernel command-line syntax.

---

# 4. Reference Target System

The project has been tested against an Arch Linux installation with characteristics equivalent to:

```text
Distribution: Arch Linux
Bootloader: Limine
Root filesystem: Btrfs
Root subvolume: @
Root encryption: LUKS2
ESP mount: /boot
ESP filesystem: vfat
Limine config: /boot/limine.conf
Kernel cmdline file: /etc/kernel/cmdline
Kernel: linux
CPU microcode: Intel
Snapper: configured
```

The user's system has Snapper configurations for:

```text
/
/home
```

The initial snapshot boot-menu scope is centered on the root filesystem.

Do not automatically generate boot entries for `/home` snapshots.

---

# 5. Important Environment Detail

The system uses an encrypted Btrfs root.

Equivalent root-device information may appear differently depending on the source.

For example:

```text
/dev/mapper/cryptroot
```

and:

```text
/dev/mapper/cryptroot[/@]
```

may refer to the same underlying mounted Btrfs filesystem, with the latter including the selected subvolume.

This representation difference caused a validation failure and must be normalized before comparison.

---

# 6. Supported Encrypted-Root Command-Line Forms

The validator must support valid root-unlock configurations including:

```text
cryptdevice=UUID=<uuid>:cryptroot
```

and:

```text
rd.luks.name=<uuid>=cryptroot
```

The second form was specifically relevant to the real target system and was part of the `v1.0.1` correction.

The project must not assume that `cryptdevice=` is the only valid way to declare an encrypted root.

---

# 7. Known Architecture

The project currently contains or has contained components organized approximately as:

```text
include/limine_manager/
├── application/
│   ├── change_planner.hpp
│   ├── validation_service.hpp
│   └── ...
├── config/
│   ├── config.hpp
│   ├── config_loader.hpp
│   └── ...
├── domain/
│   ├── kernel_cmdline.hpp
│   └── ...
├── infrastructure/
│   ├── process.hpp
│   ├── system_detector.hpp
│   └── ...
└── render/
    ├── limine_renderer.hpp
    ├── unified_diff_renderer.hpp
    └── ...

src/
├── application/
│   ├── apply_service.cpp
│   ├── validation_service.cpp
│   └── ...
├── config/
├── domain/
│   ├── kernel_cmdline.cpp
│   └── ...
├── infrastructure/
│   ├── process.cpp
│   ├── system_detector.cpp
│   └── ...
├── render/
│   ├── limine_renderer.cpp
│   ├── unified_diff_renderer.cpp
│   └── ...
└── main.cpp

tests/
└── tests.cpp
```

This section is descriptive, not normative.

Always inspect the current repository tree before modifying files.

---

# 8. Known Functional Components

## System detection

The project detects information including:

```text
root filesystem
root source
root Btrfs subvolume
boot mount
boot source
boot filesystem
Limine configuration
kernel command-line file
running kernel
installed kernel images
kernel release
CPU microcode
```

Known component:

```text
SystemDetector
```

---

## Kernel discovery

The project discovers installed kernel information instead of hard-coding one complete boot entry.

The real system successfully discovered:

```text
/boot/vmlinuz-linux
```

and:

```text
/boot/intel-ucode.img
```

The design should remain compatible with future support for multiple installed kernels.

---

## Kernel command-line parsing

The project contains a dedicated kernel command-line model/parser.

It must handle arguments more safely than a naive whitespace split.

Known error cases include:

```text
unterminated quotes
trailing escape characters
```

The command line is boot-critical and should be modified minimally when generating snapshot entries.

---

## Configuration loading

The project supports application configuration through concepts equivalent to:

```text
Config
LoadedConfig
ConfigLoader
```

It can distinguish:

```text
explicit configuration file
built-in defaults
```

Configuration schema validation exists.

---

## Validation

The project performs validation before proposing or applying changes.

Diagnostics are designed to expose statuses similar to:

```text
[INFO]
[WARNING]
[ERROR]
```

Validation is expected to inspect the relationship between:

```text
detected root filesystem
detected root device
root subvolume
kernel command line
kernel files
Limine configuration
Snapper data
```

---

## Change planning

Changes are represented as a plan rather than being immediately written.

Known concept:

```text
ChangePlan
```

A plan can represent states equivalent to:

```text
create
modify
unchanged
```

This allows the application to preview what would happen before applying it.

---

## Unified diff rendering

The project contains a renderer for displaying differences between:

```text
current configuration
proposed configuration
```

Known component:

```text
UnifiedDiffRenderer
```

This is part of the safety-first preview workflow.

---

## Safe apply

The project contains an application service responsible for applying a previously generated change plan.

Known component:

```text
ApplyService
```

The implementation includes or has included logic for:

```text
safe file reading
safe file writing
handling EINTR
unique temporary/backup paths
stale-plan detection
backup creation
```

A plan must be rejected if the target changed after the plan was created.

---

## Backup handling

The CLI contains or has contained behavior for:

```text
listing backups
restoring backups
```

Backups are part of the recovery strategy for changes to the Limine configuration.

---

# 9. Intended CLI Capabilities

The executable includes or has included CLI behavior for:

```text
--version
configuration validation
system inspection / verbose diagnostics
previewing a planned change
applying a change
listing backups
restoring a backup
```

The exact current CLI flags must be verified against:

```bash
./build/limine-manager --help
```

Do not rely on this document for exact flag spelling when the executable can be inspected directly.

---

# 10. Safety Model

The intended workflow is:

```text
System detection
       │
       ▼
Validation
       │
       ▼
Domain/menu generation
       │
       ▼
Render candidate limine.conf
       │
       ▼
ChangePlan
       │
       ├── no changes ──► exit safely
       │
       ▼
Preview / unified diff
       │
       ▼
Explicit apply
       │
       ▼
Backup + safe replacement
```

Preview must not modify:

```text
/boot/limine.conf
```

---

# 11. Current Regression Fixes in v1.0.1

## Fix 1: Btrfs source normalization

Problem:

A filesystem source may be reported as:

```text
/dev/mapper/cryptroot[/@]
```

while another source identifies it as:

```text
/dev/mapper/cryptroot
```

Raw string comparison incorrectly reports a mismatch.

Required behavior:

```text
/dev/mapper/cryptroot[/@]
        │
        ▼
/dev/mapper/cryptroot
```

before identity comparison.

The normalization must not arbitrarily strip unrelated valid path content.

---

## Fix 2: `rd.luks.name` support

Problem:

Validation previously expected an encrypted-root declaration in a form such as:

```text
cryptdevice=UUID=<uuid>:cryptroot
```

The real system used a valid systemd-style declaration:

```text
rd.luks.name=<uuid>=cryptroot
```

The validator must recognize both supported forms.

Required behavior:

```text
cryptdevice=UUID=<uuid>:cryptroot
→ accepted when semantically consistent

rd.luks.name=<uuid>=cryptroot
→ accepted when semantically consistent
```

Malformed values must still be rejected.

---

# 12. Test Expectations

Regression tests should cover at least:

```text
Btrfs source normalization
cryptdevice parsing
rd.luks.name parsing
kernel command-line parsing
system validation
snapshot model generation
Limine rendering
change planning
unchanged plans
safe apply
stale-plan rejection
backup behavior
```

Known tests have exercised cases involving:

```text
SnapperConfig{"root", "/", "btrfs"}
snapshot metadata
validation reports
stale ChangePlan rejection
filesystem metadata
```

---

# 13. Build and Verification

Preferred clean verification:

```bash
rm -rf build

cmake -S . -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build

ctest \
    --test-dir build \
    --output-on-failure
```

Before release, also run the configured formatting and quality checks.

A previously captured CI/quality run reported `clang-format` violations across multiple project files.

Because that log may represent an earlier intermediate state, the current repository must be checked directly before concluding that the issue is still present.

The release must not be considered clean until the current commit passes the repository's required quality checks.

---

# 14. Installation

From a verified Release build:

```bash
sudo cmake --install build
```

Then verify:

```bash
limine-manager --version
```

Expected release version:

```text
limine-manager 1.0.1
```

Before applying boot configuration changes, run the non-destructive validation/preview workflow exposed by the current CLI.

Check the actual command names with:

```bash
limine-manager --help
```

---

# 15. Git and GitHub Release Flow

Typical initial repository publication:

```bash
git init
git add .
git commit -m "Initial release"
```

Create the remote repository using GitHub CLI:

```bash
gh repo create limine-manager
```

The exact visibility and source/push options should be selected according to the intended repository state.

For the `v1.0.1` release:

```bash
git add .
git commit -m "fix: improve root device and LUKS validation"

git tag -a v1.0.1 -m "limine-manager v1.0.1"

git push origin main
git push origin v1.0.1
```

A GitHub release can then be created with:

```bash
gh release create v1.0.1
```

If a release archive is attached, generate it from the exact release contents and verify its checksum before publishing.

The reconstructed SHA-256 associated with the generated `v1.0.1` artifact was:

```text
3a577e02e8015de19371d0f57a8792fc5f91a88873d1096db552f04844f4b3c7
```

Do not assume this checksum applies to a newly regenerated archive.

Any regenerated archive must receive a newly calculated checksum.

---

# 16. Current Known Status

## Working or substantially implemented

```text
[✓] C++20 project architecture
[✓] CMake build system
[✓] system detection
[✓] Btrfs root detection
[✓] root subvolume detection
[✓] /boot mount detection
[✓] Limine configuration detection
[✓] kernel command-line loading/parsing
[✓] kernel discovery
[✓] CPU microcode discovery
[✓] validation framework
[✓] change planning
[✓] Limine rendering architecture
[✓] unified diff rendering
[✓] safe apply architecture
[✓] backup architecture
[✓] tests
[✓] Btrfs findmnt-source normalization fix
[✓] cryptdevice support
[✓] rd.luks.name support
```

---

## Must be verified against the current repository

```text
[?] Current branch and commit
[?] Whether all formatting violations are resolved
[?] Whether all CI jobs pass on the v1.0.1 commit
[?] Exact current CLI option names
[?] Exact release artifact filename
[?] Whether v1.0.1 is already pushed to GitHub
[?] Whether the GitHub release is already published
[?] Complete real-system output after installing the corrected v1.0.1
[?] End-to-end snapshot boot entry generation on the real machine
[?] Successful boot of a generated snapshot entry
```

These items must not be guessed.

---

# 17. Current Objective

The immediate objective after the `v1.0.1` validation fixes is to verify the corrected release on the real Arch Linux system.

The next agent should begin by checking the actual repository and then perform:

```text
1. Confirm current Git status and version.
2. Build the exact current source.
3. Run all tests.
4. Run formatting/quality checks.
5. Install the verified build.
6. Execute the non-destructive validation/preview workflow.
7. Capture the complete output.
8. Confirm that the two previous validation errors are gone.
9. Inspect the generated Limine configuration/diff.
10. Do not apply or reboot until the generated boot entries are technically reviewed.
```

---

# 18. Next Technical Milestone

Once `v1.0.1` passes real-system validation, the next milestone is:

```text
Reliable end-to-end generation of the desired Limine menu,
including current-system and Snapper snapshot boot entries.
```

The generated hierarchy should be equivalent to:

```text
Arch Linux
├── Linux
└── Snapshots
    ├── <snapshot date/time>
    │   └── Linux
    └── ...
```

The next agent must verify:

```text
kernel path
microcode path
initramfs path
root device semantics
LUKS unlock arguments
Btrfs rootflags
snapshot subvolume path
Limine syntax
menu nesting
snapshot description
```

before considering a snapshot entry safe to boot.

---

# 19. Do Not Reintroduce

Do not reintroduce the following previously rejected directions:

```text
- a Zsh-only core implementation
- a Bash-only core implementation
- destructive direct writes without preview
- hard-coded assumptions about a single exact root-device representation
- validation that only recognizes cryptdevice=
- raw comparison of findmnt Btrfs sources containing [/subvolume]
- large shell scripts duplicating the C++ application
```

---

# 20. Handoff Summary

A new agent should understand the project as follows:

```text
limine-manager is no longer a shell-script experiment.

It is a C++20 safety-oriented system utility with explicit layers for
domain logic, operating-system interaction, validation, rendering,
change planning, preview, safe apply, and recovery.

Version 1.0.1 addressed two real-system validation bugs:
Btrfs source normalization and support for rd.luks.name.

The next task is not to redesign the project.

The next task is to verify the corrected v1.0.1 against the real system,
confirm all quality checks, inspect the generated Limine configuration,
and then continue toward safe end-to-end snapshot boot testing.
```
