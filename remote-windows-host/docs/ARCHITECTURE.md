# Windows Host — Architecture

## Principles

1. **Core is platform-independent.** Everything testable without Win32
   lives in `core/` (C++20, no platform APIs): protocol model + JSON codec
   + validation, catalog model, window relationship logic, key mapping
   tables, FPS gating, power/pairing state machines, authorization policy.
   `core/` must build on macOS and Linux so its tests run in the
   development environment.
2. **Win32 layer is thin.** `win32/` contains adapters that call Win32/
   WGC/SendInput/PowrProf and feed the core through interfaces. No
   business rules in `win32/`.
3. **Protocol is a contract.** The .proto in `protocol/` is canonical;
   both peers validate every frame per `docs/PROTOCOL.md`; tests share the
   golden vectors in `protocol/test-vectors/`.
4. **No arbitrary execution.** The protocol has no command/path/PID fields
   (see PROTOCOL §9). Power actions are typed enums with privilege checks.

## Module map

```
app/                    Windows app shell: main, AppController, TrayController
core/protocol_types     typed envelope + payloads (mirror of the .proto)
core/protocol_json      JSON codec (nlohmann::json), framing rules
core/protocol_validation  per-field + per-kind validation (PROTOCOL §5)
core/protocol_version   semver parse + compatibility
core/sha256             self-contained SHA-256 (pairing key derivation, bundle checksums in tests)
core/catalog            AppRecord, normalization, stable app_id, dedupe
core/window_relationships  session graph, announce decisions, modality
core/input              macOS->Windows key tables, KeyStateTracker
core/fps_policy         FrameRateGate (pre-encode gate)
core/power_policy       typed request state machine, no auto-retry
core/pairing            one-time code + nonce, attempt lockout, key derivation
core/authorization      pre/post-pairing kind gates
network/                TcpSocket (POSIX/Winsock), frame codec,
                        SignalingServer, SignalingClient, rwh-signaling-probe
win32/capture           WGC (raw COM) session per window
win32/windows           WindowRegistry + WinEvent monitor (fills core events)
win32/input             SendInput dispatch of validated InputEvents
win32/power             privilege check + SetSuspendState / InitiateSystemShutdownExW
win32/catalog           StartMenu scanner (IShellLinkW), user entries, launcher
resources/en-US.rc      all user-facing strings (English only, MVP)
```

## Data flow (v0.1.0, LAN-only)

```
[macOS Client] --TCP 47987 (length-prefixed JSON envelopes)--> [Windows Host]
  pairing (one-time code shown on host screen)
  catalog push, launch by app_id
  window announce/update/remove (host-driven)
  input events (client->host), resize (client->host, acked)
  power (typed, paired+authorized only)
```
Video: not transmitted in v0.1.0. When integrated, frames will travel on
WebRTC media tracks directly Host↔Client (no relay); the v0.1.0 TCP channel
remains signaling/control only.

## Threading (Host)

- SignalingServer: accept thread + one worker thread per connection
  (max 16).
- AppController: main UI thread (tray) + registry thread (WinEvents).
- Capture: per-window capture callback; FrameRateGate gates before encode.
- All cross-thread state is mutex-protected; no locks held across Win32
  calls where avoidable.
