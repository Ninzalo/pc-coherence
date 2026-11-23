---
name: host-build-test
description: Build and test the Windows Host tree from the Mac mini dev host.
---

# Host build/test workflow

Portable core + network (macOS dev host, C++20):

```
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```

Expected: all `rwh_*` test suites run; exit code 0; ctest prints
`100% tests passed`.

Windows full app (MSVC + Windows SDK 11, on the Windows PC only):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Rules:
- Never report a Windows build as PASS from a macOS machine. If the runner
  is unavailable, record NOT RUN with the exact repro command.
- After any `protocol/` change, re-run both the Host core tests and the
  Client `swift test` (Client shares the golden vectors after sync).
- Do not delete `build/`; it is gitignored.
