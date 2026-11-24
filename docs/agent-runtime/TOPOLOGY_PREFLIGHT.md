# LAN Topology Preflight

Required by plan Section 18.8. Verified on 2026-11-24; no credentials stored.

## Machine roles (Section 18.1)

| Machine | Observed state |
|---|---|
| Mac mini (dev/orchestration + primary Client target) | macOS 27.2.1; Swift 6.4.2 (Command Line Tools only — no full Xcode); CMake 4.5.1; Ninja 1.13.2; Apple clang; runs Hermes v0.23.3 + local LLM |
| Windows 11 Pro PC | DESKTOP-PC, 192.168.100.139 (DHCP), Windows 10.0.26300.10075; user `Ninzalo` (Administrators); console session active; ~1.7 TB free on C:; ~56 GB RAM |
| MacBook | Not present / not recorded — secondary target remains optional, not blocking |

## 18.8 checklist

- [x] Hermes version + local model/provider confirmed on Mac mini
  (v0.23.3, Qwen3.8-27B-GGUF:UD-Q4_K_XL via loopback custom endpoint; no
  cloud fallback). See `LOCAL_MODEL_PREFLIGHT.md`.
- [x] Windows PC and Mac mini IPs, OS, roles recorded (above).
- [x] Mac mini accesses the working Git repository locally
  (`/Users/nnzlgg/pc-coherence`, branch `main`).
- [x] Mac mini authenticates to the Windows PC over the approved LAN
  channel: OpenSSH, key `~/.ssh/id_ed25519_pc`, `BatchMode=yes` success.
- [ ] Windows runner builds a harmless test project and reports tool versions
  — **IN PROGRESS**: no MSVC/Windows SDK present on 2026-11-24
  (`vswhere`/`cl`/`cmake`/`ninja` absent; only Git at `C:\Programs\Git`).
  Visual Studio Build Tools 2022 (workload `VCTools`, `--includeRecommended`,
  quiet) install launched on the PC 2026-11-24 via scheduled task
  `rwh_vs_install` (interactive session; owner approves the UAC prompt).
- [ ] Specific Host commit buildable by the runner with exact-SHA
  verification — **BLOCKED** on the install above.
- [ ] Mac mini builds/launches/packages the macOS Client — **NOT STARTED**:
  the `remote-windows-client/` tree does not exist yet (first scaffold task).
- [ ] Client launch/test on Mac mini — **NOT STARTED** (same).
- [ ] MacBook smoke test — N/A (device not available; recorded, not blocking).
- [x] Product runtime path is direct Windows PC ↔ Client process; Hermes is
  never a media relay (design rule; no relay code exists in the tree).
- [ ] Pairing/launch/streaming/input with Internet disconnected —
  **NOT RUN** (no Client, no built Host app yet).
- [x] Stopping Hermes never stops a Client session — by construction
  (separate processes; no IPC/relay coupling exists in the tree).
- [ ] Firewall rules scope — pending runner work (see host `docs/SECURITY.md`
  for intended product ports: TCP 47987 signaling/control, LAN interface only).
- [x] Power actions are mock-tested only; no real SLEEP/SHUTDOWN run on the
  only runner without explicit approval (policy held).

## Deviation record

- Single-repository layout (plan mandates two repos): owner instruction,
  ADR-0008 in root `docs/decisions/`.
- Client build without full Xcode: SwiftPM (`swift build` / `swift test`)
  with AppKit/SwiftUI from the CLT SDK (recorded in ADR-0009); an Xcode
  project file remains a documented follow-up if packaging requirements
  demand it.
