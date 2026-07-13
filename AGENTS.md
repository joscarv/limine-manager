# AGENTS.md

## Purpose

This file defines the rules that any AI agent or developer must follow when working on `limine-manager`.

It is the authoritative source for development behavior, architectural boundaries, validation expectations, and change discipline.

For the current project state, read:

* `docs/PROJECT_CONTEXT.md`

For architectural and historical decisions, read:

* `docs/DECISIONS.md`

The source code is the ultimate source of truth for what is actually implemented.

---

# 1. Project Overview

`limine-manager` is a Linux system utility written primarily in modern C++.

Its purpose is to safely discover the current Arch Linux boot environment and generate or maintain a structured Limine boot menu containing:

```text
Arch Linux
├── Linux
└── Snapshots
    ├── <snapshot date/time>
    │   └── Linux
    ├── <snapshot date/time>
    │   └── Linux
    └── ...
```

The normal Linux entry must boot the currently installed system.

Snapshot entries must boot supported Btrfs snapshots discovered through Snapper.

The project is designed around safety:

* inspect before modifying;
* validate before applying;
* preview changes before writing;
* preserve the existing boot configuration;
* create recoverable backups;
* reject stale plans;
* avoid shell-based parsing for core system logic.

---

# 2. Primary Target Environment

The initial target environment is:

* Arch Linux
* Limine bootloader
* Btrfs root filesystem
* Snapper
* LUKS2 encrypted root supported
* ESP mounted at `/boot`
* Limine configuration at `/boot/limine.conf`
* kernel images stored under `/boot`
* kernel command line available through `/etc/kernel/cmdline`
* mkinitcpio-based initramfs

The reference development/test system observed during development used:

```text
Root filesystem: Btrfs
Root subvolume: @
Boot mount: /boot
Boot filesystem: vfat
Limine configuration: /boot/limine.conf
Kernel image: /boot/vmlinuz-linux
CPU microcode: /boot/intel-ucode.img
```

Do not hard-code these values as universal assumptions when they can be safely detected.

---

# 3. Technology and Build Rules

## Language

Use:

```text
C++20
```

Do not lower the language standard unless there is a documented project decision.

## Build system

Use CMake.

Preferred build flow:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Installation

Use the CMake installation rules:

```bash
sudo cmake --install build
```

Do not manually copy the executable into `/usr/bin` as the normal installation procedure.

## Dependencies

Keep runtime dependencies minimal.

Prefer:

* the C++ standard library;
* standard Linux/POSIX interfaces;
* established system utilities where querying the operating system is more reliable than reimplementing system databases.

Do not introduce a new dependency unless it provides a clear architectural or correctness benefit.

---

# 4. Architecture

The project follows a layered architecture.

The known structure includes components under namespaces/directories equivalent to:

```text
include/limine_manager/
├── application/
├── config/
├── domain/
├── infrastructure/
└── render/

src/
├── application/
├── config/
├── domain/
├── infrastructure/
├── render/
└── main.cpp

tests/
└── tests.cpp
```

The exact repository tree may evolve. Inspect the repository before assuming that every file listed here still exists.

---

# 5. Layer Responsibilities

## `domain`

Contains pure project concepts and logic.

Examples include:

* kernel command-line representation and parsing;
* Limine document/menu models;
* validation reports;
* boot-entry concepts;
* snapshot-related domain data.

Domain code should:

* avoid direct filesystem access;
* avoid launching external processes;
* avoid modifying the system;
* be independently testable.

---

## `infrastructure`

Contains interaction with the operating system.

Known responsibilities include:

* filesystem abstraction;
* POSIX process execution;
* system detection;
* kernel discovery;
* Snapper discovery;
* reading system files.

Examples of known components include:

```text
FileSystem
ProcessRunner
PosixProcessRunner
SystemDetector
KernelDiscovery
```

Infrastructure code may interact with:

```text
findmnt
uname
Snapper
/proc
/sys
/boot
/etc
```

Core application logic must not depend directly on shell scripts.

---

## `config`

Contains `limine-manager` configuration loading and rendering.

Known concepts include:

```text
Config
LoadedConfig
ConfigLoader
```

Configuration handling must:

* support built-in defaults;
* distinguish built-in defaults from an explicitly loaded file;
* validate the configuration schema;
* fail clearly on invalid configuration.

---

## `application`

Coordinates use cases.

Known responsibilities include:

* system validation;
* planning configuration changes;
* safely applying changes;
* managing backups;
* rejecting stale plans.

Known components include concepts equivalent to:

```text
ValidationService
ChangePlan
ChangePlanner
ApplyService
```

Application services may coordinate domain and infrastructure components but should not contain arbitrary CLI presentation logic.

---

## `render`

Converts project models into user-visible text.

Known responsibilities include:

* rendering Limine configuration;
* rendering unified diffs.

