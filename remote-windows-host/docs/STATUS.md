# Windows Host — STATUS

Last updated: 2026-11-27 (RW-H-010 Stage 1: portable window-registry core
implemented — live window registry with UUID window_ids, owner/parent
relationship graph, cross-PID auxiliary correlation (ADR-0006), modality
inference, snapshot reconciliation; **177/177 macOS CTest** (145 prior +
32 new), macOS-verifiable; the Win32 WinEvents adapter + hermetic
integration test are Stage 2 (Windows-runner-verified, pending). RW-H-009
Win32 catalog remains the last fully-verified Windows item: runner
**157/157 unit tests + selftest_ok @ `7bb5687`** after four runner
feedback rounds — MSVC build fixes, a 3.5 h session-0 launch hang triaged
and root-caused, ctest per-test timeout + 25-min watchdog added, and
launch/scan correctness fixes (pre-resolve .lnk targets, no
`SLGP_FILESYSTEM` in the Start Menu scan, ms-appx URI fixtures that do not
persist on 24H2). macOS 145/145 at that SHA. RW-H-008: 143/143 macOS +
Windows runner 146/146 @ `d342bb3`; see "Implemented and verified" below.
Client C-MEDIA (RW-C-003) media channel merged into main at `c5320e3` on
2026-11-25 (client `docs/STATUS.md`); the host tree was unchanged by that
merge.)

## Implemented and verified

