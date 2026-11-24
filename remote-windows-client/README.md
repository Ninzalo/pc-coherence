# remote-windows-client — macOS Client

The **macOS Client** for the Remote Windows product: connects to a running
**Windows Host** on the LAN, displays the application catalog, launches
applications by `app_id`, presents each streamed Windows window as a
separate native `NSWindow`, adapts keyboard shortcuts, and exposes window,
performance, and power controls.

Layout note: the plan calls for two Git repositories. Per explicit owner
instruction (root `docs/decisions/ADR-0008-single-repo-layout.md`) this
project lives in one repository (`Ninzalo/pc-coherence`) containing both
product trees as independent subtrees. All other plan rules apply unchanged.

## Layout

```
Package.swift                 SwiftPM manifest (Core library + app + test runner)
Sources/RemoteWindowsCore/
  Network/                    protocol adapter, codec, validation, framing (C-CONTRACT/C-CONNECTION)
  L10n/ + Resources/          L10n helper + Localizable.en.json (English-only MVP)
  Settings/                   Preferences (versioned UserDefaults model)
Sources/RemoteWindowsApp/
  App/                        bootstrap, main window, app controller
  (feature UI modules — Catalog, RemoteWindows, Keyboard, Power — land with their tasks)
Tests/RemoteWindowsAppTests/  test runner executable + harness + suites
Protocol/Contract/            pinned snapshot of the Host protocol (never hand-edited)
tools/sync_protocol_from_host.sh
tools/launch_smoke_test.sh
docs/                         STATUS, ARCHITECTURE, DEPENDENCIES, ...
```

## Build and test (Mac mini)

```sh
cd remote-windows-client
swift build
swift run RemoteWindowsAppTests   # 83 checks; exit 0 = all pass
```

Requires: macOS with Command Line Tools (Swift 6.x). No Xcode project is
needed for the MVP build path. NOTE: this CLT-only environment has no
XCTest framework, so the suites run through the plain executable test
runner instead of `swift test` (see `docs/STATUS.md` and
`docs/decisions/ADR-0001-client-build-system-swiftpm.md`).

## Protocol contract

This tree consumes a **pinned, checksum-verified snapshot** of the
Host-owned canonical schema in `Protocol/Contract/`. Update it only via:

```sh
tools/sync_protocol_from_host.sh
```

Never hand-edit files under `Protocol/Contract/`. Wire format and
validation rules: `remote-windows-host/docs/PROTOCOL.md` (normative) and
`docs/CLIENT_PROTOCOL_INTEGRATION.md`.
