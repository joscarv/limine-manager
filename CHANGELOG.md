# Changelog

All notable changes to this project are documented here. The project follows Semantic Versioning.

## [1.1.0] - 2026-07-13

### Added

- Add guarded Btrfs snapshot rollback commands: `rollback-status`, `rollback-plan`, and `rollback`.
- Add rollback planning models, Btrfs infrastructure, execution phases, locking, topology verification, and partial recovery after failed subvolume moves.
- Preserve the previous main root subvolume under a unique `@.limine-manager.rollback.<snapshot>.<transaction>` name before replacing `@`.
- Regenerate `limine.conf` after a successful Btrfs rollback using the existing apply workflow.
- Add regression tests for rollback eligibility, conflicts, successful execution, and recovery after a failed final move.
- Add built-in visual theme presets and a `themes` command to list them.

### Compatibility

- Configuration schema remains version 1.
- Existing `apply`, `restore`, backup, and Limine rendering behavior remain unchanged.
- `restore` still restores only managed `limine.conf` backups; it does not perform Btrfs system rollback.

## [1.0.1] - 2026-07-13

### Fixed

- Normalize Btrfs `findmnt` sources such as `/dev/mapper/cryptroot[/@]` before comparing them with `root=`.
- Support both mkinitcpio encryption parameter families: `cryptdevice=UUID=...:mapper` and repeatable `rd.luks.name=UUID=mapper`.
- Add regression tests for `sd-encrypt`, traditional `encrypt`, repeated `rd.luks.name`, and Btrfs subvolume source suffixes.

## [1.0.0] - 2026-07-13

### Added

- Stable command-line interface for validation, preview, planning, application, backup management, and restore.
- Configuration schema version 1 through `[manager] schema_version = 1`.
- `check-config` command that validates configuration without requiring a compatible host system.
- Bash and Zsh completion files.
- Release checklist, security policy, migration guide, and stable release notes.

### Changed

- Project version and packaging metadata updated to 1.0.0.
- Effective configuration output now includes the schema version.
- Future unsupported configuration schema versions are rejected explicitly.

### Compatibility

- Configuration files created for 0.9.0 remain valid. A missing schema version is interpreted as schema 1.
- Existing commands and exit codes from 0.9.0 remain unchanged.

## [0.9.0] - 2026-07-13

- Added release engineering, CI, sanitizers, coverage, formatting, man page, and reproducible source archives.