| Item | Evidence |
|---|---|
| Window registry core (RW-H-010 Stage 1, H-WINDOWS, plan FR-03): `core/window_registry.{hpp,cpp}` — live registry of top-level windows; durable `window_id` = UUID v4 assigned once at first sight (HWND never the remote identity, ADR-0006); full `WindowRecord` (hwnd transient, pid/tid, title, class, DPI-aware client-area geometry, visible/enabled/minimized/maximized, z-order rank, active, owner/parent/root-owner native edges + resolved remote window_ids, role, modality, app_session_id, surface kind); event state machine (create/show/hide/destroy/name-change/location-change/z-order/focus/minimize/restore/enable) with idempotent re-delivery and HWND-recycling → fresh UUID; reconciliation scan ("scan wins for current truth, events win for history" via per-field event recency) recovering missed creates/destroys, idempotent; relationship graph (owner/parent resolution incl. late-owner Reannounce, nested root-owner chains); ADR-0006 session correlation (owner/root-owner chain cross-PID, corroborated pid-set, focus-transition chain — PID alone NEVER correlates, explicit counter-example test); surface classification (WS_CHILD-style = ChildControl, never a duplicate capture stream; owned/popup top-levels = own stream); modality inference (none / window-modal / app-modal from owner + owner+sibling enabled state, portable seam) | 32 new test cases in `tests/window_registry_test.cpp` (lifecycle, HWND reuse, reconciliation, nested/cross-PID dialogs, z-order, focus, destroy cleanup, anti-pattern correlation, surface, modality, session grouping) — macOS **177/177 CTest** (previous 145 + 32 new, 0 failed; commands below). Win32 adapter = Stage 2 (NOT RUN on macOS by design — Win32 targets do not compile there) |
| Protocol contract v0.1.0 (schema, VERSION, 22 test vectors, export tool) | `tools/export_protocol.sh` runs, produces **per-file** `SHA256SUMS` (fixed 2026-11-24: the old script hashed the printed file list, yielding one `-:` entry; now `sha256sum` over each file); export re-verified 25/25 |
| `core/` portable protocol model: types, JSON codec, semantic validation, version parsing, FPS gate, catalog, window relationships, input/key mapping, pairing, power policy, authorization | 104/104 CTest tests pass (commands below) |
| `network/` portable signaling: length-prefixed framing, TCP socket, signaling server/client (HELLO/pairing/heartbeat) | `signaling_e2e_test` loopback + concurrent clients + malformed-frame handling pass |
| `app/` session authorization + pairing state machine (RW-H-004, H-NET): per-connection `SessionAuth` (unpaired/pairing/paired/locked), shared `PairingHost` one-time-code store (single use), typed `not_paired` rejections for gated control kinds, post-pairing empty catalog, extended `--selftest` full handshake; tray pairing-code display/rotation (RW-H-005, H-FOUND) | `session_auth_test` (21 tests: 16 state-machine + 2 PairingHost rotation + 3 loopback e2e; the 3 RW-H-005 tests added 2026-11-24) + `--selftest` report (evidence below) |
| `docs/PROTOCOL.md` framing, validation, authorization, lifecycle rules | present, normative for both peers; codec now enforces `window_id` UUID format and `title` ≤ 512 chars at decode time (`bad_uuid` / `title_too_long`) |
| Media channel normative spec (RW-H-006, CONTRACT): PROTOCOL §9 — TCP 47988, handshake, key derivation (matching `core/pairing.cpp`), little-endian binary framing, stream lifecycle, robustness, security (ADR-0008) + golden vectors `protocol/test-vectors/media/` (3× AES-256-GCM + key derivation; generated with CryptoKit, independently re-verified with pyca/cryptography, OpenSSL 3.5.9 EVP, and shasum CLI) | `bash tools/export_protocol.sh <dir>` → bundle incl. `test-vectors/media/*.json` in SHA256SUMS (27 entries, verified); Client snapshot re-synced via `tools/sync_protocol_from_host.sh` (`shasum -a 256 -c` OK); §7.4 session-key formula corrected to the implemented `rwh-pairing-v1\|code\|host_name`; stale WebRTC note in §1 removed |
| `media/` portable media core (RW-H-007, H-MEDIA): the machine-facing parts of PROTOCOL §9 — binary packet framing (§9.4, 13-byte LE header, exact-byte unit test), per-window stream key / nonce-prefix derivation (§9.3, golden `key-derivation.json` reproduced exactly), thread-safe stream registry (§9.5, per-stream `core::FpsGate`, 16-bit collision documented: last add wins), RFC 2104 HMAC-SHA256 (§9.2 handshake, RFC 4231 TC1/TC2), portable `IAead` interface + Windows CNG AES-256-GCM backend (`AeadCng`, WIN32-only TU) | 127/127 macOS CTest (previous 104 + 23 new media tests, command below) + **CNG backend VERIFIED on the Windows runner**: `unit-tests` @ `d5c1f044` → `100% tests passed out of 130`, `RWH status=tests_ok`, incl. `MediaAeadCng.GoldenVectorsReproducedExactly` (all three `aes-256-gcm.json` ciphertext+tag values reproduced by live BCrypt on DESKTOP-PC), `RoundTrip1000Bytes`, `OpenRejectsTamperedCiphertext` (evidence below) |
| Win32 catalog + launch adapters (RW-H-009, H-CATALOG): `win32/catalog/` — `StartMenuSource` (per-user + per-machine Start Menu scan via `IShellLinkW`, dedupe, `app_id` derivation, URI classification: `ms-appx:`/`ms-app://` targets kept, other URI schemes skipped — FR-01), `Win32Launcher` (allow-listed `ShellExecuteExW`; the .lnk is resolved with `IShellLinkW::GetPath` + `GetFileAttributesW` existence check BEFORE the shell call — the shell's own resolution of a broken target blocks indefinitely in headless session-0, verified live; empty target → `launch_failed`, URI targets skip the existence check, COM-down → `launch_failed`), `Win32ProcessProbe` (`OpenProcess`/`WaitForSingleObject` on the launcher's retained NOCLOSEPROCESS handle). App wiring: `AppController` injects `ControlServices` (real catalog, real launches, heartbeat piggyback) and `--selftest` now exercises the full path: pairing → catalog (3 entries from the real Start Menu scan) → `launch_app` of a Start Menu entry → `launch_result started` → unknown app_id → `rejected` | macOS 145/145 CTest (143 portable + 2 new WIN32-unbuildable-on-macOS test files excluded; the 2 new are the Win32 test suites, Windows-only) + **Windows runner @ `7bb56873ae575e2da641c949f2f2fb74fb9532bd` (2026-11-25): `100% tests passed out of 157` (145 portable + 3 CNG + 9 WIN32-only: 5 `StartMenuSource` + 4 `Win32Launcher`), `Total Test time 0.91 s`, `RWH ctest_exit=0`, `RWH status=tests_ok`; `selftest` → `{"selftest":true,"passed":true,"port":47987,"reply_kinds":"hello;pairing_challenge;pairing_result;app_catalog;launch_result;launch_result","pairing":"accepted","catalog_entries":3,"launch":"started","launch_unknown":"rejected"}`, `RWH selftest_exit=0`, `RWH status=selftest_ok`** (evidence below) |
| Runner feedback rounds 3–4 (2026-11-25, plan rule 2 — live 24H2 findings recorded in code comments): round 3 run @ `d97ab841` → 3 failures: (a) `Win32Launcher.DeletedTargetIsLaunchFailed` hit the 300 s per-test timeout — `ShellExecuteExW` blocked in the shell's resolution of a missing-target .lnk in session-0 (the safety net converting a 3.5 h hang class into a clean 300 s failure); (b) the kill test raced a cmd.exe process that exits instantly on stdin-EOF under sshd (stale-handle `WAIT_FAILED`); (c) `SLGP_FILESYSTEM` made `GetPath` fail for URI targets, silently dropping ms-appx shortcuts. Fixes (`9a0b911b`): pre-resolve before the shell call, kill test retargeted to `msg.exe "/W *"` (plain Win32, blocks until killed; waits on the launcher's retained handle) → 156/157. Round 4: sole remaining failure `StartMenuSource.DisplayNameFallbacksAndAvailability` — probed the PC (WScript.Shell: create .lnk, `SetPath` to an ms-appx URI, Save, reopen → `TargetPath=[] URL=[]`): **URI targets set through `IShellLinkW::SetPath` do not persist on Windows 11 24H2**, so the fixture shortcut genuinely has no retrievable target and the source correctly skips it; the test now verifies the premise (`fixtureTargetEmpty()`: record absent AND target unretrievable — a retrievable-but-missing record is still a regression) instead of assuming the record exists, keeping the strong assertions for OSes where URIs do persist (`8967f00`) → 157/157 | `9a0b911b` runner run: 156/157, launcher tests 0.04 s / 0.08 s; `7bb5687` runner run: **157/157 PASS** (line above) |
| ADRs 0001–0009 | `docs/decisions/` (0008: MVP media transport — TCP 47988, H.264, AES-GCM, WebRTC deferred; 0009: single-repo topology deviation from plan §4.3) |
| `core/catalog_service` portable catalog + launch domain (RW-H-008, H-CATALOG): `CatalogSource`/`CatalogService` (scan → dedupe → protocol-ready `app_catalog` snapshot, revision + broadcast bookkeeping), `AppLauncher`/`ProcessProbe` interfaces + `LaunchService` (allow-listed launch by `app_id`, per-app launch sessions with UUID session ids, liveness sweep → `app_status`), `makeUuid`; `app::ControlServices` injection into `SessionAuth` (real catalog reply, real launch result, heartbeat piggyback for `app_catalog_changed` + `app_status` — thread-local, no cross-connection writes; nullptr = byte-identical legacy placeholders) | 143/143 macOS CTest (previous 127 + 14 `catalog_service_test` + 2 loopback `session_auth` e2e with wired services, command below) + **Windows runner `unit-tests` @ `d342bb3`: `100% tests passed out of 146`, `RWH status=tests_ok`** (146 = 143 portable + 3 CNG-only). Integration review fix `d342bb3`: `CatalogService::find()` returns a record copy (the child's pointer return was only valid under the now-released lock → UAF under a concurrent forced rescan) and `LaunchService::sessions()` returns a locked copy |

## Last successful build (this tree, portable core on macOS)

Commands:
```
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```
Result: 2026-11-27 (RW-H-010 Stage 1), macOS 27.2.1, CMake 4.5.1,
Ninja 1.13.2, clang (Apple). Build clean (exit 0). **177/177 tests
PASSED** (previous 145 + 32 new `window_registry_test` cases, 0 failed).
The 32 new cases: WindowRegistryLifecycle (11: full-field announce with
UUID identity, HWND recycling → fresh window_id, name/location/z-order
updates + idempotent re-delivery, hide/show, minimize/restore, focus
active-flag handoff, destroy-removal with parent survival, owner-death
clears child's resolved owner id, missed-create materialization),
Reconciliation (6: missed-CREATE recovery from scan, missed-DESTROY
recovery ("unavailable"), scan-wins-for-current-truth, events-win-for-history
over a stale scan, scan idempotency, scripted multi-miss sequence),
Relationships (6: early-owner announce carries owner id, late-owner
Reannounce, nested root-owner chain, cross-PID brokered dialog
correlated via owner chain, PID-alone counter-example (same PID in two
sessions' pid sets → uncorrelated), focus-transition correlation after
late anchor), SurfaceClassification (3: child control never a separate
stream, owned popup is its own surface, child-inside-tracked-parent),
Modality (4: no-owner never modal, enabled owner → window-modal,
owner+siblings disabled → app-modal transition both ways, enabled sibling
keeps window-modal), AppSession (2: primary-pid first top-level anchors,
explicit association → auxiliary role). The Win32 adapter test suites
(STAGE 2) build ONLY on Windows — on macOS they are excluded from the
CTest set (per AGENTS.md: Win32 targets do not compile on macOS; recorded
as NOT RUN there). Earlier suites (sha256, protocol_version, protocol_json
incl. 22 canonical vectors, validation, fps_policy, catalog,
window_relationships, input, pairing, power_policy, authorization, framing,
signaling_e2e, session_auth, the RW-H-007 media suites, and the RW-H-008
catalog_service suite) unchanged and green at their prior counts.

## Windows runner (Section 18.4) — verified

| Item | Evidence (2026-11-24) |
|---|---|
| Toolchain on DESKTOP-PC | VS Build Tools 2026 18.8.2, MSVC 14.51.36231, Windows SDK 10.0.26100.0, git 2.54.0 (preinstalled 2026-07-29); CMake 4.5.1 + Ninja 1.13.2 installed by the agent (user-level, no admin rights) |
| Runner | `tools/windows_runner.ps1` (PC: `C:\rwh\rwh-runner.ps1`, SHA256-verified at install) + `tools/windows-runner.sh` Mac wrapper; allow-listed actions only; machine-readable `RWH key=value` report |
| Exact-SHA build + unit tests | `windows-runner.sh unit-tests` @ `9450a49f8c58acb8c93680939e8b7f2dcb95d6c3`: `RWH sha=9450a49...`, `100% tests passed out of 83`, `RWH ctest_exit=0`, `RWH status=tests_ok` (RelWithDebInfo, Ninja, MSVC) |
| Portability fix found by the runner | POSIX `ssize_t` in the TCP layer is not declared by MSVC in C++ mode → `std::int64_t` (commit `978dc55`); clang still 83/83 |
| CNG AES-256-GCM media backend (RW-H-007) | `unit-tests` @ `d5c1f044a89ca09122475f987059b536cda5f0b6` (2026-11-24): `100% tests passed out of 130`, `RWH ctest_exit=0`, `RWH status=tests_ok` — the 3 `MediaAeadCng` tests PASS on MSVC/SDK 26100: golden vectors (ciphertext+tag, all three), 1000-byte round-trip, tamper rejection. CNG API corrections found by the runner (SDK 10.0.26100): `BCryptCreateKey` absent → `BCryptGenerateSymmetricKey`; GCM nonce+tag via `BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO` (pPaddingInfo), NOT via pbIV (a pbIV-only nonce yields `STATUS_INVALID_PARAMETER`); `BCRYPT_INIT_AUTH_MODE_INFO(info)` takes the struct NAME (macro wraps param in `&`) |
| Portable catalog/launch core (RW-H-008) | `prepare` @ `d342bb3258f4b13e1bba1226dbaea8da2cd95e95` (2026-11-25): `RWH status=prepared`; `unit-tests` @ same SHA: `100% tests passed out of 146`, `RWH ctest_exit=0`, `RWH status=tests_ok` (MSVC 14.51.36231 / SDK 10.0.26100, RelWithDebInfo, Ninja — 146 = 143 portable + 3 WIN32-only CNG); `selftest` @ same SHA: `{"selftest":true,"passed":true,"port":47987,"reply_kind":"app_catalog","envelopes":3,"pairing":"accepted","reply_kinds":"hello;pairing_challenge;pairing_result;app_catalog"}`, `RWH selftest_exit=0`, `RWH status=selftest_ok` (behavior unchanged — `AppController` does not wire `ControlServices` until RW-H-009) |
| Win32 catalog MSVC build (RW-H-009) | First MSVC compile of `win32/catalog/` — three build rounds found real SDK/MSVC discrepancies (clang on macOS hid all of them; plan rule 2, all recorded in the code): 4-arg `IShellLinkW::GetPath` (the 3-arg form does not exist in this SDK), 1-arg `IShellLinkW::SetPath` C++ signature, `std::towlower` NOT in `std` under MSVC 14.51 even with `<cctype>`/`<cwchar>` (replaced by a deterministic ASCII fold), `SLGP_FILESYSTEM`/`SLGP_LONGPATH` not declared by SDK 10.0.26100 `<shlobj.h>` under this project's version pins (locally `#ifndef`-defined, Microsoft-documented values 0x10/0x20). Build at `991dae4` completed: all 6 link steps OK (`rwh_win32_catalog.lib`, `rwh-host.exe`, both WIN32 test exes) |
| Runner hang triage + safety nets (2026-11-25) | First `unit-tests` ctest run at `991dae4` **hung ~3.5 h**: `win32_launcher_test` blocked in `ShellExecuteExW` — on Windows 11 24H2 `System32\notepad.exe` is an App Execution Alias stub for the PACKAGED Notepad, and app-model activation from the headless session-0 (sshd) context never completes (no process ever appeared; killed test PID 9736 + ctest PID 9148). Fixes committed: (a) the real-launch test now targets `%SystemRoot%\System32\cmd.exe` (plain Win32: starts, real PID + NOCLOSEPROCESS handle, idles on stdin, killable); (b) `ctest --timeout 300` per-test kill → clean failure instead of a block; (c) global 25-minute action watchdog in `rwh-runner.ps1` that kills a hung action (it then emits no `RWH status=..._ok` line → honest non-PASS; the `exit { block }` cleanup idiom is forbidden there — in this PS 5.1 context it terminates the script immediately, verified live). Runner re-uploaded SHA256-verified; `status` smoke-tested: `RWH watchdog_minutes=25`, `RWH status=ok` |
| RW-H-009 unit tests / selftest at the final SHA | `prepare` + `unit-tests` + `selftest` @ `7bb56873ae575e2da641c949f2f2fb74fb9532bd` (2026-11-25, after rounds 3–4 above): `RWH status=prepared`, **`100% tests passed out of 157`** (145 portable + 3 CNG + 9 WIN32-only: 5 `StartMenuSource` + 4 `Win32Launcher`), `Total Test time (real) = 0.91 s`, `RWH ctest_exit=0`, `RWH status=tests_ok` (MSVC 14.51.36231 / SDK 10.0.26100, RelWithDebInfo, Ninja; full chain exit 0); `selftest` → `{"selftest":true,"passed":true,"stage":"","port":47987,"reply_kind":"launch_result","envelopes":5,"pairing":"accepted","reply_kinds":"hello;pairing_challenge;pairing_result;app_catalog;launch_result;launch_result","error":"","catalog_entries":3,"launch":"started","launch_unknown":"rejected"}`, `RWH selftest_exit=0`, `RWH status=selftest_ok` |

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
them (logged as `session.gated`). (Resolved 2026-11-24 in the RW-H-006
integration: PROTOCOL §7.4 now documents the implemented
`rwh-pairing-v1|code|host_name` derivation — the `rwpair1|code|nonce`
form existed in no code.)

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
  input (SendInput), power (SetSuspendState /
  InitiateSystemShutdownExW). **NOT RUN** — not implemented yet (next
  feature tracks: H-WINDOWS/H-MEDIA/H-INPUT/H-POWER). The Win32 catalog
  adapters are DONE and verified (RW-H-009, evidence above).

## Blocked

| Task | Blocker |
|---|---|
| (none currently — the Stage 1 WebRTC blocker was resolved 2026-11-24 by ADR-0008: MVP media rides TCP 47988; WebRTC deferred) | — |

## Next tasks

1. C-INPUT (Client): FR-08 shortcut adaptation stack — task packet
   `remote-windows-client/docs/agent-tasks/RW-C-004.md` (written,
   committed `7bb5687`; input-pipeline integration is scoped out of the
   packet — it needs C-WINDOWS/H-INPUT).
2. H-WINDOWS (Host): window registry — Stage 1 (portable core) DONE on
   branch `agent/H-WINDOWS/RW-H-010` (177/177 macOS, above); Stage 2
   (Win32 WinEvents adapter + hermetic integration test, Windows-runner
   verified) is the active step. H-MEDIA: Win32 WGC capture (next host
   track after the registry lands).

Done:

- C-MEDIA: macOS Client media channel (RW-C-003) — DONE 2026-11-25
  (branch `agent/C-PERF/RW-C003`; merged into main at `c5320e3`,
  fast-forward from `275eb09`): TCP 47988 client with the §9.2
  handshake, §9.4 framing, §9.3 key derivation, per-stream AES-256-GCM
  (CryptoKit — the golden `key-derivation.json` and `aes-256-gcm.json`
  vectors in `Protocol/Contract/test-vectors/media/` reproduced
  byte-exact), VideoToolbox H.264 decode into a real CAMetalLayer
  NSWindow per `window_id`; 369/369 Swift checks on main (250 baseline
  + 119 new, exit 0). NOT RUN (headless): decoded pixel output. Known
  limitation: no media-channel auto-reconnect after a §9.6 failure
  (teardown until re-pairing).
- H-CATALOG: Win32 catalog + launch adapters + app wiring (RW-H-009) —
  DONE 2026-11-25 (branch `agent/H-CATALOG/RW-H009`; integrated at
  `e2c6f85`/`94c712c` + held-handle review fix `7f1b344` + runner
  feedback rounds 3–4 `d97ab841`/`9a0b911b`/`8967f00`; 145/145 macOS
  ctest; Windows runner **157/157 unit tests + selftest_ok @
  `7bb5687`** — full evidence in the "Windows runner" tables above,
  incl. the 24H2 live findings: session-0 shell-launch hang class,
  `SLGP_FILESYSTEM` URI drop, URI targets not persisting via
  `IShellLinkW::SetPath`).

- H-CATALOG: portable catalog + launch services (RW-H-008) — DONE
  2026-11-25 (branch `agent/H-CATALOG/RW-H008`; integrated at `c3891c1`
  + review fix `d342bb3`; 143/143 macOS ctest; Windows runner
  `unit-tests` 146/146 + `selftest` selftest_ok @ `d342bb3`, evidence in
  the tables above). Win32 adapters + app wiring = follow-up RW-H-009.
- H-MEDIA: portable media core (RW-H-007) — DONE 2026-11-24
  (branch `agent/H-MEDIA/RW-H-007`; `media/` library: §9.4 framing,
  §9.3 key derivation with the golden `key-derivation.json` reproduced
  exactly, §9.5 stream registry, §9.2 HMAC-SHA256, `IAead` + CNG
  backend; 127/127 macOS ctest — previous 104 + 23 new; CNG backend
  **verified on the Windows runner** 2026-11-24: 130/130 @ `d5c1f044`,
  `RWH status=tests_ok`, golden AES-256-GCM vectors reproduced by live
  BCrypt on DESKTOP-PC).
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
- CONTRACT: RW-H-006 media-channel spec — DONE 2026-11-24 at `d270d15`
  (PROTOCOL §9 normative 9.1–9.7; golden vectors 3× AES-256-GCM + key
  derivation, triple-verified; 104/104 macOS ctest; `.proto`/`VERSION`
  untouched; §7.4 formula fix + export-script media-vector gap +
  ADR-0008 nonce-prefix wording resolved in the same integration,
  commit below).