Known components include:

```text
LimineRenderer
UnifiedDiffRenderer
```

Rendering should be deterministic.

Given the same input model, the generated output should be identical.

---

## `main.cpp`

The executable entry point is responsible for:

* parsing CLI arguments;
* selecting the requested operation;
* constructing dependencies;
* invoking application services;
* presenting results;
* mapping failures to useful exit behavior.

Do not move core business logic into `main.cpp`.

---

# 6. Core Development Principles

## 6.1 Detect, do not assume

System-specific information must be detected whenever practical.

Examples:

* root filesystem;
* root source device;
* Btrfs root subvolume;
* boot mount;
* boot filesystem;
* Limine configuration;
* kernel installations;
* microcode images;
* kernel release;
* kernel command line;
* Snapper configuration;
* snapshots.

---

## 6.2 Normalize before comparing

System tools may represent equivalent resources differently.

A known example is `findmnt` returning a Btrfs source with a subvolume suffix:

```text
/dev/mapper/cryptroot[/@]
```

while another source may refer to:

```text
/dev/mapper/cryptroot
```

These must be normalized before device identity is compared.

Do not compare raw device-source strings when system tools can append metadata such as:

```text
[/@]
```

---

## 6.3 Support valid encrypted-root syntax

The validation logic must recognize supported LUKS root declarations.

At minimum, the project must not reject valid configurations solely because they use either of these forms:

```text
cryptdevice=UUID=<uuid>:cryptroot
```

or:

```text
rd.luks.name=<uuid>=cryptroot
```

The exact parser implementation must remain explicit and tested.

Do not rely on substring matching that can accidentally accept malformed syntax.

---

## 6.4 Preview before apply

Generating a proposed configuration must not modify:

```text
/boot/limine.conf
```

The normal workflow is:

```text
detect
→ validate
→ build model
→ render candidate
→ create ChangePlan
→ show preview/diff
→ explicitly apply
```

Do not collapse preview and apply into one implicit operation.

---

## 6.5 Never overwrite blindly

Before replacing an existing configuration:

1. verify that the plan still applies to the current file;
2. preserve the previous content;
3. write safely;
4. avoid leaving a partially written configuration;
5. make recovery possible.

A plan generated against old content must be rejected if the target changed before apply.

---

## 6.6 Keep shell usage outside core logic

The project was intentionally restarted using a hybrid architecture:

```text
C++:
- detection
- parsing
- domain model
- validation
- change planning
- rendering
- safe apply
- backup management

Shell:
- installation helpers
- packaging helpers
- hooks when appropriate
```

Do not migrate core system detection or configuration generation back into a large Bash or Zsh script.

Shell scripts must not become an alternative implementation of the C++ application.

---

# 7. Limine Menu Requirements

The intended logical hierarchy is:

```text
Arch Linux
├── Linux
└── Snapshots
    ├── YYYY-MM-DD HH:MM
    │   └── Linux
    ├── YYYY-MM-DD HH:MM
    │   └── Linux
    └── ...
```

## Normal Linux entry

The normal entry must:

* boot the installed Arch Linux system;
* use the discovered kernel;
* include required microcode/initramfs modules;
* preserve the appropriate kernel command line;
* expose the installed kernel release as useful descriptive information where supported by the generated Limine configuration.

## Snapshots submenu

Snapshot entries must:

* be generated from supported Snapper snapshots;
* use a human-readable date/time as the menu level;
* contain a Linux boot entry;
* use the snapshot description where useful;
* boot the selected Btrfs snapshot rather than the current `@` subvolume.

Do not invent snapshot paths from the displayed snapshot date.

Use the actual snapshot metadata and filesystem layout.

---

# 8. Kernel Discovery Rules

Kernel discovery must not assume that only one kernel will always exist.

A kernel installation may include:

* Linux kernel image;
* kernel release;
* CPU microcode image;
* initramfs images.

The reference environment currently discovered:

```text
/boot/vmlinuz-linux
/boot/intel-ucode.img
```

Future work may encounter:

```text
linux
linux-lts
linux-zen
linux-hardened
```

Do not implement new behavior in a way that prevents multiple kernel installations from being represented later.

---

# 9. Kernel Command-Line Rules

Kernel command-line parsing is security- and boot-critical.

The parser must correctly handle:

* whitespace-separated arguments;
* quoted values where supported;
* escaped characters;
* malformed quoting;
* malformed trailing escapes.

Do not parse the command line with a naive:

```cpp
split(' ')
```

The parsed command line should preserve argument meaning when rendered again.

When changing only the root snapshot, modify only the arguments that actually need to change.

Do not accidentally discard unrelated arguments such as:

```text
rw
quiet
splash
nvidia_drm.modeset=1
```

---

# 10. Validation Rules

Validation must produce actionable diagnostics.

Use severity levels equivalent to:

