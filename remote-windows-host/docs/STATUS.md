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

## Implemented, NOT verified on Windows (runner being configured)

- Win32 adapters (`win32/`): capture (WGC), window registry/events,
  input (SendInput), power (SetSuspendState / InitiateSystemShutdownExW),
  catalog scan (IShellLinkW), tray app. **NOT RUN** — the Windows toolchain
  on the target PC is now verified (VS Build Tools 2026 18.8.2, MSVC
  14.51.36231, Windows SDK 10.0.26100.0, git 2.54 — installed 2026-07-29;
  see root `docs/agent-runtime/TOPOLOGY_PREFLIGHT.md`), CMake/Ninja
  installation on the PC and the exact-SHA build smoke test (Section 18.4)
  are in progress. Repro steps: see README "Build and test" (Windows block).

## Blocked

| Task | Blocker |
|---|---|
| Windows native build + unit tests on Windows 11 Pro | Toolchain verified present; CMake/Ninja install on the PC + runner exact-SHA smoke test pending (Section 18.4) |
| Stage 1 one-window video end-to-end | WebRTC pinned build not integrated (see DEPENDENCIES.md) |
| Real pairing/LAN integration test with the macOS Client | Needs both apps built (Windows side in progress) |

## Next tasks

1. ~~CONTRACT: first contract bundle export + Client snapshot sync~~ —
   DONE 2026-11-24: client tree scaffolded; `Protocol/Contract/` v0.1.0
   synced via the approved tool and verified 25/25 against SHA256SUMS
   (client `docs/STATUS.md`).
2. Windows runner (Section 18.4): install CMake/Ninja on the PC, exact-SHA
   build + ctest of this Host tree under MSVC.
3. H-FOUND: `app/` shell (main + AppController + logging scaffolding) on top
   of the core library.
