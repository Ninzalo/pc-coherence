# Windows Host — STATUS

Last updated: 2026-11-24 (RW-H-004: session authorization + pairing
handshake over the signaling channel, H-NET).

## Implemented and verified

| Item | Evidence |
|---|---|
| Protocol contract v0.1.0 (schema, VERSION, 22 test vectors, export tool) | `tools/export_protocol.sh` runs, produces **per-file** `SHA256SUMS` (fixed 2026-11-24: the old script hashed the printed file list, yielding one `-:` entry; now `sha256sum` over each file); export re-verified 25/25 |
| `core/` portable protocol model: types, JSON codec, semantic validation, version parsing, FPS gate, catalog, window relationships, input/key mapping, pairing, power policy, authorization | 101/101 CTest tests pass (commands below) |
| `network/` portable signaling: length-prefixed framing, TCP socket, signaling server/client (HELLO/pairing/heartbeat) | `signaling_e2e_test` loopback + concurrent clients + malformed-frame handling pass |
| `app/` session authorization + pairing state machine (RW-H-004, H-NET): per-connection `SessionAuth` (unpaired/pairing/paired/locked), shared `PairingHost` one-time-code store (single use), typed `not_paired` rejections for gated control kinds, post-pairing empty catalog, extended `--selftest` full handshake | `session_auth_test` (18 tests incl. 2 loopback e2e) + `--selftest` report (evidence in the RW-H-004 section below) |
| `docs/PROTOCOL.md` framing, validation, authorization, lifecycle rules | present, normative for both peers; codec now enforces `window_id` UUID format and `title` ≤ 512 chars at decode time (`bad_uuid` / `title_too_long`) |
| ADRs 0001–0007 | `docs/decisions/` |

## Last successful build (this tree, portable core on macOS)

Commands:
```
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```
Result: 2026-11-24, macOS 27.2.1, CMake 4.5.1,
Ninja 1.13.2, clang (Apple). Build clean (exit 0). **101/101 tests
PASSED** (0 failed). Suites:
sha256, protocol_version, protocol_json (incl. 22 canonical test vectors),
validation, fps_policy, catalog, window_relationships, input, pairing,
power_policy, authorization, framing, signaling_e2e, session_auth (new,
RW-H-004: 16 state-machine tests + 2 loopback e2e over the real
SignalingServer/Client).

## Windows runner (Section 18.4) — verified

| Item | Evidence (2026-11-24) |
|---|---|
| Toolchain on DESKTOP-PC | VS Build Tools 2026 18.8.2, MSVC 14.51.36231, Windows SDK 10.0.26100.0, git 2.54.0 (preinstalled 2026-07-29); CMake 4.5.1 + Ninja 1.13.2 installed by the agent (user-level, no admin rights) |
| Runner | `tools/windows_runner.ps1` (PC: `C:\rwh\rwh-runner.ps1`, SHA256-verified at install) + `tools/windows-runner.sh` Mac wrapper; allow-listed actions only; machine-readable `RWH key=value` report |
| Exact-SHA build + unit tests | `windows-runner.sh unit-tests` @ `9450a49f8c58acb8c93680939e8b7f2dcb95d6c3`: `RWH sha=9450a49...`, `100% tests passed out of 83`, `RWH ctest_exit=0`, `RWH status=tests_ok` (RelWithDebInfo, Ninja, MSVC) |
| Portability fix found by the runner | POSIX `ssize_t` in the TCP layer is not declared by MSVC in C++ mode → `std::int64_t` (commit `978dc55`); clang still 83/83 |

## Windows Host app shell (H-FOUND) — verified

| Item | Evidence (2026-11-24, SHA `d3c780e`) |
|---|---|
| Build | `rwh-host` (Win32 console app + en-US.rc) compiles and links under MSVC 14.51 / SDK 10.0.26100 via runner `selftest` action |
| Runtime smoke | `rwh-host.exe --selftest`: real SignalingServer on 127.0.0.1:47987 + in-process macos-client HELLO exchange → `{"selftest":true,"passed":true,"port":47987,"reply_kind":"hello","envelopes":1}`, `RWH status=selftest_ok` |
| Unit tests (same SHA) | `100% tests passed out of 83`, `RWH status=tests_ok` |
| Components | `app/main.cpp` (args: --port/--addr/--selftest), `AppController` (logging + server lifecycle + HELLO answer + pairing_challenge), `TrayController` (tray icon + Status/Exit menu, message-only window), `Logger` (structured line log → %LOCALAPPDATA%\RemoteWindows\host.log, stdout mirror), `Win32Env.hpp` (wide-only Win32 environment), `resources/en-US.rc` (English strings only) |
| Known limitations | Console-subsystem binary (console window visible in GUI mode; windowed packaging later); placeholder tray icon (IDI_APPLICATION); no Host UI displays the one-time code yet (tray/UI task — the code exists in memory and is consumed on first pairing); GUI-mode tray behavior verified by build + code review only (no interactive session test from the runner) — GUI runtime = NOT RUN |

## Session authorization + pairing handshake (H-NET, RW-H-004)

Behavior (plan FR-13, FR-02/FR-05 gating, PROTOCOL §6/§7):

