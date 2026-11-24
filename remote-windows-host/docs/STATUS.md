# Windows Host — STATUS

Last updated: 2026-11-24 (LAN end-to-end pairing with the macOS Client
VERIFIED — see "LAN end-to-end pairing (verified)" below).

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
- `WM_NCCREATE` return contract (MS "Using Window Procedures", verified
  2026-11-24): a window procedure that returns `FALSE` (0) for
  `WM_NCCREATE` makes `CreateWindowExW` fail with a **NULL handle and
  `GetLastError() == 0`** (no error code — the creation was declined,
  not rejected). The tray's `wndProc` originally did
  `case WM_NCCREATE: SetWindowLongPtrW(...); return 0;` → the Host
  exited ~1 s after start with no tray icon, in every session. Fixed
  in `c4f9250`: return `DefWindowProcW`'s result. Discovered via the
  `--probe-tray` in-process bisect (below) — 13 window-creation probe
  cases isolated the return value as the single differentiator on this
  toolchain (MSVC 14.51 / SDK 10.0.26100, Windows 11).

## Tray window-creation failure — diagnosed and fixed (2026-11-24)

Symptom: Host started, bound the server, then exited in ~1 s with no
tray icon (`tray.failed code=2 detail="CreateWindowExW failed: 0"`), in
both the user's interactive desktop session and non-interactive SSH
sessions.

Diagnosis (no user round-trips after the first log read):

1. Instrumented `TrayController` (detailed error codes, `lastError()`,
   `tray.failed`/`tray.exit` log events) + hidden-top-level-window
   fallback — both window types failed with `GetLastError()==0`.
2. Standalone probe binary (runner `probe-tray` action,
   `tools/probe/probe-tray.cpp`): all 8 window-creation variants
   **succeeded** in the same SSH session — including with a live
   socket accept thread. The failure was process-specific, not
   session-specific.
3. In-process bisect: `rwh-host.exe --probe-tray` (diagnostic mode,
   `AppController::runProbeTray`) walks the real GUI-init sequence
   (logger → session handler → signaling server → console-control
   handler) and creates probe windows at each step, then replicates
   the exact tray window setup with 9 single-variable variants
   (class name / wndProc / lpCreateParams / title / USERDATA reads /
   WM_NCCREATE return value). Result: every wndProc that handled
   `WM_NCCREATE` and `return 0;` failed (NULL + lasterror 0); every
   wndProc returning `DefWindowProcW`'s result succeeded — including
   with `SetWindowLongPtrW(GWLP_USERDATA)` done inside WM_NCCREATE
   (the candidate-fix pattern that was then applied).

Fix (commit `c4f9250`): `TrayController::wndProc` returns
`DefWindowProcW`'s result for `WM_NCCREATE` (the documented
contract). Related hardening in the same work stream:

- **Degraded mode** (`d8444f8`): if `Shell_NotifyIconW(NIM_ADD)`
  fails (no notification area — e.g. a non-interactive session has
  none; observed error `2147500037`), the Host **keeps running and
  serving** instead of exiting; `TrayController::iconOk()` reports
  the state and the app layer logs `tray.icon_failed` (immediate,
  via the icon-state callback added in `36fe61d`) / `tray.icon`.
- **Per-line log flush** (`2243b1c`): `Logger::write` flushes the log
  file after each line — a daemon log must not hold its last lines in
  stdio's full buffer (observed: a live GUI process held its
  `host.start`/tray lines for 10+ s).
- Console-subsystem GUI mode ignores CTRL_CLOSE/Ctrl+C
  (`SetConsoleCtrlHandler` swallow-all) — closing the console window
  no longer stops the Host.

Verification (all 2026-11-24):

| Item | Evidence |
|---|---|
| macOS (clang) | 104/104 CTest at `36fe61d` |
| Windows runner (MSVC) | `unit-tests` **104/104** + `selftest` `pairing=accepted` at `c4f9250` and `36fe61d` (`RWH status=tests_ok` / `selftest_ok`) |
| GUI in non-interactive session | `rwh-host.exe --port 47999` launched over SSH: process **alive after 10 s**, `LISTENING 0.0.0.0:47999`, log shows `host.start` + `tray.icon_failed` (expected in a session without a tray) — degraded mode working |
| GUI in the user's interactive session | **VERIFIED** 2026-11-24: user double-clicked the binary; log shows `tray.icon` (success) and the pairing handshake (below) |

