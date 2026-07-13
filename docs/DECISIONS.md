# DECISIONS.md

# Architecture and Technical Decision Log

This document records decisions that materially affect the architecture, safety model, compatibility, or long-term direction of `limine-manager`.

It is not a development diary.

Status values:

```text
ACCEPTED
SUPERSEDED
REJECTED
PROPOSED
```

---

# ADR-001: Use C++ for the core application

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The project initially explored a shell-based implementation for detecting the Arch Linux boot environment and generating Limine configuration.

As the required behavior expanded, the application needed to handle:

* filesystem detection;
* Btrfs subvolumes;
* encrypted root devices;
* kernel discovery;
* structured kernel command-line parsing;
* Snapper metadata;
* hierarchical menu modeling;
* deterministic rendering;
* validation;
* preview;
* backup;
* safe apply;
* stale-plan detection.

Implementing all core behavior in shell would make parsing, modeling, testing, and safe mutation increasingly difficult.

## Decision

The core application will be implemented in:

```text
C++20
```

C++ owns:

```text
system detection
parsing
domain models
validation
menu generation
rendering
change planning
safe apply
backup management
```

## Consequences

Positive:

* stronger data modeling;
* better testability;
* clearer architecture;
* safer parsing;
* easier deterministic rendering;
* better control over POSIX filesystem operations.

Negative:

* compilation is required;
* implementation is more verbose than a small shell script;
* release/build tooling is required.

---

# ADR-002: Shell is allowed only for supporting integration

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

Shell remains useful for Linux installation and package integration.

The project does not need to eliminate shell entirely.

## Decision

Shell may be used for:

```text
installation helpers
packaging
package-manager hooks
small integration tasks
```

Shell must not duplicate the C++ core.

## Rejected alternatives

### Zsh-only implementation

Rejected because the application should not depend on an interactive user's shell.

### Bash-only core implementation

Rejected because the required parsing, modeling, validation, testing, and safe-write behavior is better represented in the C++ application.

---

# ADR-003: Preview is separate from apply

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The project modifies bootloader configuration.

An incorrect write to:

```text
/boot/limine.conf
```

can prevent the system from booting correctly.

## Decision

The normal workflow must separate:

```text
generation
preview
apply
```

Generating or previewing a configuration must not modify the active Limine configuration.

## Consequences

The user or calling workflow can inspect the proposed change before applying it.

This also enables unified diff output and safer debugging.

---

# ADR-004: Represent filesystem changes as a ChangePlan

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

Directly rendering and immediately writing a file couples generation with mutation.

The project needs to distinguish:

```text
new file
modified file
unchanged file
```

It also needs to detect whether the target changed between preview and apply.

## Decision

A proposed filesystem mutation is represented by a:

```text
ChangePlan
```

The plan contains enough information to determine the intended change and verify that it is still valid when applied.

## Consequences

The application can:

* show a preview;
* render a diff;
* skip unchanged files;
* reject stale plans;
* separate planning from mutation.

---

# ADR-005: Reject stale plans

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

A configuration file may change after a plan is generated.

Applying an old plan without checking the current target could overwrite a newer change.

## Decision

Before applying a plan, the application must verify that the target still matches the state against which the plan was created.

If it does not, the apply operation must fail.

## Consequences

The user must regenerate the plan against the current file.

This is intentionally safer than silently overwriting concurrent changes.

---

# ADR-006: Preserve recoverability through backups

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

Boot configuration changes are high-impact.

Even a syntactically valid generated configuration may contain a semantic mistake.

## Decision

The apply workflow must preserve the previous configuration through a backup mechanism.

The project may expose:

```text
backup listing
backup restoration
```

## Consequences

Configuration changes remain recoverable without depending exclusively on version control.

Backup handling must itself avoid ambiguous or destructive behavior.

---

# ADR-007: Use a layered architecture

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The application contains several different categories of responsibility:

```text
domain rules
operating-system interaction
configuration
application orchestration
text rendering
CLI presentation
```

