# ADR-0001 (client): SwiftPM as the Client build system

Date: 2026-11-24 · Status: accepted

## Context

The plan names Xcode for the macOS Client. The Mac mini (primary
development/build/packaging machine) has Swift 6.4.2 with Command Line
Tools only — no full Xcode — and `swift build` / `swift test` work, with
AppKit/SwiftUI compiling from the SDK.

## Decision

Build the Client as a SwiftPM package (one executable target
`RemoteWindowsApp` for the MVP scaffold, test target
`RemoteWindowsAppTests`). No `.xcodeproj` is checked in yet.

## Consequences

- Fast, scriptable, CI-friendly builds on the Mac mini:
  `swift build` / `swift run RemoteWindowsAppTests`.
- **Test runner deviation (verified 2026-11-24):** the CLT install has no
  XCTest framework (`import XCTest` and `import Testing` both fail to
  resolve; XCTest ships with Xcode). The suites therefore run as a plain
  SwiftPM executable (`RemoteWindowsAppTests`) with a minimal assertion
  harness (`Tests/RemoteWindowsAppTests/Harness.swift`), exit code 0 =
  all pass. The same cases port 1:1 to XCTest when Xcode is available;
  this ADR would be superseded at that point.
- `.app` bundle packaging (icon, Info.plist, signing) is a follow-up
  task; the MVP runs as the SwiftPM executable.
- Strict concurrency (Swift 6 mode) is deferred: targets compile in
  Swift 5 language mode for now; adoption is a tracked follow-up.
