# Release checklist

1. Update `project(... VERSION ...)`, `PKGBUILD`, man page, changelog, and release notes.
2. Run format verification.
3. Build and test Debug and Release with GCC.
4. Run ASan and UBSan tests.
5. Build with Clang and run clang-tidy.
6. Perform a staged installation using `DESTDIR`.
7. Generate the source archive twice and compare SHA-256 values.
8. Update the `PKGBUILD` source URL and checksum after publishing the tag.
9. Test `check-config`, `validate`, `dry-run`, `apply`, and `restore` on the supported reference system.
10. Publish the signed tag and release notes.
