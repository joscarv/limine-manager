# Releasing limine-manager

Releases are created automatically by GitHub Actions when a semantic version tag is pushed.

## Prerequisites

Before creating a release:

1. Update the project version in `CMakeLists.txt`.
2. Ensure the branch is fully synchronized and CI is green.
3. Run the local validation commands:

```bash
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target format-check
```

4. Verify that the source archive is reproducible:

```bash
version=$(sed -n 's/^project(limine-manager VERSION \([^ ]*\).*/\1/p' CMakeLists.txt)
mkdir -p dist
scripts/create-release-tarball "$version" "dist/limine-manager-$version.tar.gz"
scripts/create-release-tarball "$version" "dist/limine-manager-$version.rebuilt.tar.gz"
cmp "dist/limine-manager-$version.tar.gz" "dist/limine-manager-$version.rebuilt.tar.gz"
rm "dist/limine-manager-$version.rebuilt.tar.gz"
```

## Create the release

Create an annotated tag whose version exactly matches `CMakeLists.txt`:

```bash
version=$(sed -n 's/^project(limine-manager VERSION \([^ ]*\).*/\1/p' CMakeLists.txt)
git tag -s "v$version" -m "Release v$version"
git push origin "v$version"
```

The release workflow will:

- validate the tag and project versions;
- compile and run the test suite in Release mode;
- generate the source archive twice and compare both files;
- generate an SPDX JSON SBOM;
- generate `SHA256SUMS`;
- optionally create detached ASCII-armored GPG signatures;
- generate a GitHub/Sigstore provenance attestation for the source archive;
- create the GitHub Release with generated release notes.

## Optional GPG asset signing

The annotated Git tag should be signed locally with the maintainer key. The release assets can additionally be signed in GitHub Actions by configuring these repository secrets:

- `RELEASE_GPG_PRIVATE_KEY`: ASCII-armored exported private key.
- `RELEASE_GPG_PASSPHRASE`: passphrase for that private key.

Export a dedicated release-signing key rather than a general-purpose personal key:

```bash
gpg --armor --export-secret-keys KEY_ID > release-private-key.asc
```

Store the contents of that file in `RELEASE_GPG_PRIVATE_KEY`, then securely delete the exported file. Never commit private key material to the repository.

When the private-key secret is absent, the workflow still publishes checksums, the SBOM, and the GitHub artifact attestation, but it does not create `.asc` files.

## Verify downloaded assets

Checksums:

```bash
sha256sum --check SHA256SUMS
```

GPG signatures, when published:

```bash
gpg --verify limine-manager-VERSION.tar.gz.asc limine-manager-VERSION.tar.gz
gpg --verify limine-manager-VERSION.spdx.json.asc limine-manager-VERSION.spdx.json
gpg --verify SHA256SUMS.asc SHA256SUMS
```

GitHub provenance attestation:

```bash
gh attestation verify limine-manager-VERSION.tar.gz --repo joscarv/limine-manager
```
