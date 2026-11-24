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
- [x] Windows runner toolchain present (verified 2026-11-24 via SSH):
  Visual Studio Build Tools 2026 18.8.2 (`18.8.12023.21`), MSVC
  14.51.36231, Windows SDK 10.0.26100.0 (headers + libs under
  `C:\Program Files (x86)\Windows Kits\10`), git 2.54.0 — installed on the
  PC on 2026-07-29, before this project (an earlier check that reported
  "no MSVC" was wrong: `cl`/`vswhere` are not on PATH and need the VS
  developer environment). **CMake and Ninja are absent** and are being
  installed as user-level binaries (no admin rights required).
  NOTE: the scheduled task `rwh_vs_install` (VS Build Tools install) is
  obsolete now — it was created before the existing install was found and
  its last run exited 1 on UAC; it is being deleted.
- [ ] Windows runner builds a harmless test project and reports tool
  versions — **IN PROGRESS**: cmake/ninja install + dev-env compile
  smoke test pending (toolchain itself confirmed present).
- [ ] Specific Host commit buildable by the runner with exact-SHA
  verification — **NOT STARTED** (toolchain ready; awaiting cmake/ninja).
- [x] Mac mini builds/launches/tests the macOS Client — verified
  2026-11-24: `swift build` exit 0; `swift run RemoteWindowsAppTests`
  83/83 checks pass (exit 0); `tools/launch_smoke_test.sh` — app running
  after 4 s, killed cleanly. Packaging (`.app` bundle) remains a
  follow-up; the MVP runs as the SwiftPM executable.
- [x] Client launch/test on Mac mini — same evidence as above.
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
- Client build without full Xcode: SwiftPM (`swift build` /
  `swift run RemoteWindowsAppTests`) with AppKit from the CLT SDK
  (recorded in client `docs/decisions/ADR-0001-client-build-system-swiftpm.md`);
  the CLT install has no XCTest framework, so suites run as a plain
  executable with a minimal harness (portable 1:1 to XCTest).
