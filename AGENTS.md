# PC Coherence — Agent Rules

Coordinating agent: Hermes Agent on the Mac mini (see
`docs/agent-runtime/LOCAL_MODEL_PREFLIGHT.md`). This file applies to any agent
(or human) making changes in this repository.

## Identity

- One Git repository, two product trees (ADR-0007). The plan text lives in
  `remote-windows-host/docs/development-plan/RemoteWindows_Development_Plan_MacMini_Client_Supported.md`.
- Product names are fixed: **Windows Host** (Windows) and **macOS Client**
  (macOS). Never "Windows Agent" or "macOS Host".
- Protocol contract: canonical `.proto` in `remote-windows-host/protocol/`
  only. The Client holds a pinned, checksum-verified snapshot in
  `remote-windows-client/Protocol/Contract/`, updated only via
  `remote-windows-client/tools/sync_protocol_from_host.sh`.

## Single-writer ownership

| Path | Owner role |
|---|---|
| `remote-windows-host/protocol/` | CONTRACT |
| `remote-windows-host/core/`, `remote-windows-host/network/` | H-FOUND / H-NET |
| `remote-windows-host/win32/`, `remote-windows-host/app/` | H-* module owners |
| `remote-windows-client/Sources/RWProtocol/` | C-CONTRACT |
| `remote-windows-client/Sources/RWKeyboard/` | C-INPUT |
| `remote-windows-client/Sources/RWCore/` | C-WINDOWS / C-PERF / C-POWER |
| `remote-windows-client/Sources/RemoteWindowsApp/` | C-FOUND + module owners |

Do not edit another owner's paths opportunistically. Raise an interface
change request instead.

## Commits

Conventional Commits (`feat`, `fix`, `docs`, `chore`, `test`, `refactor`,
`build`, `ci`) with scope where useful, e.g. `feat(protocol): add v0.1.0
control schema`. Push each commit to `origin/main`
(`git@github.com:Ninzalo/pc-coherense.git`).

## Build/test (Mac mini)

```sh
cmake --build remote-windows-host/build -j && ctest --test-dir remote-windows-host/build --output-on-failure
cd remote-windows-client && swift build && swift test
```

Win32 app targets do NOT compile on macOS. Record them as NOT RUN — never as
PASS.

## Hard rules

1. Never commit secrets (pairing keys, certificates, tokens). `.gitignore`
   covers common forms; do not add new secret-bearing files to the repo.
2. Never report an unrun test as passed. Missing evidence = NOT RUN/BLOCKED.
3. User-facing text goes through centralized resources only
   (`remote-windows-client/Sources/RemoteWindowsApp/Resources/Localizable.en.json`
   + `L10n` helper; Windows Host uses `.rc` string resources).
4. Keep `docs/STATUS.md` of the affected tree updated in the same change that
   changes behavior.
5. No arbitrary remote command execution anywhere in the protocol.
