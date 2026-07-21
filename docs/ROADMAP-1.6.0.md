# limine-manager 1.6.0 roadmap

Version 1.6.0 focuses on consolidating the Secure Boot work introduced in 1.5.0, reducing coupling in the apply pipeline, and preparing the project for broader EFI signing and UKI workflows.

## Goals

1. Preserve the behavior validated on Arch Linux with UEFI Secure Boot, Limine, LUKS, Btrfs, Snapper, sbctl, and traditional kernel images.
2. Isolate Secure Boot responsibilities behind explicit application and infrastructure boundaries.
3. Move ephemeral process locks out of the EFI system partition.
4. Expand integration coverage for apply, refresh, signing, verification, and rollback.
5. Prepare a signing-backend abstraction without changing the default sbctl workflow.
6. Improve UKI discovery, validation, status, and refresh behavior.

## Phase 1 — Runtime safety and regression coverage

- Move configuration locking from the directory containing `limine.conf` to the configured runtime directory, normally `/run/limine-manager`.
- Ensure manual apply and systemd refresh use the same locking implementation.
- Add regression tests for a read-only boot mount and for concurrent apply/refresh attempts.
- Keep all persistent configuration and EFI writes transactional.

## Phase 2 — Secure Boot module boundaries

- Extract hashing, enrollment, signature verification, signing, and EFI transaction handling from the application orchestration layer.
- Introduce explicit result types for enrollment and signing operations.
- Keep command execution injectable so tests do not require firmware, sbctl keys, or a writable ESP.
- Preserve current CLI output and exit-code compatibility unless a change is documented.

## Phase 3 — Signing backend abstraction

- Introduce a signing backend interface.
- Retain sbctl as the default and initially supported backend.
- Keep room for future `sbsign` or `pesign` implementations without exposing incomplete configuration options.
- Report backend capabilities through validation and status.

## Phase 4 — UKI improvements

- Distinguish traditional kernel resources from UKI resources in the system model.
- Validate UKI signatures and EFI paths consistently.
- Ensure refresh does not attempt traditional Limine resource enrollment for UKI-only entries.
- Add encrypted Btrfs and snapshot regression scenarios for UKI installations.

## Phase 5 — Release hardening

- Run unit and integration tests with GCC and Clang.
- Run AddressSanitizer and UndefinedBehaviorSanitizer builds.
- Validate manual apply, rollback, reboot, Snapper-triggered refresh, and package installation on a real Arch Linux Secure Boot system.
- Update README, man page, configuration example, migration notes, changelog, and release checklist.

## Compatibility policy

- Configuration schema remains version 1 until a schema change is necessary.
- Existing 1.5.0 configuration files must continue to load without modification.
- sbctl remains the default Secure Boot implementation.
- Installing a development build must not modify `/boot`; changes occur only through explicit apply, refresh, restore, or rollback commands.
