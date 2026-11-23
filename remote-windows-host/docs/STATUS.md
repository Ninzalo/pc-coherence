# Windows Host — STATUS

Last updated: 2026-11-23 (Stage 0 complete; portable core implemented and green).

## Implemented and verified

| Item | Evidence |
|---|---|
| Protocol contract v0.1.0 (schema, VERSION, 22 test vectors, export tool) | `tools/export_protocol.sh` runs, produces `SHA256SUMS`; vectors executed by `rwh_protocol_tests` (see last build below) |
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
Result: 2026-11-23, macOS 27.2.1, CMake 4.5.1, Ninja 1.13.2, clang (Apple).
Build clean (exit 0). **83/83 tests PASSED** (0 failed). Suites:
sha256, protocol_version, protocol_json (incl. 22 canonical test vectors),
validation, fps_policy, catalog, window_relationships, input, pairing,
power_policy, authorization, framing, signaling_e2e.

## Implemented, NOT verified on Windows (runner unavailable)

- Win32 adapters (`win32/`): capture (WGC), window registry/events,
  input (SendInput), power (SetSuspendState / InitiateSystemShutdownExW),
  catalog scan (IShellLinkW), tray app. **NOT RUN** — no MSVC/Windows SDK
  build runner is reachable from this Mac yet. Repro steps: see README
  "Build and test" (Windows block).

## Blocked

| Task | Blocker |
|---|---|
| Windows native build + unit tests on Windows 11 Pro | No build runner configured (Section 18.4 not satisfied: toolchain + runner script + exact-SHA smoke test missing) |
| Stage 1 one-window video end-to-end | WebRTC pinned build not integrated (see DEPENDENCIES.md); no Windows runner |
| Real pairing/LAN integration test with the macOS Client | Needs both apps built (Windows side blocked) |

## Next tasks

1. CONTRACT: first contract bundle export + Client snapshot sync
   (`remote-windows-client` not yet created — next tree to scaffold).
2. H-FOUND: `app/` shell (main + AppController + logging scaffolding) on top
   of the core library.
3. Unblock Windows runner (requires owner decision: install MSVC on the
   target PC or provision a second test machine); then verify this same
   tree builds under MSVC/Windows SDK.
