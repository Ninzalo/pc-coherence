# macOS Client — Architecture

One SwiftPM package, one executable target (`RemoteWindowsApp`) for the MVP
scaffold, organized by responsibility directories matching the plan's
module layout (§4.2). Subdirectories can be split into separate SwiftPM
targets/products later without behavior changes.

## Layers

```
UI (AppKit, SwiftUI views later)
  App/, Catalog/, RemoteWindows/ (views), Power/, Settings/
Services (platform-independent where possible; unit-tested)
  Keyboard/   shortcut adaptation (FR-08)
  Network/    protocol adapter (codec + validation + framing), signaling
  Performance/ FPS policy (FR-09)
  Settings/   Preferences (versioned UserDefaults)
Contract (pinned, checksum-verified; never hand-edited)
  Protocol/Contract/  snapshot of remote-windows-host/protocol/
```

## Design rules

- **Client-side shortcut adaptation** (root ADR-0005 in the Host tree):
  rules live entirely in this tree; the Host only receives normalized
  Windows key events.
- **Protocol**: v0.1.0 wire = length-prefixed JSON envelope (Host ADR-0002).
  The Swift codec and validation mirror the Host's portable core exactly;
  the 22 canonical test vectors are the shared golden set.
- **No video in v0.1.0**: media will use WebRTC once the pinned build is
  integrated (root ADR-0009); until then the UI shows no fake video.
- **Localization**: `L10n` + `Localizable.en.json` only (FR-14).
- **Secrets**: pairing key to Keychain at runtime; UserDefaults holds
  non-sensitive settings only (docs/SECURITY.md).