- Per-connection `SessionAuth` state machine in `app/` (portable,
  injectable clock): `unpaired → pairing → paired`, `locked` on the
  3-failure/60 s lockout (PROTOCOL §7.4). HELLO → host HELLO +
  PAIRING_CHALLENGE (fresh 32-hex nonce, no code — canonical vector
  shape). Client PAIRING_CHALLENGE (code + nonce echo) is accepted only
  in `pairing`; verified via `core::PairingManager` (attempt limit,
  lockout, 300 s code TTL). Success → paired + session key derived via
  `PairingManager::deriveSessionKey(code, host_name)` (in memory, for
  later MAC use) + code consumed (single use) + PAIRING_RESULT
  (accepted) per the canonical vector. Failure → typed PAIRING_RESULT
  (`invalid_code` / `locked` / `expired` / `invalid_state`), session
  stays unpaired.
- Gating: while unpaired, `launch_app`, `resize_request`,
  `window_action`, `frame_rate_request`, `system_power_request` are
  rejected with the typed result kind and `error_code="not_paired"`.
  `input_event` and `app_catalog_request` are dropped (no typed result
  envelope exists for them in schema v0.1.0 — see
  "Known limitations / protocol gaps" below). `heartbeat` works in every
  state. After pairing: `app_catalog_request` → APP_CATALOG with an
  EMPTY entries list (honest placeholder; catalog scanner is a later
  task); other control kinds answer `not_implemented`.
- Structured logs only (`session.hello`, `session.pairing`,
  `session.gated`, `session.disconnect`): peer, kind, state, stable
  error codes. Codes, nonces, and session keys are never logged.
- `--selftest` now performs the full handshake in-process: HELLO →
  HELLO → PAIRING_CHALLENGE → PAIRING_RESULT(accepted) →
  APP_CATALOG_REQUEST → APP_CATALOG(empty). Report keys added:
  `pairing` (`accepted`/`rejected`/`none`) and `reply_kinds`
  (`;`-joined reply kinds); existing keys unchanged. Negative paths are
  unit tests, not selftest.

macOS verification (this commit, 2026-11-24):

```
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```

Result: **101/101 tests PASSED** (previous 83 + 18 new `session_auth`
tests, 0 failed). The `rwh-host` target (and therefore `--selftest`)
does not compile on macOS — Windows-only, verified via the runner below.

Windows runner (DESKTOP-PC, exact SHA — see the follow-up commit for the
verified results at this SHA):

| Item | Evidence |
|---|---|
| `windows-runner.sh unit-tests <sha>` | NOT RUN at the time of this commit (verification in progress; results recorded in the follow-up commit) |
| `windows-runner.sh selftest <sha>` | NOT RUN at the time of this commit (same) |

Protocol gaps found (CONTRACT follow-up requested, see report):
`input_event` has no result kind and `app_catalog` carries no
`error_code` field in schema v0.1.0, so unpaired requests of those two
kinds cannot be answered with a typed error envelope; the Host drops
them (logged as `session.gated`). Also: PROTOCOL §7.4 documents the
session key as `hex(sha256("rwpair1|" + code + "|" + nonce))` while
`core::PairingManager::deriveSessionKey` uses `"rwh-pairing-v1|" + code
+ "|" + host_name` — the core (executable) definition is what the Host
uses; the doc should be aligned.

### SDK 10.0.26100 discrepancies (plan rule 2 — recorded 2026-11-24)

- `SHGetAllocatedString` (shlobj.h) is **removed** in this SDK → logger uses
  the `LOCALAPPDATA` environment variable instead (COM-free;
  `GetKnownFolderPath` would require COM initialization).
- `DOUBLE_CLICK` is a wincontypes.h macro (`0x0002`) and collides with the
  portable core enum member → `app/Win32Env.hpp` undefines it (plus a
  guarded list of other colliding SDK macros).
- The MessageBox icon constant is `MB_ICONINFORMATION` (no underscore) in
  this SDK — `MB_ICON_INFORMATION` does not exist.

## Implemented, NOT verified on Windows

- Win32 adapters (`win32/`): capture (WGC), window registry/events,
  input (SendInput), power (SetSuspendState / InitiateSystemShutdownExW),
  catalog scan (IShellLinkW). **NOT RUN** — not implemented yet (next
  stages after H-NET pairing/session work).

## Blocked

| Task | Blocker |
|---|---|
| Stage 1 one-window video end-to-end | WebRTC pinned build not integrated (see DEPENDENCIES.md) |
| Real pairing/LAN integration test with the macOS Client | Host side DONE (RW-H-004); Client connection/pairing counterpart (C-CONNECTION) not implemented |

## Next tasks

1. ~~Windows runner (Section 18.4)~~ — DONE 2026-11-24 (evidence above).
2. ~~CONTRACT: first contract bundle export + Client snapshot sync~~ —
   DONE 2026-11-24 (client `docs/STATUS.md`).
3. ~~H-FOUND: `app/` shell~~ — DONE 2026-11-24 at `d3c780e` (evidence
   above, selftest_ok + 83/83).
4. ~~H-NET: pairing state machine + session authorization over the
   signaling channel (RW-H-004)~~ — DONE 2026-11-24 (evidence above;
   101/101 macOS + runner verification in this section's follow-up
   commit).
5. C-CONNECTION: macOS Client connection/pairing counterpart (pairs
   against the Host handshake verified in RW-H-004); per orchestrator,
   H-MEDIA (capture/video) is the parallel Host track.
