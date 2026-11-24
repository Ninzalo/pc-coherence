# remote-windows-host — Windows Host

The **Windows Host** runs in the interactive user session of a Windows 11
Pro PC. It discovers and launches applications, tracks main and auxiliary
windows, captures and encodes window video, receives input, and exposes
typed, authorized sleep/shutdown actions. The macOS peer is the
**macOS Client** (sibling tree `remote-windows-client/`).

This tree is the **canonical owner of the protocol contract**
(`protocol/`). See `docs/PROTOCOL.md` and `docs/ARCHITECTURE.md`.

> Layout note: the plan calls for two Git repositories. Per explicit owner
> instruction this project lives in one repository
> (`Ninzalo/pc-coherence`) containing both product trees as independent
> subtrees — see `docs/decisions/ADR-0008-single-repo-layout.md`. All
> other plan rules apply unchanged.

## Layout

```
protocol/    canonical .proto schema, VERSION, test vectors   (CONTRACT role)
core/        platform-independent C++20 logic + unit tests    (builds everywhere)
network/     portable LAN signaling (TCP framing, server, client)
win32/       Windows-only adapters: capture, windows, input, power, catalog, tray
app/         Windows app shell (main.cpp, AppController, TrayController)
tools/       export_protocol.sh
vendor/      pinned build dependencies (googletest v1.15.2, nlohmann/json v3.11.3)
docs/        STATUS, ARCHITECTURE, DEPENDENCIES, PROTOCOL, SECURITY, TEST_PLAN,
             PERFORMANCE, decisions/
```

## Build and test

Windows (MSVC + Windows SDK, the only environment for the full app):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Portable core + network (any C++20 toolchain, e.g. macOS — used for
platform-independent unit tests):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Win32 app targets do NOT compile on macOS. On non-Windows machines those
targets are simply not configured; report them as NOT RUN, never as PASS.

## Protocol

- Schema: `protocol/remote_windows.proto`, version in `protocol/VERSION`.
- Export a checksum-verified bundle: `tools/export_protocol.sh [out-dir]`.
- Test vectors in `protocol/test-vectors/` are executed by both product
  trees (Host: `rwh_protocol_tests`; Client: `RemoteWindowsProtocolTests`).
