# Windows Host — STATUS

Last updated: 2026-11-27 (RW-H-011 INTEGRATED — merged into main @
`d363c93d955e79e2cc1f0a9da8a2cca3ebb488ff` (fast-forward from
`42e46b7`); merged-revision verification: **194/194 macOS CTest** (exit
0) + **Windows runner @ `d363c93`: build exit 0 (`RWH status=build_ok`)
+ 213/213 unit tests = 212 passed + 1 skipped by design (`RWH
status=tests_ok`, `RWH ctest_exit=0`)** — #213
`WgcCapture.HermeticConsentGatedPipeline` skips cleanly in session 0
(hermetic consent-gated live-capture test; the live frame path through
the real adapter is still BLOCKED, tracked as RW-H-011b). Pre-merge
branch state `agent/H-MEDIA/RW-H-011` @ `d363c93` (code-identical tip):
Stage 1 portable capture-session pipeline (FR-03/FR-06) —
`ICaptureSurfaceSource`/`IFrameConsumer` seams with typed
`CaptureError{ConsentRequired, SourceClosed, ...}`, `CaptureSessionManager`
(auto-share of auxiliary windows from the `WindowRegistry`, parent
retention, minimize-pause, size-change recreation + IDR, `SourceClosed`
isolation), bounded drop-obsolete `FrameGate`, per-session metrics,
encoder seam over the RW-H-007 media-core factory (no media-core files
modified) — **194/194 macOS CTest** (178 prior + 16 new
`capture_pipeline_test`); Stage 2 Win32 WGC adapter
(`win32/capture/WgcCaptureSource`: per-surface `GraphicsCaptureItem` +
D3D11 frame pool, consent gate via `RequestAccessAsync(Programmatic)`
bounded poll, null-frame×30 `SourceClosed` safety net) + hermetic
consent-gated Windows test — **Windows runner @ `d363c93`: build ok +
212 passed / 1 skipped (`RWH status=tests_ok`)**; pre-task runner
baseline 196/196 @ `42e46b7`. RW-H-010 window registry remains merged
into main @ `c97bbbf48f3d2aa5bb7de9abc5d41922c8a4174c` (178/178 macOS +
Windows runner build_ok + 196/196 @ `c97bbbf`); RW-H-009 Win32 catalog
is the last fully-verified Windows item (157/157 + selftest_ok @
`7bb5687`). Client C-MEDIA (RW-C-003) media channel merged into main at
`c5320e3` on 2026-11-25 (client `docs/STATUS.md`); the host tree was
unchanged by that merge.)

## Implemented and verified

