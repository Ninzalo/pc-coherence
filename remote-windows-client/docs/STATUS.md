# macOS Client — STATUS

Last updated: 2026-11-24 (tree created; scaffold + protocol adapter green).

## Implemented and verified

| Item | Evidence |
|---|---|
| SwiftPM package `RemoteWindows`: library `RemoteWindowsCore` + executable `RemoteWindowsApp` + test runner executable, macOS 13+ target | `swift build` exit 0 (commands below) |
| App bootstrap: `main.swift`, `AppDelegate`, `AppController` main status window (title, version, host address, connection state — all via L10n keys) | `swift build`; launch smoke test `tools/launch_smoke_test.sh`: "APP RUNNING after 4s" (killed cleanly) |
| Localization: `L10n` helper + `Resources/Localizable.en.json` (English-only MVP, FR-14) | `L10nSuite` 3 cases pass |
| Preferences: versioned UserDefaults model (`inactive_fps` default 10, range 1–60; host address/port 47987; master shortcut toggle; `settingsSchemaVersion = 1`) | `PreferencesSuite` 3 cases pass |
| Pinned protocol contract snapshot v0.1.0 in `Protocol/Contract/` (25 files, SHA256SUMS-verified) via `tools/sync_protocol_from_host.sh`; provenance `single-repo:<commit>` | `shasum -a 256 -c SHA256SUMS` → 25 OK |
| Protocol adapter (`RemoteWindowsCore/Network/`): envelope JSON codec, semantic validation (1:1 port of Host `core/protocol_validation.cpp`, same error codes/bounds), framing (uint32 BE, 8 MiB cap, incremental `FrameReader`) | `ProtocolCodecSuite` (20 cases), `ProtocolVectorSuite` (all 22 canonical vectors: valid → decode+validate; invalid → exact violation code), `FramingSuite` (6 cases) pass |
| Cross-language golden set | same 22 vectors execute in the Host C++ suite and the Swift suite |

## Last successful build (this tree, Mac mini)

Commands:
```
cd remote-windows-client
swift build
swift run RemoteWindowsAppTests        # 83 checks, exit 0
tools/launch_smoke_test.sh             # app launch evidence
```
Result: 2026-11-24, macOS 27.2.1, Swift 6.4.2 (Command Line Tools —
no full Xcode), SwiftPM. Build clean (exit 0).
**83/83 test checks PASSED, 0 failures** (exit code 0).
Launch smoke: process running after 4 s, terminated cleanly.

### Environment deviation (recorded per plan rule 2)

The plan names XCTest for Client tests. This machine has the Swift
Command Line Tools only; the XCTest **framework ships with Xcode, not the
CLT** (verified: no XCTest.framework present, `import XCTest` and
`import Testing` both fail to resolve). The suites therefore run as a
plain SwiftPM executable with a minimal assertion harness
(`Tests/RemoteWindowsAppTests/Harness.swift`); cases port 1:1 to XCTest
when Xcode is available. See `docs/decisions/ADR-0001-client-build-system-swiftpm.md`.

## NOT implemented yet (honest list — no fake functionality)

- Signaling/connection client TCP session + connection UI (C-CONNECTION
  next; codec/validation/framing for it are ready).
- Catalog UI, launch lifecycle (C-CATALOG).
- Remote window presentation, video surface (C-WINDOWS / C-PERF).
  Video transport is not integrated: WebRTC is a pinned-build task
  (root ADR-0009); the protocol carries no video in v0.1.0.
- Keyboard shortcut adaptation (FR-08) — next task.
- Power controls UI (FR-11/12).
- `.app` bundle packaging (MVP runs as a SwiftPM executable).

## Blocked

| Task | Blocker |
|---|---|
| End-to-end with the Windows Host | Windows side: MSVC build of the Host app pending (toolchain install on DESKTOP-PC, see root `docs/agent-runtime/TOPOLOGY_PREFLIGHT.md`) |

## Next tasks

1. C-CONNECTION: signaling client (TCP + `FrameReader`) + connection state in UI.
2. C-INPUT: shortcut adaptation stack with FR-08/§6.4 coverage.
3. C-WINDOWS/C-PERF: remote window registry + FPS policy.
