# Migrating from 0.9.x to 1.0.0

No command or configuration migration is required.

Existing configuration files without a `[manager]` section are interpreted as schema version 1. To make the version explicit, add this block at the beginning:

```ini
[manager]
schema_version = 1
```

Validate the file independently of the host system:

```bash
limine-manager check-config
```

Then perform the normal verification flow:

```bash
limine-manager validate
limine-manager dry-run
sudo limine-manager apply
```

Unsupported future schema versions are rejected instead of being partially interpreted.
