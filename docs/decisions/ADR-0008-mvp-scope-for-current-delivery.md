# ADR-0008: Scope of the first full-project delivery

- Status: Accepted (2026-11-23)
- Deciders: Hermes orchestrator, pending owner review

## Context

Environment survey on the Mac mini (2026-11-23):

- The Windows PC (DESKTOP-PC, 192.168.100.139, Windows 11 24H2) is reachable
  over the approved SSH channel, but no MSVC/Visual Studio toolchain was
  detected on it (`vswhere` and `cl` not found).
- The Mac mini has Swift 6.4.2 with Command Line Tools only (no full Xcode);
  SwiftPM `swift build` / `swift test` work, and AppKit/SwiftUI compile from
  the SDK.
- No `protoc`/protobuf toolchain is installed.

The plan forbids claiming platform builds or tests that were not actually
run, and forbids fake functionality.

## Decision

The first delivery ("fully create the project") is:

1. Complete project structure for both products (all modules named in the
   plan's layouts), with the canonical protocol schema v0.1.0.
2. All platform-independent logic implemented and unit-tested on macOS:
   protocol model/validation, catalog normalization, window-relationship
   rules, FPS gating, power policy, pairing state machine, signaling
   server/client (portable C++), and the full Client keyboard-shortcut
   adaptation stack.
3. Win32-only Host sources (capture, window registry Win32 glue, input,
   power, catalog scanner, tray app) written against documented APIs but NOT
   compiled here — recorded as NOT RUN, with the MSVC build recorded as a
   blocked task pending toolchain installation on the runner.
4. Video transport: interface + honest placeholder rendering on the Client;
   WebRTC integration is a recorded next task (needs a pinned WebRTC build,
   which is a multi-day dependency build, per the plan's WebRTC build note).

## Consequences

- `docs/STATUS.md` in both trees lists exact PASS / NOT RUN / BLOCKED items
  with commands and results.
- No Windows-side behavior is claimed as verified until the runner builds
  and tests a specific commit.
- The placeholder video surface is labeled as such in the UI (no fake
  functionality shipped).