## LAN end-to-end pairing (verified) — 2026-11-24

macOS Client (192.168.100.156) ↔ Windows Host (192.168.100.139:47987),
the first real LAN pairing of the project:

| Item | Evidence (Host log, 2026-11-24) |
|---|---|
| Tray icon in the interactive session | `13:47:08Z event="host.start" addr="0.0.0.0" port="47987"` + `event="tray.icon"` |
| HELLO from the Client | `13:47:26Z event="session.hello" peer="192.168.100.156:50205" kind="hello" state="pairing"` |
| Pairing accepted (one-time code from the tray dialog, FR-13) | `13:47:38Z event="session.pairing" peer="192.168.100.156:50205" kind="pairing_challenge" state="paired" accepted="true"` |
| Catalog request after pairing | `13:47:38Z event="session.message" kind="app_catalog_request" state="paired"` → answered with an empty catalog (catalog scanner not implemented yet — honest empty, no fake entries) |
| Liveness | `session.message kind="heartbeat" state="paired"` every 5 s, continuous 4+ minutes |
| Client-side confirmation (user-confirmed UI state) | `Connection: Paired`, `Host: DESKTOP-PC`, `No applications available yet` |

A first pairing run earlier the same day also succeeded
(`13:41:57Z session.pairing ... accepted="true"`).

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
  normally after consume() (unit tests below). Note: `rotateCode` is
  deterministic in the timestamp — a re-rotation within the same
  steady-clock millisecond can regenerate the same 6 digits, which is
  still a valid active code (the tray always displays the current
  active code; the e2e test therefore does not assert byte-difference,
  the PairingHost unit test does, with an injectable clock).

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
| `prepare` at `2236750` | `RWH status=prepared`, `RWH sha=2236750c4746aa1634342668ca9664fffaec7d1d` (exit 0) — the Win32 `rwh-host` delta (TrayController + en-US.rc) compiles on MSVC |
| `unit-tests` at `2236750` | `100% tests passed out of 104`, `RWH ctest_exit=0`, `RWH status=tests_ok` (exit 0; incl. the 3 new RW-H-005 rotation tests) |
| `selftest` at `2236750` | `{"selftest":true,"passed":true,"port":47987,"reply_kind":"app_catalog","envelopes":3,"pairing":"accepted","reply_kinds":"hello;pairing_challenge;pairing_result;app_catalog"}`, `RWH selftest_exit=0`, `RWH status=selftest_ok` (exit 0) |

## Implemented, NOT verified on Windows

- Win32 adapters (`win32/`): capture (WGC), window registry/events,
  input (SendInput), power (SetSuspendState / InitiateSystemShutdownExW),
  catalog scan (IShellLinkW). **NOT RUN** — not implemented yet (next
  feature tracks: H-CATALOG, then H-MEDIA/H-WINDOWS/H-INPUT/H-POWER).

## Blocked

| Task | Blocker |
|---|---|
| Stage 1 one-window video end-to-end | WebRTC pinned build not integrated (see DEPENDENCIES.md) |

## Next tasks

1. H-CATALOG: Start Menu scan + `LAUNCH_APP` by allow-listed `app_id`
   (IShellLinkW / ShellExecuteExW) — the next Host feature track; the
   Client already sends `app_catalog_request` on pairing and renders
   "No applications available yet" while the catalog is empty. Per
   orchestrator, H-MEDIA (capture/video) is the parallel Host track.

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
  (RW-H-005) — DONE 2026-11-24 at `2236750` (104/104 macOS ctest;
  Windows runner prepare/unit-tests/selftest all OK at `2236750`,
  evidence in the RW-H-005 runner table above).
- H-FOUND: tray no-icon failure root-caused and fixed (WM_NCCREATE
  return contract) + degraded mode + log flush — DONE 2026-11-24 at
  `c4f9250`/`d8444f8`/`2243b1c`/`36fe61d` (evidence in the
  "Tray window-creation failure" section above).
- H-NET / C-CONNECTION: LAN end-to-end pairing with the macOS Client —
  DONE 2026-11-24 (evidence in the "LAN end-to-end pairing (verified)"
  section above; Client `docs/STATUS.md` mirrors it).
