# Windows Host — STATUS

Last updated: 2026-11-23 (Stage 0 + protocol contract + core in progress).

## Implemented and verified

| Item | Evidence |
|---|---|
| Protocol contract v0.1.0 (schema, VERSION, 22 test vectors, export tool) | `tools/export_protocol.sh` runs, produces `SHA256SUMS`; vectors executed by `rwh_protocol_tests` (see last build below) |
| `docs/PROTOCOL.md` framing, validation, authorization, lifecycle rules | present, normative for both peers |
| ADRs 0001–0007 | `docs/decisions/` |

## Last successful build (this tree, portable core on macOS)

Commands:
```
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```
Result: *(filled after the first core commit — never pre-filled.)*

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

1. H-FOUND: core protocol model + validation + tests (in progress).
2. H-FOUND/H-NET: portable signaling server/client + tests.
3. H-WINDOWS/H-INPUT/H-POWER/H-CATALOG: core domain logic + tests.
4. CONTRACT: first contract bundle export + Client snapshot sync.
5. Unblock Windows runner (requires owner decision: install MSVC on the
   target PC or provision a second test machine).
