# limine-manager

`limine-manager` generates and safely maintains a deterministic Limine menu for Arch Linux systems using Btrfs, LUKS and Snapper snapshots.

## Features

- Detects installed Arch kernels and maps each image to its real kernel release.
- Parses `/etc/kernel/cmdline` without shell evaluation.
- Reads Snapper configuration and snapshots through stable CSV output.
- Builds a hierarchical Limine menu for the live system and bootable snapshots.
- Validates the boot mount, Btrfs subvolume, encrypted root mapping, kernel files and snapshot paths.
- Provides preview, status, plan, unified diff and dry-run workflows.
- Applies changes with locking, synchronized backups, same-filesystem temporary files, atomic rename, verification and rollback.
- Lists, restores and prunes managed backups.
- Includes an opt-in pacman hook, man page, CI workflows and an Arch `PKGBUILD` template.

## Requirements

Runtime:

```text
glibc
gcc-libs
util-linux
snapper
```

Build:

```text
CMake >= 3.20
Ninja or Make
C++20 compiler
```

## Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests are designed to run without root privileges. Do not run `ctest` with `sudo`.

## Quality builds

AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLIMINE_MANAGER_ENABLE_ASAN=ON \
  -DLIMINE_MANAGER_ENABLE_UBSAN=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Coverage:

```bash
cmake -S . -B build-coverage -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLIMINE_MANAGER_ENABLE_COVERAGE=ON
cmake --build build-coverage
ctest --test-dir build-coverage --output-on-failure
```

clang-tidy:

```bash
cmake -S . -B build-tidy -G Ninja -DLIMINE_MANAGER_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy
```

Formatting targets require `clang-format`:

```bash
cmake --build build --target format
cmake --build build --target format-check
```

## Installation

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Installed files include the binary, update helper, example configuration, optional pacman hook, release helper and `limine-manager(8)` man page.

## CLI

```bash
limine-manager --help
limine-manager --version
limine-manager check-config
limine-manager validate
limine-manager preview
limine-manager show-config
limine-manager status
limine-manager plan
limine-manager diff
limine-manager dry-run
sudo limine-manager apply
limine-manager list-backups
sudo limine-manager restore
sudo limine-manager --backup /boot/limine.conf.bak.<id> restore
sudo limine-manager prune-backups
```

Additional diagnostics can be enabled with:

```bash
limine-manager --verbose status
limine-manager --verbose --log-format json status
```

JSON diagnostics are newline-delimited and written to standard error. Command output remains on standard output.

## Configuration

The optional default configuration is:

```text
/etc/limine-manager/limine-manager.conf
```

The stable configuration schema is version 1. Files without an explicit `[manager]` section are treated as schema 1 for compatibility. Unknown future schema versions are rejected. See `config/limine-manager.conf.example` and inspect effective values with:

```bash
limine-manager show-config
```

Validate only the configuration file without inspecting the system:

```bash
limine-manager check-config
```

## Shell completion

Bash and Zsh completion files are installed automatically under the standard `/usr/share` locations. Start a new shell after installation, or refresh the completion cache.

## Pacman hook

The hook is installed as an example and is not enabled automatically:

```bash
sudo install -Dm644 \
  /usr/share/limine-manager/hooks/90-limine-manager.hook.example \
  /etc/pacman.d/hooks/90-limine-manager.hook
```

The helper validates the system before invoking `apply`.

## Reproducible source archive

```bash
scripts/create-release-tarball
```

Outside a Git checkout, the script normalizes file order, ownership, timestamps and gzip metadata. It prints the resulting SHA-256 checksum.

## Packaging

`packaging/PKGBUILD` is ready structurally for an Arch package. Before publishing a release, replace the repository URL and `REPLACE_WITH_RELEASE_SHA256` with the final project location and checksum.

## Documentation

After installation:

```bash
man 8 limine-manager
```

See `CONTRIBUTING.md` for the contributor verification workflow.

## License

MIT
