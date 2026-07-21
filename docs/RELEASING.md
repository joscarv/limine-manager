# Releasing limine-manager

Releases are created automatically by GitHub Actions when a semantic version tag is pushed.

The same workflow can also be executed manually in validation mode. A manual run builds and tests the project, verifies archive reproducibility, generates the SBOM and checksums, optionally signs the assets, and uploads them as a temporary workflow artifact. It never creates a GitHub Release.

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

## Validate the workflow without publishing

Before pushing a release tag, run the `Release` workflow manually from the GitHub Actions page and select the branch or commit that will be released.

A manual run:

- does not require a release tag;
- does not create a GitHub Release;
- does not create a provenance attestation;
- uploads the generated release files as the `release-validation-VERSION` artifact;
- retains that artifact for seven days.

Download the artifact and verify it locally:

```bash
bash scripts/verify-release-assets /path/to/release-validation-VERSION
```

The script always validates `SHA256SUMS`. When `.asc` files are present, it also verifies every detached GPG signature with the keys available in the local GPG keyring.

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
- generate and verify `SHA256SUMS`;
- optionally create and verify detached ASCII-armored GPG signatures;
- generate a GitHub/Sigstore provenance attestation for the source archive;
- create the GitHub Release with generated release notes.

Only a push event for a valid semantic version tag can execute the attestation and publication steps. Manual workflow runs cannot publish a release.

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

Use the repository verification script:

```bash
bash scripts/verify-release-assets /path/to/downloaded-assets
```

Checksums can also be verified directly:

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
