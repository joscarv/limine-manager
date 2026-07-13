# Contributing

## Local verification

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run formatting before submitting changes:

```bash
cmake --build build --target format
cmake --build build --target format-check
```

For memory and undefined-behavior checks:

```bash
cmake -S . -B build-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLIMINE_MANAGER_ENABLE_ASAN=ON \
  -DLIMINE_MANAGER_ENABLE_UBSAN=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Do not run the test suite with `sudo`. Tests must remain safe for an unprivileged user.
