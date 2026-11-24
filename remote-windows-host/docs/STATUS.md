# Windows Host — STATUS

Last updated: 2026-11-24 (portable core re-verified; export checksum fix;
Windows toolchain verified on the target PC).

## Implemented and verified

| Item | Evidence |
|---|---|
| Protocol contract v0.1.0 (schema, VERSION, 22 test vectors, export tool) | `tools/export_protocol.sh` runs, produces **per-file** `SHA256SUMS` (fixed 2026-11-24: the old script hashed the printed file list, yielding one `-:` entry; now `sha256sum` over each file); export re-verified 25/25 |
| `core/` portable protocol model: types, JSON codec, semantic validation, version parsing, FPS gate, catalog, window relationships, input/key mapping, pairing, power policy, authorization | 83/83 CTest tests pass (commands below) |
| `network/` portable signaling: length-prefixed framing, TCP socket, signaling server/client (HELLO/pairing/heartbeat) | `signaling_e2e_test` loopback + concurrent clients + malformed-frame handling pass |
| `docs/PROTOCOL.md` framing, validation, authorization, lifecycle rules | present, normative for both peers; codec now enforces `window_id` UUID format and `title` ≤ 512 chars at decode time (`bad_uuid` / `title_too_long`) |
| ADRs 0001–0007 | `docs/decisions/` |

## Last successful build (this tree, portable core on macOS)

Commands:
```
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```
Result: 2026-11-24 (re-run; 2026-11-23 original), macOS 27.2.1, CMake 4.5.1,
Ninja 1.13.2, clang (Apple). Build clean (exit 0). **83/83 tests PASSED**
(0 failed). Suites:
sha256, protocol_version, protocol_json (incl. 22 canonical test vectors),
validation, fps_policy, catalog, window_relationships, input, pairing,
power_policy, authorization, framing, signaling_e2e.

## Windows runner (Section 18.4) — verified

| Item | Evidence (2026-11-24) |
|---|---|
| Toolchain on DESKTOP-PC | VS Build Tools 2026 18.8.2, MSVC 14.51.36231, Windows SDK 10.0.26100.0, git 2.54.0 (preinstalled 2026-07-29); CMake 4.5.1 + Ninja 1.13.2 installed by the agent (user-level, no admin rights) |
| Runner | `tools/windows_runner.ps1` (PC: `C:\rwh\rwh-runner.ps1`, SHA256-verified at install) + `tools/windows-runner.sh` Mac wrapper; allow-listed actions only; machine-readable `RWH key=value` report |
| Exact-SHA build + unit tests | `windows-runner.sh unit-tests` @ `9450a49f8c58acb8c93680939e8b7f2dcb95d6c3`: `RWH sha=9450a49...`, `100% tests passed out of 83`, `RWH ctest_exit=0`, `RWH status=tests_ok` (RelWithDebInfo, Ninja, MSVC) |
| Portability fix found by the runner | POSIX `ssize_t` in the TCP layer is not declared by MSVC in C++ mode → `std::int64_t` (commit `978dc55`); clang still 83/83 |

## Implemented, NOT verified on Windows

- Win32 adapters (`win32/`): capture (WGC), window registry/events,
  input (SendInput), power (SetSuspendState / InitiateSystemShutdownExW),
  catalog scan (IShellLinkW). **NOT RUN** — not implemented yet (next
  stages). The Win32 app shell is in progress (H-FOUND).

## Blocked

| Task | Blocker |
|---|---|
| Stage 1 one-window video end-to-end | WebRTC pinned build not integrated (see DEPENDENCIES.md) |
| Real pairing/LAN integration test with the macOS Client | Needs the Host app shell + Client connection client |

## Next tasks

1. ~~Windows runner (Section 18.4)~~ — DONE 2026-11-24 (evidence above).
2. ~~CONTRACT: first contract bundle export + Client snapshot sync~~ —
   DONE 2026-11-24 (client `docs/STATUS.md`).
3. H-FOUND: `app/` shell (Win32 main + AppController + TrayController +
   structured logging + en-US.rc) with a `--selftest` mode, built and
   smoke-tested on the Windows runner.