```text
INFO
WARNING
ERROR
```

Validation should verify relevant conditions such as:

* root filesystem is Btrfs;
* root subvolume is detectable;
* boot mount is available;
* Limine configuration exists or its intended target is known;
* kernel command-line source is readable;
* kernel installations are discoverable;
* required kernel files are readable;
* root-device semantics are internally consistent;
* supported encrypted-root syntax is recognized;
* Snapper configuration is usable when snapshots are requested.

A valid but differently represented system configuration must not fail because two tools format the same device differently.

---

# 11. Apply and Backup Safety

The apply path is one of the most sensitive parts of the project.

Changes must be transactional as far as reasonably possible.

The implementation must:

* detect stale plans;
* avoid blindly truncating the target;
* create backups;
* use unique temporary/backup paths where required;
* preserve recoverability;
* propagate write errors;
* handle interrupted POSIX reads/writes correctly;
* avoid silently succeeding after partial failure.

Backup listing and restore behavior must operate on backups created by the application.

Never implement a destructive cleanup policy without an explicit documented decision.

---

# 12. Testing Requirements

Every bug fix involving parsing, detection, validation, planning, rendering, or apply safety must include or update a regression test when practical.

Before considering work complete, run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

If quality targets exist, run them as well.

The project has previously used `clang-format` verification.

Before a release, verify formatting rather than assuming successful compilation is sufficient.

Do not declare a release ready while required CI quality checks are failing.

---

# 13. Formatting and Code Quality

Follow the repository's existing `.clang-format`.

When changing C++ files, run the project's formatting workflow before committing.

Typical manual verification may use:

```bash
clang-format --dry-run --Werror <files>
```

or the repository's CMake/CI quality target.

Prefer:

* RAII;
* value semantics;
* `std::filesystem`;
* `std::optional` for genuinely optional values;
* `std::string_view` for non-owning string input where lifetime is clear;
* explicit error handling;
* small focused functions;
* dependency injection for filesystem/process boundaries.

Avoid:

* `system()`;
* shell command construction with untrusted interpolation;
* global mutable state;
* hidden filesystem writes;
* silent catch-all exception handling;
* unnecessary singletons.

---

# 14. Change Discipline

Before modifying code:

1. read `docs/PROJECT_CONTEXT.md`;
2. read the relevant entries in `docs/DECISIONS.md`;
3. inspect the actual repository state;
4. identify the smallest responsible component;
5. reproduce the problem when possible;
6. add or update a regression test;
7. implement the change;
8. build;
9. run tests;
10. run formatting/quality checks;
11. summarize exactly what changed.

Do not redesign working architecture unless the current task requires it.

Do not replace an existing subsystem merely because another approach is personally preferred.

---

# 15. Source-of-Truth Priority

When information conflicts, use this order:

```text
1. Current repository source code
2. Current automated tests
3. docs/PROJECT_CONTEXT.md
4. docs/DECISIONS.md
5. README.md
6. Historical conversation context
```

Exception:

A documented invariant or explicit project requirement must not be silently removed merely because the current implementation is incomplete.

In that case, report the discrepancy.

---

# 16. Documentation Update Rule

After a meaningful development session, update:

```text
docs/PROJECT_CONTEXT.md
```

Update `docs/DECISIONS.md` only when:

* an architectural decision is made;
* a previous decision is superseded;
* an important alternative is explicitly rejected;
* a compatibility or safety policy changes.

Do not turn `DECISIONS.md` into a chronological development diary.

---

# 17. Release Rules

Before creating a release:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Also run all configured quality checks.

Verify:

```bash
./build/limine-manager --version
```

The reported version must match the intended release tag.

Release tags use:

```text
vMAJOR.MINOR.PATCH
```

Example:

```text
v1.0.1
```

Do not reuse or silently move a published release tag.

---

# 18. Current Important Regression Requirements

The following behaviors must remain covered:

1. A Btrfs source such as:

   ```text
   /dev/mapper/cryptroot[/@]
   ```

   must normalize correctly for comparison with:

   ```text
   /dev/mapper/cryptroot
   ```

2. A valid kernel command line using:

   ```text
   cryptdevice=UUID=<uuid>:cryptroot
   ```

   must be recognized.

3. A valid kernel command line using:

   ```text
   rd.luks.name=<uuid>=cryptroot
   ```

   must be recognized.

4. Preview must not modify the target Limine configuration.

5. Apply must reject a stale change plan.

6. Existing configuration must be recoverable through the backup mechanism.

---

# 19. Agent Handoff Protocol

When finishing a substantial task, the agent should leave the project in a state where another agent can answer:

```text
What works?
What changed?
What is still broken?
What was verified?
What should happen next?
```

Update `docs/PROJECT_CONTEXT.md` accordingly.

Never leave the only explanation of an important implementation decision inside a chat conversation.

