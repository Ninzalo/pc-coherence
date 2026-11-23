# AGENTS.md — remote-windows-host

Concise agent rules for this tree. The full plan is
`docs/development-plan/RemoteWindows_Development_Plan_MacMini_Client_Supported.md`
(canonical copy). Product names: **Windows Host**, **macOS Client** — never
"Windows Agent" / "macOS Host".

## Ownership (single-writer)

| Path | Owner role |
|---|---|
| `protocol/`, `tools/export_protocol.sh` | CONTRACT |
| `core/`, `network/`, `app/` (shell), root `CMakeLists.txt` | H-FOUND |
| `core/catalog*`, `win32/catalog/` | H-CATALOG |
| `core/window_relationships*`, `win32/windows/` | H-WINDOWS |
| `win32/capture/`, `win32/video/` | H-MEDIA |
| `network/` (transport only) | H-NET |
| `core/input*` (mapping/key state), `win32/input/` | H-INPUT |
| `core/power*`, `win32/power/` | H-POWER |
| `core/pairing*`, `core/authorization*` | CONTRACT + H-POWER |

Do not edit another owner's paths opportunististically; open a
protocol/interface change request instead.

## Build & test commands (recorded, verified 2026-11-23)

Portable (macOS/Linux, C++20):
```
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```
Windows (full app, MSVC/Windows SDK): see README. Win32 targets are NOT
buildable on macOS — status must be NOT RUN, never PASS.

## Rules

1. Keep `docs/STATUS.md` current in the same change that changes behavior.
2. Never commit secrets (pairing keys, certs, tokens). Tests use synthetic
   credentials only.
3. Never report an unrun test as passed.
4. Protocol changes go through CONTRACT with a version bump + test vectors.
5. No arbitrary command execution anywhere in the protocol.
6. User-facing text goes through `resources/en-US.rc` string resources only.
7. Small, focused Conventional Commits; push each commit to origin.
8. C++20, no new external dependencies without a DEPENDENCIES.md entry
   (pinned version + license + purpose).
9. Structured logs only; never log window contents, typed text, or secrets.
10. Ports (product runtime): Host listens on TCP 47987 (signaling/control),
    bind LAN interface only. See docs/SECURITY.md.
