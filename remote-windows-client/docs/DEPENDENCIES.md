# macOS Client — Dependencies

## External dependencies

None. The MVP scaffold uses only the macOS SDK (Foundation, AppKit) via
SwiftPM. No third-party packages are declared in `Package.swift`.

## Planned (not yet integrated)

| Dependency | Purpose | Pin status |
|---|---|---|
| WebRTC (native) | video transport (post-v0.1.0) | NOT pinned — requires a reproducible pinned build per plan §11; a multi-day dependency build. See root `docs/agent-runtime/TOPOLOGY_PREFLIGHT.md` and Host `docs/DEPENDENCIES.md` |

Rules (plan §11): every future dependency must be pinned to an exact
version/commit, with license, build procedure, and the statement that it
requires **no Internet access at runtime**. Runtime must never download
anything.

## Toolchain (build machine: Mac mini)

- Swift 6.4.2 (Apple Command Line Tools; no full Xcode in this environment)
- SwiftPM as the build/test runner (`swift build`, `swift test`)
- macOS 27.2.1 host; deployment target macOS 13
