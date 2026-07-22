# Releasing limine-manager

Releases are created automatically by GitHub Actions when a semantic version tag is pushed.

The same workflow can also be executed manually in validation mode. A manual run builds and tests the project, verifies archive reproducibility, generates the SBOM and checksums, optionally signs the assets, and uploads them as a temporary workflow artifact. It never creates a GitHub Release.

## Prerequisites

Before creating a release:

1. Update the project version in `CMakeLists.txt`.
2. Ensure the branch is fully synchronized and CI is green.
3. Configure the repository signing secrets described in [Release signing key](#release-signing-key).
4. Run the local validation commands:

```bash
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target format-check
```

5. Verify that the source archive is reproducible:

```bash
version=$(sed -n 's/^project(limine-manager VERSION \([^ ]*\).*/\1/p' CMakeLists.txt)
mkdir -p dist
scripts/create-release-tarball "$version" "dist/limine-manager-$version.tar.gz"
scripts/create-release-tarball "$version" "dist/limine-manager-$version.rebuilt.tar.gz"
cmp "dist/limine-manager-$version.tar.gz" "dist/limine-manager-$version.rebuilt.tar.gz"
rm "dist/limine-manager-$version.rebuilt.tar.gz"
```

## Validate the workflow without publishing

Before pushing a release tag, run the `Release` workflow manually from the GitHub Actions page and select the branch or commit that will be released.

A manual run:

- does not require a release tag;
- does not create a GitHub Release;
- does not create a provenance attestation;
- signs the assets when both signing secrets are configured;
- may run unsigned when neither signing secret is configured;
- fails when only one of the two signing secrets is configured;
- uploads the generated release files as the `release-validation-VERSION` artifact;
- retains that artifact for seven days.

Download the artifact and verify it locally:

```bash
bash scripts/verify-release-assets /path/to/release-validation-VERSION
```

The script always validates `SHA256SUMS`. When `.asc` files are present, it also verifies every detached GPG signature with the keys available in the local GPG keyring.

## Release signing key

Tagged releases require a dedicated GPG key in GitHub Actions. The workflow refuses to publish an unsigned release.

Create a dedicated key instead of reusing a general-purpose personal key:

```bash
gpg --quick-generate-key \
    "limine-manager Release Signing <YOUR_EMAIL>" \
    ed25519 sign 2y
```

List the key and copy its full fingerprint:

```bash
gpg --list-secret-keys --keyid-format LONG --with-fingerprint
```

Export the public key for distribution:

```bash
gpg --armor --export KEY_FINGERPRINT > limine-manager-release-key.asc
```

Export the private key only for loading it into GitHub Actions:

```bash
umask 077
gpg --armor --export-secret-keys KEY_FINGERPRINT > release-private-key.asc
```

Configure these repository secrets under **Settings → Secrets and variables → Actions**:

- `RELEASE_GPG_PRIVATE_KEY`: the complete ASCII-armored contents of `release-private-key.asc`;
- `RELEASE_GPG_PASSPHRASE`: the passphrase protecting the dedicated signing key.

Both secrets must be present together. A tagged release fails immediately when they are missing or incomplete.

After storing the private key in GitHub, securely remove the exported private-key file:

```bash
shred -u release-private-key.asc
```

Do not commit private key material or its passphrase. The public key may be published in the repository, attached to releases, or distributed through another trusted channel.

The workflow imports the private key into an isolated temporary GPG home, obtains its full fingerprint, signs each asset explicitly with that fingerprint, verifies the resulting signatures, and discards the temporary keyring with the runner.

## Validate signing before publishing

After configuring the secrets, execute the `Release` workflow manually.

The log should include:

```text
Imported release-signing key: FULL_FINGERPRINT
```

The uploaded validation artifact should contain:

```text
limine-manager-VERSION.tar.gz
limine-manager-VERSION.tar.gz.asc
limine-manager-VERSION.spdx.json
limine-manager-VERSION.spdx.json.asc
SHA256SUMS
SHA256SUMS.asc
```

Import the public key and verify the artifact locally:

```bash
gpg --import limine-manager-release-key.asc
bash scripts/verify-release-assets /path/to/release-validation-VERSION
```

Confirm that the fingerprint printed by GPG exactly matches the independently published release-key fingerprint.

## Create the release

Create a signed annotated tag whose version exactly matches `CMakeLists.txt`:

```bash
version=$(sed -n 's/^project(limine-manager VERSION \([^ ]*\).*/\1/p' CMakeLists.txt)
git tag -s "v$version" -m "Release v$version"
git verify-tag "v$version"
git push origin "v$version"
```

The release workflow will:

- validate the tag and project versions;
- require complete GPG signing configuration;
- compile and run the test suite in Release mode;
- generate the source archive twice and compare both files;
- generate an SPDX JSON SBOM;
- generate and verify `SHA256SUMS`;
- create and verify detached ASCII-armored GPG signatures;
- generate a GitHub/Sigstore provenance attestation for the source archive;
- create the GitHub Release with generated release notes.

Only a push event for a valid semantic version tag can execute the attestation and publication steps. Manual workflow runs cannot publish a release.

## Verify downloaded assets

Use the repository verification script:

```bash
bash scripts/verify-release-assets /path/to/downloaded-assets
```

Checksums can also be verified directly:

```bash
sha256sum --check SHA256SUMS
```

GPG signatures:

```bash
gpg --verify limine-manager-VERSION.tar.gz.asc limine-manager-VERSION.tar.gz
gpg --verify limine-manager-VERSION.spdx.json.asc limine-manager-VERSION.spdx.json
gpg --verify SHA256SUMS.asc SHA256SUMS
```

GitHub provenance attestation:

```bash
gh attestation verify limine-manager-VERSION.tar.gz --repo joscarv/limine-manager
```
