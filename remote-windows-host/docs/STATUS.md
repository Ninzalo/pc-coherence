# Windows Host — STATUS

Last updated: 2026-11-24 (RW-H-005: tray pairing-code display/rotation +
pairing session state in the tray menu, H-FOUND).

## Implemented and verified

| Item | Evidence |
|---|---|
| Protocol contract v0.1.0 (schema, VERSION, 22 test vectors, export tool) | `tools/export_protocol.sh` runs, produces **per-file** `SHA256SUMS` (fixed 2026-11-24: the old script hashed the printed file list, yielding one `-:` entry; now `sha256sum` over each file); export re-verified 25/25 |
| `core/` portable protocol model: types, JSON codec, semantic validation, version parsing, FPS gate, catalog, window relationships, input/key mapping, pairing, power policy, authorization | 104/104 CTest tests pass (commands below) |
| `network/` portable signaling: length-prefixed framing, TCP socket, signaling server/client (HELLO/pairing/heartbeat) | `signaling_e2e_test` loopback + concurrent clients + malformed-frame handling pass |
| `app/` session authorization + pairing state machine (RW-H-004, H-NET): per-connection `SessionAuth` (unpaired/pairing/paired/locked), shared `PairingHost` one-time-code store (single use), typed `not_paired` rejections for gated control kinds, post-pairing empty catalog, extended `--selftest` full handshake; tray pairing-code display/rotation (RW-H-005, H-FOUND) | `session_auth_test` (21 tests: 16 state-machine + 2 PairingHost rotation + 3 loopback e2e; the 3 RW-H-005 tests added 2026-11-24) + `--selftest` report (evidence below) |
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
Ninja 1.13.2, clang (Apple). Build clean (exit 0). **104/104 tests
PASSED** (0 failed). Suites:
sha256, protocol_version, protocol_json (incl. 22 canonical test vectors),
validation, fps_policy, catalog, window_relationships, input, pairing,
power_policy, authorization, framing, signaling_e2e, session_auth
(21 tests: 16 state-machine + 2 PairingHost rotation + 3 loopback e2e
over the real SignalingServer/Client; 3 new in RW-H-005 covering
rotation-after-consume and the tray rotate path).

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
| Components | `app/main.cpp` (args: --port/--addr/--selftest), `AppController` (logging + server lifecycle + session handler + tray wiring), `TrayController` (tray icon + Status / Show pairing code... / Exit menu, message-only window; RW-H-005), `SessionAuth` (RW-H-004), `Logger` (structured line log → %LOCALAPPDATA%\RemoteWindows\host.log, stdout mirror), `Win32Env.hpp` (wide-only Win32 environment), `resources/en-US.rc` (English strings only, IDs 101–108) |
| Known limitations | Console-subsystem binary (console window visible in GUI mode; windowed packaging later); placeholder tray icon (IDI_APPLICATION); GUI-mode tray behavior (incl. the pairing-code dialog) verified by build + code review only (no interactive session test from the runner) — GUI runtime = NOT RUN |

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

Windows runner (DESKTOP-PC):

| Item | Evidence (2026-11-24, SHA `870aec1`) |
|---|---|
| Runner health | `windows-runner.sh status` → `RWH status=ok`, `RWH host=DESKTOP-PC`, `RWH cmake=cmake version 4.5.1`, `RWH ninja=1.13.2`, `RWH git=git version 2.54.0.windows.1` (exit 0) |
| `prepare` | `RWH status=prepared`, `RWH sha=870aec1dae23da5bf18869eee05120c860c925c7` (fetch + checkout of `origin/main` verified) |
| `windows-runner.sh unit-tests <sha>` | `RWH sha=870aec1...`, **`100% tests passed out of 101`** (MSVC 14.51.36231 / SDK 10.0.26100, RelWithDebInfo, Ninja), `RWH ctest_exit=0`, `RWH status=tests_ok` |
| `windows-runner.sh selftest <sha>` | `rwh-host.exe --selftest` → `{"selftest":true,"passed":true,"stage":"","port":47987,"reply_kind":"app_catalog","envelopes":3,"pairing":"accepted","reply_kinds":"hello;pairing_challenge;pairing_result;app_catalog","error":""}`, `RWH selftest_exit=0`, `RWH status=selftest_ok` |
| MSVC link fix found by the runner | `rwh-host` was missing `app/SessionAuth.cpp` in its source list (LNK2019 for `SignalingSessionHandler` ctor + `PairingHost::currentCode`); one-line CMake fix `870aec1`; macOS 101/101 unaffected (rwh-host is Win32-only) |

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

## Tray pairing-code UI (H-FOUND, RW-H-005)