| Item | Evidence |
|---|---|
| Per-window capture-session pipeline (RW-H-011 Stage 1, H-MEDIA, plan FR-03/FR-06): `core/capture_source.{hpp}` (portable `ICaptureSurfaceSource` seam: `startSurface`/`stopSurface`, `CapturedFrame{window_id, seq, timestamp, w, h, format, payload}` → `IFrameConsumer`, typed `CaptureError{None, ConsentRequired, SourceClosed, SurfaceNotAvailable, InternalError}`), `core/capture_encoder.{hpp}` (`IVideoEncoder`/`IVideoEncoderFactory`/`IEncodedFrameConsumer` per-session encoder seam — the RW-H-007 media core plugs in here; no media-core files touched), `core/capture_gate.{hpp,cpp}` (bounded per-session `FrameGate`: fixed max depth, drop-oldest-obsolete, thread-safe, `droppedCount`), `core/capture_session_manager.{hpp,cpp}` (`CaptureSessionManager`: consumes the real `WindowRegistry` notification vector from the app layer — Announce/Reannounce → FR-03 auto-share (independently rendered surface of an already-shared app session starts its own session with NO manual request, ANY pid, correlation from the registry; ChildControl never), parent retention (dialog destroy stops only the dialog), destroy → stop ONLY that session, minimize → Pause (default, configurable `LowFps` at `minimize_low_fps`), resume = fresh surface start + IDR, size/dpi change → coalesced session recreation (stop+start, new geometry, IDR; position-only moves do NOT recreate), `SourceClosed` → session Ended + bounded recreation attempts (`max_recreate_attempts`) on later registry news + IDR on restart, per-session worker thread (isolation: one slow/dead encoder never blocks others), per-session `CaptureSessionMetrics` (frames received/delivered/dropped, queue depth + max depth, keyframe requests, last-frame ts, geometry, error count, state) for later STREAM_STATUS/PERFORMANCE_STATS) | 16 new test cases in `tests/capture_pipeline_test.cpp` (portable; fake source + real `WindowRegistry` + scripted notifications): per-window independence (stop A keeps B), unshare/reshare resource release, auto-share of auxiliary surface (different pid, same app_session_id, zero manual calls), ChildControl never auto-shared, parent retention + dialog-destroy-stops-only-dialog, minimize pause/resume with fresh frame + IDR, LowFps policy configurable, share-while-minimized starts paused, size-change recreation (stopSurface + IDR, new geometry), position-only move no-recreate, drop-obsolete bounded at fixed depth (bound asserted under load, accounting identity received==delivered+dropped+queue), metrics correctness, `SourceClosed` isolation + recreate, `SourceClosed` bounded-attempts exhaustion, `ConsentRequired` typed error surfaced with no shared session left. macOS **194/194 CTest** (previous 178 + 16 new, 0 failed; commands below). Win32 WGC adapter = Stage 2 (this branch) |
| Win32 WGC capture adapter (RW-H-011 Stage 2, H-MEDIA, plan FR-03): `win32/capture/WgcCaptureSource.{hpp,cpp}` — the Windows.Graphics.Capture implementation of the portable `ICaptureSurfaceSource` seam. Per window: `GraphicsCaptureItem` from HWND via `IGraphicsCaptureItemInterop::CreateForWindow` (Win10 1903+, windows.graphics.capture.interop.h) + free-threaded `Direct3D11CaptureFramePool` (Win10 1809+; FrameArrived on the pool's internal worker thread — no message pump) + per-surface capture worker thread + `GraphicsCaptureSession` (`IsBorderRequired=false`). **Consent gate (mandatory, human-interaction-aware)**: before the first `CreateForWindow` in a process, `GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Programmatic)` → `IAsyncOperation<AppCapabilityAccessStatus>` (Win10 2104+) with a BOUNDED 2 s wait — the OS prompt is a USER ACTION (a human at the PC); unanswered/Denied/Unavailable → typed `ConsentRequired`, never a blocking capture; Granted is cached for the process. "IsUserConsentGranted" is NOT a documented API (both candidate Learn pages 404 on the doc-check date) — `RequestAccessAsync` is the documented consent path. Session `Closed`/item `Closed` → `onSourceError(SourceClosed)` (manager recreation path) except `AccessDenied` → `SourceUnavailable` (manager ends the session WITHOUT retrying — consent is a user action, never spin against a refusal; portable manager patch in this change). Minimized/occluded: WGC halts delivery (no event) — the portable manager drives pause/resume and resume restarts the surface (fresh frames + IDR). **Pixel path (documented choice)**: each WGC D3D11 frame (B8G8R8A8 UNORM) → bounded per-window D3D11 STAGING texture (CPU_ACCESS_READ, actual frame size, row pitch handled) → tight CPU BGRA8 `CapturedFrame::pixels`. No full-screen GPU readback. A zero-copy GPU→encoder path is NOT possible yet: the `IVideoEncoder` seam takes CPU pixels and the media core has NO D3D11 encoder input (no H.264 encoder in the tree — ADR-0007 "video pipeline not yet integrated"); the real encoder backend (D3D11/MF H.264, Win32-only) is the cross-owner H-MEDIA follow-up filed with this task's report. **WGC API doc-check 2026-11-27** (learn.microsoft.com): CreateForWindow (1903), CreateFreeThreaded (1809), CreateCaptureSession/StartCapture/Close/IsBorderRequired, Closed events + reasons, RequestAccessAsync/Programmatic/AppCapabilityAccessStatus (2104), IsUserConsentGranted not documented (404). Windows-only target (`rwh_win32_capture`, CMake-guarded) — NOT RUN on macOS by design (AGENTS.md: Win32 targets do not compile there) | Hermetic consent-gated Windows test `tests/win32_wgc_capture_test.cpp` (WIN32-only target `win32_wgc_capture_test`): creates its OWN top-level window (320×200 client, painted, known DPI via GetDpiForWindow) and drives the REAL adapter through the portable manager; IF consent not granted → typed `ConsentRequired` path asserted + live-capture assertions GTEST_SKIP (granting consent is a USER ACTION); IF granted → ≥5 frames with EXACT physical pixel dimensions + correct window_id routing + IDR at start + unshare releases all adapter resources; headless session (session 0) → frame assertions GTEST_SKIP (WGC needs a desktop — run interactive via schtasks). All waits bounded — the test can never hang on UI. Windows runner verification at the task-branch SHA: see the Windows runner table below |
| Window registry core (RW-H-010 Stage 1, H-WINDOWS, plan FR-03): `core/window_registry.{hpp,cpp}` — live registry of top-level windows; durable `window_id` = UUID v4 assigned once at first sight (HWND never the remote identity, ADR-0006); full `WindowRecord` (hwnd transient, pid/tid, title, class, DPI-aware client-area geometry, visible/enabled/minimized/maximized, z-order rank, active, owner/parent/root-owner native edges + resolved remote window_ids, role, modality, app_session_id, surface kind); event state machine (create/show/hide/destroy/name-change/location-change/z-order/focus/minimize/restore/enable) with idempotent re-delivery and HWND-recycling → fresh UUID; reconciliation scan ("scan wins for current truth, events win for history" via per-field event recency) recovering missed creates/destroys, idempotent; relationship graph (owner/parent resolution incl. late-owner Reannounce, nested root-owner chains); ADR-0006 session correlation (owner/root-owner chain cross-PID, corroborated pid-set, focus-transition chain — PID alone NEVER correlates, explicit counter-example test); surface classification (WS_CHILD-style = ChildControl, never a duplicate capture stream; owned/popup top-levels = own stream); modality inference (none / window-modal / app-modal from owner + owner+sibling enabled state, portable seam) | 33 new test cases in `tests/window_registry_test.cpp` (lifecycle, HWND reuse, reconciliation, nested/cross-PID dialogs, z-order, focus, destroy cleanup, anti-pattern correlation, surface, modality, session grouping) — macOS **178/178 CTest** (previous 145 + 33 new, 0 failed; commands below). Win32 adapter = Stage 2, **DONE and runner-verified at `6ea4591`** (evidence in the Windows runner table) — NOT RUN on macOS by design: Win32 targets do not compile there |
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
Result: 2026-11-27 (RW-H-011 Stage 1 @ `078ea4c`; branch `agent/H-MEDIA/RW-H-011`), macOS 27.2.1, CMake 4.5.1, Ninja 1.13.2, clang (Apple). Build clean (exit 0).
**194/194 tests PASSED** (previous 178 + 16 new `capture_pipeline_test`
cases, 0 failed; re-verified over 5 consecutive full-suite runs).
The 16 new cases: CaptureGate (2: bounded drop-oldest under overflow,
drop counting) + CapturePipeline (14: explicit share runs the full
pipeline to the sink; `ConsentRequired` typed error surfaced with NO
shared session left; per-window independence (stop A keeps B); unshare
releases resources and reshare works; auto-share of an auxiliary surface
with a DIFFERENT pid in the same app session (zero manual calls);
ChildControl never auto-shared; parent retention (parent alive while a
dialog session is active) + dialog destroy stops only the dialog;
minimize → Pause then restore → fresh frame + IDR; `LowFps` minimize
policy configurable; share-while-minimized starts paused; size change →
session recreation (adapter surface stopped, new geometry, IDR requested);
position-only move does NOT recreate; drop-obsolete gate bounded at fixed
depth under 400-frame load with the accounting identity
received==delivered+dropped+queue; per-session metrics
(counts/timestamps/state/geometry) correct; `SourceClosed` isolates
sessions and recreates; `SourceClosed` exhausts bounded recreation
attempts). The Win32 WGC adapter suites (Stage 2) build ONLY on Windows —
on macOS they are excluded from the CTest set (per AGENTS.md: Win32
targets do not compile on macOS; recorded as NOT RUN there). Earlier
suites (sha256, protocol_version, protocol_json incl. 22 canonical
vectors, validation, fps_policy, catalog, window_relationships, input,
pairing, power_policy, authorization, framing, signaling_e2e,
session_auth, the RW-H-007 media suites, the RW-H-008 catalog_service
suite, and the RW-H-010 window_registry suite) unchanged and green at
their prior counts. Prior build record: RW-H-010 final branch state
`6ea4591` re-verified on merged main @
`c97bbbf48f3d2aa5bb7de9abc5d41922c8a4174c` (same commands, build exit 0,
178/178).

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
| RW-H-010 Stage 2 — Win32 window-registry adapter (2026-11-27, task-branch SHA `6ea4591f273648e9abf0bd06d95d41fc298baa86`, branch `agent/H-WINDOWS/RW-H-010` — the runner's `prepare` fetches only `origin/main`, so the runner checkout was fetched + checked out to the task-branch SHA directly over ssh, then the allow-listed `build`/`unit-tests` actions ran against it) | `windows-runner.sh build` @ `6ea4591`: exit 0, all 32 targets linked incl. `win32_windows_registry_test.exe` — first MSVC compile of `win32/windows/Win32WindowSource.cpp` (clean: no SDK/MSVC discrepancies found; MSVC 14.51.36231, SDK 10.0.26100.0, RelWithDebInfo, Ninja), `RWH status=build_ok`. `windows-runner.sh unit-tests` @ `6ea4591` (session 0): **`100% tests passed out of 196`** (157 pre-task baseline + 33 core `window_registry_test` + 6 `Win32WindowsRegistryTest`), `Total Test time (real) = 15.61 s`, `RWH ctest_exit=0`, `RWH status=tests_ok`; all 6 hermetic adapter tests `Passed` individually (tests #191–#196: UUID announce + geometry, owned-dialog owner/links/modality, owner-disable → app-modal, dialog-destroy parent survival, initial-scan recovery, title-change + minimize/restore). **Interactive-session re-run** (schtasks on the user's desktop, same SHA): **6/6 PASSED in 5.23 s**, no leftover processes, no failure dumps. WinEvent API details verified against Microsoft docs 2026-11-27 (SetWinEventHook runs on the creating thread's message loop → dedicated hook thread with pump; EVENT_OBJECT_* 0x8000..0x800C; EnumWindows; GetWindow(GW_OWNER); GetAncestor(GA_ROOTOWNER); GetWindowThreadProcessId; DWM/DPI). **Live 24H2 build-26300 findings** (probe-verified on this PC, recorded in code comments): (a) a NULL-window `SetTimer` on the hook thread NEVER ticks (session 0 AND interactive) → reconciliation cadence runs on a dedicated thread posting `kScanNowMsg` to the hook thread; (b) SW_MINIMIZE/SW_RESTORE emit only LOCATIONCHANGE/REORDER — NO STATECHANGE → the adapter refreshes enabled/minimized/maximized on those events too; (c) the OS creates transient invisible zero-client-area helper windows in an app's own process whose CREATE can precede the main window's → the session-anchor heuristic requires a non-zero client area (portable regression test `AppSession.ZeroGeometryHelperDoesNotStealAnchor`). Session-0 context (sshd): `IsWindowVisible` always FALSE and SW_MINIMIZE is a no-op — the integration test is session-context-aware and asserts per context (structured fields only, no window contents in logs) |
| RW-H-011 Stage 2 — Win32 WGC capture adapter + hermetic test (2026-11-27, task-branch SHA `f397e5329140243c102c1ef49bd3d46bf277a`, branch `agent/H-MEDIA/RW-H-011`; runner checkout = the task-branch SHA directly, as in RW-H-010) | runner internal build green @ `f397e53` (all targets incl. `rwh-host.exe`, `win32_wgc_capture_test.exe`, `rwh_win32_capture` lib; MSVC 14.51.36231 / SDK 10.0.26100.0, RelWithDebInfo, Ninja). Session-0 unit tests @ `f397e53`: first run `99% tests passed, 2 tests failed out of 213` — the 2 loopback-E2E flake class: `147 SignalingE2E.MalformedFramesCountedNotCrash` (Failed) and `168 SessionAuthE2E.PairedServicesHeartbeatPiggyback` (SEGFAULT); both PASS on the immediate re-run and the RW-H-011 diff only touches `tests/win32_wgc_capture_test.cpp` (unrelated — recorded honestly, not as 213/213-on-first-run). Re-run: 213/213 executed, 212 passed + 1 skipped by design (`#213 WgcCapture.HermeticConsentGatedPipeline` — live-capture assertions SKIP in session 0), `Total Test time (real) = 18.29 s`, `RWH ctest_exit=0`, `RWH status=tests_ok`. **Interactive hermetic test** (schtasks, user session 6, same SHA): clean SKIP, `EXIT_CODE=0`, `win32_wgc_capture_test.cpp(267): Skipped — fewer than 5 WGC frames within 15000 ms — headless session (no desktop) or the capture is not delivering; live-capture assertions NOT RUN in this context. Run interactive (schtasks) to verify.` (log `C:\rwh\wgc_interactive_f397.log`) — the live path IS entered (consent granted: user clicked the OS prompt during earlier probing) but <5 frames arrived in 15 s. **Live-capture bisect (hand-rolled probe v8b on the PC, `C:\rwh\wgc_probe\`, NOT in the repo)**: consent × frame-thread-message-pump matrix, 4 configs — every config reached `W16: StartCapture ok`, `F1: FrameArrived`, `F3: desc w=320 h=200 fmt=87` and delivered exactly 1 frame (the initial composite — the probe window is static after its first paint, and WGC emits frames on content change), `W17: wait done frames=1 null=0 drops=0`, rc=25: A (consent + nopump) = 1, B (consent + pump) = 1, C (noconsent + nopump) = 1, D (noconsent + pump) = 1 → a message pump on the frame thread is NOT the differentiator (the pool thread delivers the initial frame with or without pumping) and the bounded RequestAccessAsync gate does not block (a freshly-built probe exe got `C1: consent Allowed` immediately with no new prompt click, and capture works even with the gate skipped — the consent state on this PC appears per-user; the adapter's documented gate still applies). The residual defect sits in the REAL adapter/manager frame path (probe v8a through the real adapter: session Running, delivered=0 — while the hand-rolled pool-thread handler delivered the initial frame) — live-capture verification is BLOCKED pending that adapter-internal diagnosis (not chased further in this task). **Consent state**: Programmatic WGC consent prompt granted by user click (2026-11-27, earlier probe); NOTE: the production `rwh-host.exe` will need its own one-time consent = remaining USER ACTION (per the matrix observation the consent state appears per-user on this PC, so the bounded gate should return Allowed without a prompt; re-verify on first production run). **SDK 10.0.26100 rework findings** (probe-verified; all recorded in code comments): (a) the consent status enum lives in the NESTED winrt namespace `winrt::Windows::Security::Authorization::AppCapabilityAccess` (enum type: impl header line 24; statics like `::Allowed` in the same nested namespace: winrt header line 351) — the flat path `winrt::Windows::Security::Authorization::AppCapabilityAccessStatus` FAILS on this SDK (C2039 not a member + C2873 using-declaration); the compiled spelling was verified byte-exact (od) before the probe built green (`CL_EXIT=0` after the one known fix = the missing nested-namespace segment). (b) `IID_IDXGIDevice` moved to `dxguid.lib` in SDK 26100 (else LNK2019). (c) the UMP composition include chain pulls legacy-projection headers that collide with cppwinrt names → qualify `AsyncStatus` (C2872), plain HRESULT sentinel in WGC item interop (C2737), `IInspectable` ambiguity resolved; the using-declaration form (not a fully-qualified alias) is the collision-free form. WGC API doc-check 2026-11-27 (see the adapter row above). |

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

- Win32 adapters (`win32/`): capture (WGC) is IMPLEMENTED and build-verified
  (RW-H-011 Stage 2 — the live-capture frame path is BLOCKED, see the Windows runner
  table); input (SendInput), power (SetSuspendState / InitiateSystemShutdownEx) —
  **NOT RUN** — not implemented yet (next feature tracks: H-INPUT/H-POWER). The
  Win32 catalog adapters are DONE and verified (RW-H-009, evidence
  above); the Win32 window-registry adapter is DONE and runner-verified
  (RW-H-010 Stage 2, evidence in the Windows runner table above).

## Blocked

| Task | Blocker |
|---|---|
| (none currently — the Stage 1 WebRTC blocker was resolved 2026-11-24 by ADR-0008: MVP media rides TCP 47988; WebRTC deferred) | — |

## Next tasks

1. C-INPUT (Client): FR-08 shortcut adaptation stack — task packet
   `remote-windows-client/docs/agent-tasks/RW-C-004.md` (written,
   committed `7bb5687`; input-pipeline integration is scoped out of the
   packet — it needs C-WINDOWS/H-INPUT).
2. H-MEDIA (Host): RW-H-011 Win32 WGC capture — Stage 2 DELIVERED on branch
   `agent/H-MEDIA/RW-H-011` (adapter + hermetic consent-gated test; Windows runner
   build green + session-0 213/213 @ `f397e53`; interactive live-capture = clean
   SKIP, documented BLOCKED with the A–D bisect matrix in the Windows runner
   table above). Remaining: (a) diagnose the adapter-internal frame path (probe
   v8a: delivered=0 through the real adapter vs 1 frame via the hand-rolled
   pool-thread handler); (b) the H.264 encoder backend (RW-H-012 — no in-tree
   H.264 encoder; the seam wraps a factory without a concrete backend); (c) H-FOUND
   app-layer wiring. (The RW-H-010 window registry merged into main @
   `c97bbbf48f3d2aa5bb7de9abc5d41922c8a4174c` on 2026-11-27: merged-revision
   178/178 macOS CTest + Windows runner build_ok + 196/196 @ `c97bbbf`;
   pre-merge branch evidence @ `6ea4591` in the Windows runner table above.)

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
