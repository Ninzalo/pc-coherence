# PC Coherence — Remote Windows for macOS

Run applications on a Windows 11 Pro PC and work with them as native macOS
windows on a Mac mini (or a MacBook), directly over the LAN, with no required
Internet access.

Two products, two self-contained source trees in one Git repository
(see `docs/decisions/ADR-0007-single-repo-layout.md` for the deviation from
the two-repository rule of the plan):

- `remote-windows-host/` — the **Windows Host** (C++20, MSVC/Windows SDK).
  Owns the canonical protocol contract in `remote-windows-host/protocol/`.
- `remote-windows-client/` — the **macOS Client** (Swift, AppKit/SwiftUI,
  Swift Package Manager). Pins a checksum-verified snapshot of the Host
  contract in `remote-windows-client/Protocol/Contract/`.

Canonical development plan:
`remote-windows-host/docs/development-plan/RemoteWindows_Development_Plan_MacMini_Client_Supported.md`.

Product naming (mandatory): the Windows application is the **Windows Host**,
the macOS application is the **macOS Client**.

## Status

`remote-windows-host/docs/STATUS.md` and `remote-windows-client/docs/STATUS.md`
are updated after every stage with verified results only (PASS / FAIL / NOT RUN
/ BLOCKED).

## Quick start (Mac mini)

Windows Host portable core + signaling (full app targets build on Windows only):

```sh
cmake -S remote-windows-host -B remote-windows-host/build
cmake --build remote-windows-host/build -j
ctest --test-dir remote-windows-host/build --output-on-failure
```

macOS Client (build + unit tests):

```sh
cd remote-windows-client
swift build
swift test
```

Run the Client: `swift run RemoteWindowsApp`, then pair it with a running
Windows Host (see `remote-windows-client/README.md`).

## Runtime requirements

- One Windows 11 Pro PC (Windows Host) and one supported macOS machine
  (macOS Client) on the same LAN.
- No Internet required: no STUN/TURN/SFU, no cloud services, no telemetry.
- Product runtime port: TCP **42117** on the Windows Host (LAN-scoped).
  See `remote-windows-host/docs/SECURITY.md` and
  `remote-windows-client/docs/SECURITY.md`.