Behavior (plan FR-13 — pairing confirmed via "a one-time code ... shown
on the Host"):

- Tray menu gains **"Show pairing code..."**: shows the current active
  one-time code in a MessageBox; if the code was consumed (or past its
  300 s TTL) it starts a fresh epoch (`PairingHost::beginEpoch` →
  `core::PairingManager::rotateCode`) and shows that instead. The
  Status dialog gains the pairing session count
  (`SignalingSessionHandler::sessionCount()`) alongside port and
  connection count.
- Wiring: `TrayController` takes an optional
  `std::function<std::string()>` code provider; `AppController::runGui`
  wires it to the new `SignalingSessionHandler::rotateOrCurrentCode()`
  (one small public method). `PairingHost` gains
  `hasActiveCode(now_ms)` (consumed OR expired → false; same strict-`>`
  TTL comparison as `core::PairingManager::verifyCode`).
- The code is displayed via MessageBox ONLY (the legitimate FR-13 UI
  channel). It is never logged and never written to a file (AGENTS
  rule 9). All new user-facing strings are en-US.rc resources
  (IDS_MENU_PAIRING_CODE / IDS_PAIRING_CODE_TITLE /
  IDS_PAIRING_CODE_FORMAT, IDs 106–108, body uses the %s code
  placeholder); the Status MessageBox title now comes from
  IDS_APP_TITLE instead of a hardcoded literal.
- Rotation semantics unchanged: attempt counters / 60 s lockout remain
  in `core::PairingManager` (PROTOCOL §7.4); a fresh code pairs
  normally after consume() (unit tests below).

macOS verification (branch `agent/H-FOUND/RW-H005`, 2026-11-24):

```
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```

Result: **104/104 tests PASSED** (previous 101 + 3 new, 0 failed). New
tests: `PairingHost.RotationAfterConsumeYieldsFreshValidCode`
(consume → `hasCode()==false` → rotate → fresh 6-digit code pairs; the
consumed code returns `invalid_code`), `PairingHost.ExpiredCodeIsNotActiveAndRotates`
(TTL), `SessionAuthE2E.RotateOrCurrentCodeAfterConsumePairsNewClient`
(real channel: stable code while active → pair → consume → rotated code
pairs a second client; `sessionCount()` observed).

Windows runner (DESKTOP-PC), 2026-11-24:

| Item | Evidence |
|---|---|
| Runner health | `windows-runner.sh status` → `RWH status=ok`, `RWH host=DESKTOP-PC`, `RWH cmake=cmake version 4.5.1`, `RWH ninja=1.13.2`, `RWH git=git version 2.54.0.windows.1` (exit 0) |
| `prepare` / `unit-tests` / `selftest` at this SHA | **NOT RUN** — the branch is intentionally not pushed (task constraint), and `windows_runner.ps1 prepare` fetches only from `origin` (github.com/Ninzalo/pc-coherence.git), so an unpushed SHA cannot be checked out. The `rwh-host` Win32 target (TrayController + en-US.rc changes) does not compile on macOS, so the runner is the only compile check for this delta; it will run at this SHA once the orchestrator allows a push (or via `git bundle` if the allow-list ever permits it). |

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

1. LAN end-to-end pairing test with the macOS Client — Host side is
   READY (RW-H-005: the tray shows/rotates the one-time code; the full
   handshake is verified at unit + loopback e2e level). Requires the
   C-CONNECTION Client counterpart (item 2) and an interactive Host
   session (GUI tray runtime is NOT RUN from the runner).
2. C-CONNECTION: macOS Client connection/pairing counterpart (pairs
   against the Host handshake verified in RW-H-004); per orchestrator,
   H-MEDIA (capture/video) is the parallel Host track.

Done:

- Windows runner (Section 18.4) — DONE 2026-11-24 (evidence above).
- CONTRACT: first contract bundle export + Client snapshot sync —
  DONE 2026-11-24 (client `docs/STATUS.md`).
- H-FOUND: `app/` shell — DONE 2026-11-24 at `d3c780e` (evidence
  above, selftest_ok + 83/83).
- H-NET: pairing state machine + session authorization over the
  signaling channel (RW-H-004) — DONE 2026-11-24 at `870aec1`
  (101/101 macOS + Windows runner unit-tests/selftest, evidence above).
- H-FOUND: tray pairing-code display/rotation + pairing session state
  (RW-H-005) — DONE 2026-11-24 (evidence above; 104/104 macOS ctest.
  Win32 runner verification at this SHA is NOT RUN: branch
  intentionally not pushed and the runner's `prepare` fetches only
  from origin — to re-run once a push is authorized).
