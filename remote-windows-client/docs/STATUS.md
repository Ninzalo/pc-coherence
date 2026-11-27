# macOS Client — STATUS

Last updated: 2026-11-27 (RW-C-004 C-INPUT shortcut adaptation stack —
887/887 Swift checks on `agent/C-INPUT/RW-C-004`; merged main stays at
`c5320e3` with 369/369 until integration).

## Implemented and verified

| Item | Evidence |
|---|---|
| SwiftPM package `RemoteWindows`: library `RemoteWindowsCore` + executable `RemoteWindowsApp` + test runner executable, macOS 13+ target | `swift build` exit 0 (commands below) |
| App bootstrap: `main.swift`, `AppDelegate`, `AppController` main status window (title, version, host address, connection state — all via L10n keys) | `swift build`; launch smoke test `tools/launch_smoke_test.sh`: "APP RUNNING after 4s" (killed cleanly) |
| **Signaling client + pairing flow (C-CONNECTION, task RW-C-001)**: `SignalingClient` state machine (disconnected/connecting/pairing/paired/rejected/locked) in `RemoteWindowsCore/Network/` with injectable transport/clock/timer; real TCP transport `NWSignalingTransport` (Network.framework); HELLO → challenge → 6-digit code (client-side format validation) → pairing_result (host safe_message surfaced, retry on rejection, no auto-retry when locked); auto `app_catalog_request` once after pairing (empty entries displayed honestly); 5 s heartbeat while connected (cancellable, last-activity exposed); any socket error resets to `.disconnected` — no stale connection | `SignalingClientSuite` 14 cases: 12 fake-transport cases + 2 loopback cases (real 127.0.0.1 socket, in-test POSIX host replying the canonical shapes; no LAN/external network) |
| **Connection/pairing UI in the AppKit status window**: editable host address field (prefilled from Preferences `host:port`, saved on connect), Connect/Disconnect buttons, pairing code field + Enter code button, status label with the real state (Disconnected / Connecting / Pairing / Paired / Rejected: \<safe_message\> / Locked), host-name label, catalog label ("No applications available yet" while the host catalog is empty). Every control performs a real action (plan rule 6); controls enable/disable per state | `swift build` exit 0; launch smoke test `tools/launch_smoke_test.sh`: "APP RUNNING after 4s" (killed cleanly) |
| **Catalog list UI with launch lifecycle (C-CATALOG, task RW-C-002)**: `SignalingClient.launchApp(appID:)` (paired-only, fresh `request_id`, one click = one `launch_app`, no retry/auto-resend, app_id only — never paths), `onLaunchResult`/`onAppStatus` callbacks (same delivery contract as `onCatalog`), `app_catalog_changed` now re-requests the catalog non-forced while paired; `CatalogLaunchModel` (Foundation-only, `RemoteWindowsApp/App/`) tracks the per-app phase idle→launching→started/failed with host `app_status` running/closed refinements (failed keeps its safe message; nothing invented client-side); AppKit catalog section replaces the count-only label: live `NSSearchField` filtering (case-insensitive substring), sorted view-based `NSTableView` (Name + Status, unavailable rows dimmed), Launch button enabled iff paired + selected entry available (blocked attempts surface `catalog.launchBlocked` + beep — no silent no-op), honest `catalog.empty` when empty, selection re-validated on wholesale catalog refresh, model + search reset on disconnect, all callbacks hop to main | `CatalogLaunchSuite` 8 fake-transport cases (launch_app shape/paired-only/fresh-ids, launch_result decode + unpaired-suppression, app_status running/closed, catalog_changed re-request non-forced + unpaired-no-op); GUI rendering NOT RUN (CLT, no display) — smoke-tested + code-review-verified |
| **Media channel + H.264 decode pipeline (C-MEDIA, task RW-C-003)**: `RemoteWindowsCore/Media/` — §9.4 binary framing (13-byte LE header, incremental `[UInt8]` reassembly; a terminal malformed header is recorded, not thrown, and the channel closes with the typed reason *after* delivering every packet reassembled before it, §9.6), AES-256-GCM via CryptoKit (16-byte tag at end of payload; `MediaStreamCipher` nonce = nonce_prefix ‖ frame_seq LE — the counter is never on the wire and the sender's `packet_counter` MUST equal the header `frame_seq`, §9.3/§9.4), §9.3 key derivation (`MediaKeys`), per-window stream state with gap counting (gaps counted, stream continues; unknown stream id = typed close, §9.4/§9.6), §9.2 handshake over TCP 47988 (32-byte session-key MAC over the 16-byte host nonce, typed `ClosedReason`s); `RemoteWindowsApp/Video/` — one `VTDecompressionSession` per `window_id` (Annex-B → AVCC, in-band SPS/PPS from the first keyframe per §9.5, `resetDecoderBeforeDecoding` on a keyframe after a gap) rendered into a real `CAMetalLayer` NSWindow per window; `window_announce`/`window_remove` drive stream + window lifecycle (paired-only), the media channel starts on pairing and tears down on unpairing or §9.6 failure (signaling session unaffected); session key derived on pairing acceptance, retained under the lock, never logged | 119 new checks, 6 new suites: MediaFraming 12 (split-packet reassembly, malformed-header deliver-then-close), MediaKeys 4 (golden `key-derivation.json` reproduced byte-exact), MediaAead 6 (golden `aes-256-gcm.json` reproduced byte-exact + tamper/short-input rejection), MediaHandshake 3 + MediaStream 2 (real 127.0.0.1 TCP loopback: accept/reject/truncated-nonce; decrypt/route/gap across 2 streams + malformed-packet `.malformedHeader` close; unknown-stream typed close), VideoConstruction 1 (real `CMVideoFormatDescription` + `VTDecompressionSession` from a valid SPS/PPS fixture); decoded pixel output NOT RUN (headless CLT) |
| **Keyboard shortcut adaptation (C-INPUT, task RW-C-004, FR-08)**: `RemoteWindowsCore/Keyboard/` — `KeyCodeTable` (kVK_* physical key codes verified against the CLT SDK `Carbon HIToolbox Events.h`; Windows VK + WIN_MOD constants inside the pinned contract domain `windows_vk` 1...0xFF / `modifier_bits` 0...0xF), `BuiltInShortcutCatalog` (the plan's exact 16-rule table: 12 on by default — copy/cut/paste/selectAll/undo/redo/find/save/open/newDoc/print/closeDoc; 4 off — quitApp/refresh/newTab/saveAs), `CustomShortcutStore` (versioned `keyboard.customRules` blob + separately persisted `keyboard.builtInOverrides`; future version / corrupt data → stored bytes left untouched + honest report), `ShortcutRuleValidator` (typed errors: empty trigger, modifier-only trigger, empty target, unsupported target/source VK, duplicate source naming the conflicting rule — never silent), `KeyboardEventTranslator` (ordered emission: modifier-downs Shift/Ctrl/Alt/Win before the key, key-up, modifier-ups in reverse, with at-event-time modifier bits; baseline mac→VK translation for unmatched typing), `ShortcutAdaptationService` (the pure seam: `translate(keyCode:modifierFlags:) -> .adapted([KeyInput]) / .passthrough / .consumed`; physical-code + exact-modifier matching, custom-over-built-in priority, in-flight chord tracking so source key-ups are consumed, auto-repeat re-fires the matched target, `reset()` clears tracked key state); `KeyboardShortcutsSettingsModel` (Foundation-only, unit-testable UI logic: master/built-in/custom toggles, custom CRUD + reorder, validation surfaced) | 31 new cases / 518 checks in `ShortcutAdaptationSuite`: exact 16-rule table (IDs, triggers, targets, defaults), per-enabled-rule matching, default-off rules, custom-over-built-in + disabled-custom precedence, master-off passthrough without deleting rules, pinned literal emission sequences (Ctrl+Shift+S 6-event, Alt+F4 4-event, Ctrl+C 4-event) + pairing/domain invariants, chord-key consumption, reset() on focus loss/disconnect + idempotency, repeat behavior, baseline path for unmatched typing (no clipboard), all validator rejections, store round-trip / future-version / corrupt / override-separation, model CRUD + enable-conflict blocking, L10n key resolution for all new `shortcuts.*` keys |
| Localization: `L10n` helper + `Resources/Localizable.en.json` (English-only MVP, FR-14); all C-CONNECTION and C-CATALOG strings as L10n keys (`status.pairing/paired/rejected/locked`, `connect.button`, `pairingCode.*`, `catalog.*` incl. 12 C-CATALOG keys, ...) | `L10nSuite` 5 cases pass (incl. `connectionStringsResolve`, `catalogStringsResolve` — 12 new key checks) |
| Localization (continued, RW-C-004): all new `shortcuts.*` L10n keys in `RemoteWindowsCore/Resources/Localizable.en.json` (section title, master toggle, interception note, per-built-in names, custom CRUD labels, 6 validation messages, 2 store-warning messages — additions only, no existing key touched) | covered by `ShortcutAdaptationSuite.l10nShortcutKeysResolve` (50 keys checked) |
| **Keyboard Shortcuts settings UI (C-INPUT, task RW-C-004)**: `RemoteWindowsApp/Settings/KeyboardShortcutsSettingsView.swift` (AppKit renderer of the settings model: master checkbox, plan-mandated interception-limit disclosure, built-in table with per-rule toggles + `⌘X`/`Ctrl+X` columns, custom table with Add/Edit/Delete/Move-up/Move-down + enable toggles, capture-field source editor + target picker with live source→target preview; validation messages shown, never swallowed) + minimal `AppController` wiring (section mount via a toggle button; `service.reset()` on disconnect and on window focus loss — FR-07/8.1 input-state hygiene) | `swift build` exit 0; UI rendering NOT RUN (headless CLT, no display) — model logic unit-tested, rendering code-review-verified |
| Preferences: versioned UserDefaults model (`inactive_fps` default 10, range 1–60; host address/port 47987; master shortcut toggle; `settingsSchemaVersion = 1`) | `PreferencesSuite` 3 cases pass |
| Pinned protocol contract snapshot v0.1.0 in `Protocol/Contract/` (27 files incl. the RW-H-006 media golden vectors `test-vectors/media/`, SHA256SUMS-verified) via `tools/sync_protocol_from_host.sh`; provenance `single-repo:<commit>` | `shasum -a 256 -c SHA256SUMS` → 27 OK (re-synced 2026-11-24 at host commit `d270d15`; sync tool updated to include `test-vectors/` subdirectories) |
| Protocol adapter (`RemoteWindowsCore/Network/`): envelope JSON codec, semantic validation (1:1 port of Host `core/protocol_validation.cpp`, same error codes/bounds), framing (uint32 BE, 8 MiB cap, incremental `FrameReader`) — reused unchanged by the signaling client | `ProtocolCodecSuite` (20 cases), `ProtocolVectorSuite` (all 22 canonical vectors), `FramingSuite` (6 cases) pass |
| Cross-language golden set | same 22 vectors execute in the Host C++ suite and the Swift suite |
| No pairing secrets persisted (codes/keys are never written to disk or UserDefaults in this task; Keychain storage is a later task) | code review of the diff: no persistence of `one_time_code`/nonces/session material |

## Last successful build (this tree, Mac mini)

Commands (worktree `/Users/nnzlgg/pc-coherence-rw-c-004`, branch
`agent/C-INPUT/RW-C-004`, base `cccd7f5`):
```
cd remote-windows-client
swift build
swift run RemoteWindowsAppTests        # 887 checks, exit 0
```
Result: 2026-11-27, macOS 27.2.1, Swift 6.4.2 (Command Line Tools —
no full Xcode), SwiftPM. Build clean (exit 0).
**887/887 test checks PASSED, 0 failures** (exit code 0) — 369
baseline (merged main @ `c5320e3`: 250 C-CATALOG + 119 C-MEDIA) +
518 new C-INPUT checks (31 new cases in `ShortcutAdaptationSuite`,
suite count 13 → 14).

Pre-merge main tree (unchanged by this task): 369/369 @ `c5320e3`
(2026-11-25), launch smoke on the merged main tree: process running
after 4 s (pid 18926), terminated cleanly.

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
3. Swift 6.4.2 CLT hazards found in C-MEDIA (recorded in code comments,
   2026-11-25): (a) a `break` inside a `switch case` exits the switch,
   not the enclosing `while` — `MediaFrameReader.feed` keeps its loop
   guards directly in the while body (a misplaced guard spun forever on
   a partial header); (b) mutating long-lived `Data` (`removeFirst`) or
   `subdata(in:)` on CryptoKit-seal-derived buffers corrupts the buffer
   so later subscripts trap EXC_BREAKPOINT — hot-path byte work uses
   `[UInt8]`/`ArraySlice` + fresh `Data` construction.

## NOT implemented yet (honest list — no fake functionality)

- Pixel-verified remote window display (C-WINDOWS / C-PERF display):
  the C-MEDIA pipeline (RW-C-003 @ `c5320e3`) opens one real NSWindow
  per `window_id`, decodes H.264 and blits to a `CAMetalLayer` — but
  decoded pixel output is NOT RUN headless (no display verification),
  and there is no media-channel auto-reconnect after a §9.6 failure
  (the channel tears down until re-pairing). The v0.1.0 control
  protocol carries no video (by design).
- Keyboard shortcut adaptation (FR-08) — **implemented and unit-tested
  on `agent/C-INPUT/RW-C-004` (RW-C-004, not yet merged)**. Still NOT
  RUN: live input-pipeline integration (the service is a pure seam;
  applying it while a remote window is the active input target and
  emitting `input_event` frames requires C-WINDOWS + H-INPUT per the
  plan's dependency gate), and visual verification of the settings UI
  (headless CLT, no display).
- Power controls UI (FR-11/12).
- Pairing-secret persistence in the Keychain (later security task;
  RW-C-001 deliberately persists nothing).
- `.app` bundle packaging (MVP runs as a SwiftPM executable).

## LAN end-to-end pairing — verified (2026-11-24)

Real LAN run, macOS Client (192.168.100.156, this machine) ↔ Windows
Host (DESKTOP-PC, 192.168.100.139:47987), after the Host tray
no-icon bug was root-caused and fixed (Host `c4f9250`, see Host
STATUS.md "Tray window-creation failure").

| Item | Evidence |
|---|---|
| Host tray icon in the user's interactive session | Host log: `13:47:08Z event="host.start" port="47987"` then `event="tray.icon"` (success path — the non-interactive SSH session logs `tray.icon_failed` instead) |
| Connection + pairing over the LAN | Host log: `13:47:26Z session.hello peer="192.168.100.156:50205" state="pairing"` → `13:47:38Z session.pairing ... accepted="true" state="paired"` → `session.message kind="app_catalog_request" state="paired"` |
| Liveness | `session.message kind="heartbeat" state="paired"` every 5 s, continuous for 4+ minutes after pairing |
| Client UI state (user-confirmed) | `Connection: Paired`, `Host: DESKTOP-PC`, `No applications available yet` (correct: the Host catalog scanner is not implemented yet, so the catalog is honestly empty) |
| Earlier same-day run | First pairing attempt also succeeded: `13:41:57Z session.pairing ... accepted="true"` (same evidence chain) |

Notes: the pairing code was shown on the Host via the tray
"Show pairing code..." dialog and entered in the Client by the user
(FR-13 flow, as designed). The Client's default `127.0.0.1` was
changed to `192.168.100.139` in the host-address field for the test
(the field is editable and persists to Preferences).

## Blocked

(none — the LAN e2e blocker is resolved, evidence above)

## Next tasks

1. Merge `agent/C-INPUT/RW-C-004` → main (orchestrator), then the
   input-pipeline stage (C-WINDOWS window tracking + H-INPUT): apply
   `ShortcutAdaptationService.translate` only while a remote window is
   the active input target and emit `.adapted` sequences as
   `input_event` key frames (the documented seam; FR-08 8.1 gate).
2. C-MEDIA follow-ups (post-integration): pixel-output verification of
   the decode/render path when a display is available (NOT RUN
   headless), and a media-channel reconnect policy after a §9.6 failure
   (currently: teardown until re-pairing).

## Notes

- Write-scope note (RW-C-001): the task's directory-level write scope listed
  `Sources/RemoteWindowsCore/Network/`; task requirement 6 explicitly mandates
  the new user-facing strings as L10n keys in
  `Sources/RemoteWindowsCore/Resources/Localizable.en.json` (plan rule 3), so
  that file was updated as directed. No files outside the Client tree were
  modified; the protocol snapshot was not touched.