Combining them in one executable source file would make testing and future development difficult.

## Decision

Use logical layers equivalent to:

```text
domain
infrastructure
config
application
render
CLI/main
```

## Responsibilities

### Domain

Pure concepts and rules.

### Infrastructure

Filesystem, process execution, system detection, Snapper, and kernel discovery.

### Config

Application configuration loading and validation.

### Application

Use-case orchestration, validation, planning, and apply.

### Render

Limine output and human-readable diff generation.

### CLI

Argument handling and presentation.

## Consequences

Dependencies should flow toward stable abstractions and domain concepts.

Core domain behavior should remain testable without accessing the real system.

---

# ADR-008: Detect system state instead of hard-coding the reference machine

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The initial machine has known characteristics:

```text
Arch Linux
Btrfs
root subvolume @
LUKS2
ESP at /boot
Limine config at /boot/limine.conf
Intel microcode
```

Hard-coding these values would make the application fragile and unnecessarily specific.

## Decision

Detect system state wherever practical.

Examples include:

```text
root source
root filesystem
root subvolume
boot mount
boot filesystem
kernel installations
microcode
kernel command line
Snapper configuration
```

## Consequences

The project can support valid variations of the target environment.

Validation must operate on semantics rather than one exact string representation.

---

# ADR-009: Normalize Btrfs source representations before comparison

```yaml
status: ACCEPTED
date: 2026-07-13
release: 1.0.1
```

## Context

On the real target system, filesystem tooling may report a Btrfs source as:

```text
/dev/mapper/cryptroot[/@]
```

while the root device is otherwise represented as:

```text
/dev/mapper/cryptroot
```

These can refer to the same underlying device.

Raw string comparison produced a false validation error.

## Decision

Normalize a Btrfs source representation before comparing underlying device identity.

A recognized subvolume suffix such as:

```text
[/@]
```

must not cause the underlying device to be treated as different.

## Consequences

Validation becomes robust against `findmnt` Btrfs source formatting.

Normalization must remain conservative.

The implementation must not strip arbitrary text merely to force two values to match.

---

# ADR-010: Support multiple valid encrypted-root command-line conventions

```yaml
status: ACCEPTED
date: 2026-07-13
release: 1.0.1
```

## Context

Encrypted Arch Linux systems may express LUKS activation using different valid kernel command-line conventions.

One supported form is:

```text
cryptdevice=UUID=<uuid>:cryptroot
```

The real target environment used a form equivalent to:

```text
rd.luks.name=<uuid>=cryptroot
```

Validation that recognizes only `cryptdevice=` incorrectly rejects a valid system.

## Decision

The validator must recognize supported encrypted-root declarations including:

```text
cryptdevice=UUID=<uuid>:<mapper-name>
```

and:

```text
rd.luks.name=<uuid>=<mapper-name>
```

## Consequences

The application supports both common initramfs conventions relevant to the project.

Validation must compare the parsed semantics:

```text
LUKS device identity
mapper name
root mapping
```

rather than requiring one exact textual syntax.

---

# ADR-011: Kernel command-line parsing must be structured

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The kernel command line contains boot-critical arguments.

Naively splitting only on spaces can fail when quoting or escaping is involved.

Snapshot boot generation may need to change only root/subvolume-related arguments while preserving all unrelated options.

## Decision

Represent the kernel command line as a structured sequence of arguments.

The parser must detect malformed input such as:

```text
unterminated quotes
invalid trailing escape state
```

Rendering must preserve argument meaning.

## Consequences

Snapshot generation can modify only the required boot arguments.

Unrelated arguments such as:

```text
quiet
splash
nvidia_drm.modeset=1
```

should remain intact.

---

# ADR-012: Generate Limine configuration from a model

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The desired boot menu is hierarchical:

```text
Arch Linux
├── Linux
└── Snapshots
    └── <snapshot>
        └── Linux
```

Building this directly through scattered string concatenation would make nesting and validation difficult.

## Decision

Represent the intended Limine document/menu as structured data and render it through a dedicated renderer.

