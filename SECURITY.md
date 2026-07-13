# Security policy

## Supported versions

The latest 1.x release receives security fixes. Development snapshots and pre-1.0 releases are unsupported after 1.0.0.

## Reporting a vulnerability

Do not open a public issue for a vulnerability that could lead to privilege escalation, unsafe ESP writes, arbitrary file replacement, or boot-chain compromise. Report it privately to the repository maintainers and include:

- affected version and commit;
- exact command and configuration;
- expected and observed behavior;
- a minimal reproduction when safe;
- whether root privileges are required.

Avoid attaching private keys, LUKS recovery material, complete machine identifiers, or sensitive kernel command lines.

## Security boundaries

`apply`, `restore`, and `prune-backups` are privileged operations. The project rejects symbolic-link targets, uses same-directory temporary files, locking, synchronized backups, atomic replacement, post-write verification, and rollback. Administrators should still review `dry-run` output and protect `/etc/limine-manager` and the ESP from untrusted writers.
