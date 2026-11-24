# macOS Client — STATUS

Last updated: 2026-11-24 (C-CONNECTION complete: signaling client + pairing flow green).

## Implemented and verified

| Item | Evidence |
|---|---|
| SwiftPM package `RemoteWindows`: library `RemoteWindowsCore` + executable `RemoteWindowsApp` + test runner executable, macOS 13+ target | `swift build` exit 0 (commands below) |
| App bootstrap: `main.swift`, `AppDelegate`, `AppController` main status window (title, version, host address, connection state — all via L10n keys) | `swift build`; launch smoke test `tools/launch_smoke_test.sh`: "APP RUNNING after 4s" (killed cleanly) |
| **Signaling client + pairing flow (C-CONNECTION, task RW-C-001)**: `SignalingClient` state machine (disconnected/connecting/pairing/paired/rejected/locked) in `RemoteWindowsCore/Network/` with injectable transport/clock/timer; real TCP transport `NWSignalingTransport` (Network.framework); HELLO → challenge → 6-digit code (client-side format validation) → pairing_result (host safe_message surfaced, retry on rejection, no auto-retry when locked); auto `app_catalog_request` once after pairing (empty entries displayed honestly); 5 s heartbeat while connected (cancellable, last-activity exposed); any socket error resets to `.disconnected` — no stale connection | `SignalingClientSuite` 14 cases: 12 fake-transport cases + 2 loopback cases (real 127.0.0.1 socket, in-test POSIX host replying the canonical shapes; no LAN/external network) |
| **Connection/pairing UI in the AppKit status window**: editable host address field (prefilled from Preferences `host:port`, saved on connect), Connect/Disconnect buttons, pairing code field + Enter code button, status label with the real state (Disconnected / Connecting / Pairing / Paired / Rejected: \<safe_message\> / Locked), host-name label, catalog label ("No applications available yet" while the host catalog is empty). Every control performs a real action (plan rule 6); controls enable/disable per state | `swift build` exit 0; launch smoke test `tools/launch_smoke_test.sh`: "APP RUNNING after 4s" (killed cleanly) |
| Localization: `L10n` helper + `Resources/Localizable.en.json` (English-only MVP, FR-14); all new C-CONNECTION strings added as L10n keys (`status.pairing/paired/rejected/locked`, `connect.button`, `pairingCode.*`, `catalog.empty/available`, ...) | `L10nSuite` 4 cases pass (incl. `connectionStringsResolve`) |
| Preferences: versioned UserDefaults model (`inactive_fps` default 10, range 1–60; host address/port 47987; master shortcut toggle; `settingsSchemaVersion = 1`) | `PreferencesSuite` 3 cases pass |
| Pinned protocol contract snapshot v0.1.0 in `Protocol/Contract/` (25 files, SHA256SUMS-verified) via `tools/sync_protocol_from_host.sh`; provenance `single-repo:<commit>` | `shasum -a 256 -c SHA256SUMS` → 25 OK |
| Protocol adapter (`RemoteWindowsCore/Network/`): envelope JSON codec, semantic validation (1:1 port of Host `core/protocol_validation.cpp`, same error codes/bounds), framing (uint32 BE, 8 MiB cap, incremental `FrameReader`) — reused unchanged by the signaling client | `ProtocolCodecSuite` (20 cases), `ProtocolVectorSuite` (all 22 canonical vectors), `FramingSuite` (6 cases) pass |
| Cross-language golden set | same 22 vectors execute in the Host C++ suite and the Swift suite |
| No pairing secrets persisted (codes/keys are never written to disk or UserDefaults in this task; Keychain storage is a later task) | code review of the diff: no persistence of `one_time_code`/nonces/session material |

## Last successful build (this tree, Mac mini)

Commands:
```
cd remote-windows-client
swift build
swift run RemoteWindowsAppTests        # 185 checks, exit 0
tools/launch_smoke_test.sh             # app launch evidence
```
Result: 2026-11-24, macOS 27.2.1, Swift 6.4.2 (Command Line Tools —
no full Xcode), SwiftPM. Build clean (exit 0).
**185/185 test checks PASSED, 0 failures** (exit code 0; verified across 3
consecutive runs). Previous baseline was 83/83; C-CONNECTION added 102
checks (14 new `SignalingClientSuite` cases + 5 new `L10nSuite` checks).
Launch smoke: process running after 4 s, terminated cleanly.

### Environment deviation (recorded per plan rule 2)

1. The plan names XCTest for Client tests. This machine has the Swift
   Command Line Tools only; the XCTest **framework ships with Xcode, not
   the CLT**. The suites therefore run as a plain SwiftPM executable with
   a minimal assertion harness (`Tests/RemoteWindowsAppTests/Harness.swift`);
   cases port 1:1 to XCTest when Xcode is available. See
   `docs/decisions/ADR-0001-client-build-system-swiftpm.md`.
2. The macOS 27.2 CLT SDK ships a redesigned `Network` Swift overlay:
   `NWEndpoint` is now a Swift enum (`NWEndpoint.hostPort(host:port:)` with
   `NWEndpoint.Host` / `NWEndpoint.Port` value types) and
   `NWConnection.State` gained `setup`/`preparing` cases.
   `NWSignalingTransport` is written against that API (it is the only
   API visible to the current toolchain).

## NOT implemented yet (honest list — no fake functionality)

- LAN integration test against the running Windows Host (next task —
  Client side is ready; the handshake is verified against the Host's
  `app/SessionAuth.cpp` behavior, Host commit 870aec1, per the orchestrator).
- Catalog UI with real entries + launch lifecycle (C-CATALOG). The current
  UI shows the honest empty-catalog state; the host catalog scanner is a
  later Host task. `app_catalog_changed` handling (re-request) lands with C-CATALOG.
- Remote window presentation, video surface (C-WINDOWS / C-PERF).
  Video transport is not integrated: WebRTC is a pinned-build task
  (root ADR-0009); the protocol carries no video in v0.1.0.
- Keyboard shortcut adaptation (FR-08) — next task.
- Power controls UI (FR-11/12).
- Pairing-secret persistence in the Keychain (later security task;
  RW-C-001 deliberately persists nothing).
- `.app` bundle packaging (MVP runs as a SwiftPM executable).

## Blocked

| Task | Blocker |
|---|---|
| LAN e2e test (Client ↔ Windows Host) | Needs the Windows Host running on the LAN (Host app verified on Windows at Host commit 870aec1 per the orchestrator; no Host was reachable from this machine for this task, so the e2e run is NOT RUN) |

## Next tasks

1. C-CONNECTION follow-up: LAN integration test against the real Windows
   Host (connect, pair with the live code, catalog, heartbeat).
2. C-CATALOG: catalog UI with entries, `app_catalog_changed` re-request,
   launch lifecycle.
3. C-INPUT: shortcut adaptation stack with FR-08/§6.4 coverage.

## Notes

- Write-scope note (RW-C-001): the task's directory-level write scope listed
  `Sources/RemoteWindowsCore/Network/`; task requirement 6 explicitly mandates
  the new user-facing strings as L10n keys in
  `Sources/RemoteWindowsCore/Resources/Localizable.en.json` (plan rule 3), so
  that file was updated as directed. No files outside the Client tree were
  modified; the protocol snapshot was not touched.