Known renderer:

```text
LimineRenderer
```

## Consequences

Rendering becomes deterministic and independently testable.

Menu construction and Limine text formatting remain separate concerns.

---

# ADR-013: Use a dedicated unified diff renderer

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The user must be able to inspect changes before modifying the active boot configuration.

Displaying only the complete generated file makes small changes harder to review.

## Decision

Provide unified diff output for planned changes.

Known component:

```text
UnifiedDiffRenderer
```

## Consequences

The user can inspect additions and removals before apply.

The diff is informational; the `ChangePlan`, not the diff text, remains the representation of the actual intended change.

---

# ADR-014: Root Snapper configuration is the initial boot-snapshot source

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The reference system has Snapper configurations for:

```text
/
/home
```

A `/home` snapshot is not independently sufficient to represent a bootable system root.

## Decision

The initial boot-snapshot menu is based on snapshots associated with the root filesystem.

Do not automatically create boot entries from `/home` snapshots.

## Consequences

The project remains focused on system boot recovery.

Support for more complex multi-subvolume snapshot coordination would require a separate design decision.

---

# ADR-015: Preserve future multiple-kernel support

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

The current reference system discovered one kernel installation:

```text
linux
```

Arch Linux systems may install multiple kernels, including:

```text
linux
linux-lts
linux-zen
linux-hardened
```

## Decision

Kernel discovery and internal models must not be designed around the permanent assumption that exactly one kernel exists.

## Consequences

The current menu may initially expose one discovered kernel, but the architecture should allow multiple kernel installations without a major redesign.

---

# ADR-016: CMake installation is the canonical installation path

```yaml
status: ACCEPTED
date: 2026-07
```

## Context

Manually copying the executable makes installation behavior difficult to reproduce and maintain.

The project already uses CMake.

## Decision

The canonical source installation workflow is:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

## Consequences

Install destinations are controlled by the build system.

Packaging can later build on the same installation rules.

---

# ADR-017: Formatting and quality checks are release requirements

```yaml
status: ACCEPTED
date: 2026-07-13
```

## Context

A captured quality run reported widespread `clang-format` verification failures.

Successful compilation alone does not guarantee that the repository passes its CI requirements.

## Decision

A release is considered verified only after:

```text
build succeeds
tests pass
required formatting checks pass
required quality checks pass
```

## Consequences

Formatting failures must be corrected before declaring the corresponding commit release-ready.

A historical quality log does not prove that the current repository still fails; the exact current commit must be checked.

---

# ADR-018: The repository is the implementation source of truth

```yaml
status: ACCEPTED
date: 2026-07-13
```

## Context

Development occurred through a long AI-assisted conversation.

Conversation history contains:

* early designs;
* rejected approaches;
* intermediate implementations;
* bug reports;
* corrected implementations.

A new agent reading the full conversation could mistake an obsolete implementation for the current one.

## Decision

Use the following authority order:

```text
1. Current repository source
2. Current tests
3. PROJECT_CONTEXT.md
4. DECISIONS.md
5. README.md
6. Historical chat
```

## Consequences

Conversation history provides background but does not override the current implementation.

Important current state must be transferred into project documentation.

---

# ADR-019: Maintain a versioned project checkpoint

```yaml
status: ACCEPTED
date: 2026-07-13
```

## Context

The project may continue across multiple AI chats or coding agents.

Passing the complete conversation is inefficient and can introduce obsolete context.

## Decision

Maintain:

```text
AGENTS.md
docs/PROJECT_CONTEXT.md
docs/DECISIONS.md
```

with distinct responsibilities.

```text
AGENTS.md
→ permanent working rules

PROJECT_CONTEXT.md
→ current project checkpoint

DECISIONS.md
→ architectural reasoning and superseded decisions
```

Increment:

```text
context_version
```

whenever `PROJECT_CONTEXT.md` receives a meaningful checkpoint update.

## Consequences

A new agent can continue the project without processing the complete historical conversation.

The checkpoint must be updated when the current project state materially changes.

