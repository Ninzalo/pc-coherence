# Remote Windows for macOS — Development Plan and Hermes Agent Execution Rules

**Document purpose:** MVP product specification and mandatory execution policy for Hermes Agent.  
**Language:** English. **MVP UI:** English, localization-ready.  
**Machines:** Mac mini runs Hermes/local LLM and is the primary macOS Client development, build, test and packaging machine. One Windows 11 Pro PC is the Windows Host target and native build/test runner. A MacBook is an optional secondary Client compatibility target.  
**Runtime constraint:** Windows Host and whichever Mac runs the Client communicate directly over the LAN. No Internet, cloud service, Hermes relay or third-party server is required at product runtime.

---

# 0. Mandatory Hermes Execution Policy — Highest Priority

These rules govern every development, maintenance, build, test, review and integration task in this project. They override any conflicting workflow suggestions elsewhere in this document.

## 0.1. The main conversation MUST delegate all work

**The top-level Hermes session is an orchestrator only. It MUST use the `delegate_task` tool to assign every actual task to a subagent.** This is mandatory, not optional and not merely a preference.

The main conversation must not perform implementation or operational work itself. It must not directly:

- edit, create, delete, move, format or generate project files;
- run shell/terminal commands, scripts, Git commands, builds, tests, linters or benchmarks;
- inspect repositories or source files by using file/terminal tools;
- connect to Windows, run SSH/PowerShell commands, or operate the remote build runner;
- perform manual integration, merging, cherry-picking, deployment or packaging.

All such work—including repository inspection, planning that requires repository evidence, documentation edits, code changes, research, API verification, Git operations, builds, tests, debugging, security review, QA, integration and final acceptance checks—must be assigned through `delegate_task` to a suitable subagent. Use subagents for small tasks too. Do not make an exception because a task appears trivial.

The top-level session may only:

1. Maintain the overall task backlog and dependency order.
2. Break work into bounded task packets and call `delegate_task`.
3. Monitor delegated work, obtain status and request a bounded continuation or correction through another delegation.
4. Evaluate returned reports and evidence, decide whether acceptance criteria are met, and assign the next task.
5. Communicate blockers, decisions and the final status to the user.

Even integration and acceptance work must be delegated. The main session may decide whether reported evidence is sufficient, but it must not run commands or modify files to produce that evidence.

**Mandatory behavior:** Before doing anything that would constitute project work, call `delegate_task`. Never silently fall back to doing the work in the main conversation. If `delegate_task` is unavailable, errors, or cannot create a usable child, stop the affected work and report it as `BLOCKED`; do not perform it yourself.

A subagent must not be assumed to have completed a task merely because it was started. Wait for its report using bounded monitoring, inspect the returned evidence, and then continue. Keep one active implementation child at a time by default because all agents use the same local inference capacity. Do not delegate nested subagents unless explicitly proven necessary and supported by the installed Hermes configuration; the top-level session remains the sole coordinator.

## 0.2. Mandatory `windows-ssh-remote` skill for every Windows operation

**Whenever a task involves the Windows PC, Windows Host, Windows build/test runner, or any remote Windows operation, the responsible subagent MUST use the `windows-ssh-remote` skill.** This includes, without exception:

- connecting to or checking the Windows PC;
- inspecting Windows files, processes, services, environment, SDKs or tool versions;
- running PowerShell, CMD, scripts, Git or build/test commands on Windows;
- copying or synchronizing source, artifacts, logs or test reports;
- starting, monitoring, stopping or diagnosing remote processes;
- configuring or troubleshooting Windows SSH, firewall rules or the build runner.

Before the first Windows operation in a task, the subagent must locate, read and activate/trust `windows-ssh-remote` as required by the installed Hermes version, then follow its instructions for the entire task. The task packet must explicitly name this skill and make its use an acceptance requirement. Do not substitute a generic terminal, a manually constructed SSH command, another remote tool, or an assumed connection method for the skill's documented workflow. Do not perform Windows work if the skill is missing, unavailable or not trusted; report the blocker and continue with independent macOS-only tasks.

The Windows runner remains a restricted development interface. Use its documented/allow-listed actions and key-based authentication. Do not expose arbitrary command execution as a product feature or weaken Windows security to make the workflow easier. Never put credentials, private keys or tokens in Git or task reports.

## 0.3. Process supervision — no infinite waits or orphaned processes

The assigned subagent is responsible for the full lifecycle of every process it starts, locally or remotely. **No command, build, test, SSH operation, delegated task or status poll may wait indefinitely.**

For every potentially long-running command or remote job:

1. Define an appropriate hard timeout and expected completion condition in the task packet before starting it. Choose a realistic limit for the operation; do not use an unlimited timeout.
2. Prefer synchronous/foreground execution when supported. If the skill or runner starts a background job, record its process/job ID, start time, log location and owning session immediately.
3. Poll at bounded intervals using the process-monitoring method supported by the relevant skill/tool. Every poll must have a finite timeout. Do not use an unbounded `wait`, tight polling loop or indefinite retry.
4. Check for real progress: process state, elapsed time, output/log growth, CPU/build/test activity and last meaningful event, where available.
5. If the hard deadline is reached, or the process shows no meaningful progress for the task's defined no-progress interval, stop waiting. Attempt graceful cancellation, then use the documented force-stop method after a finite grace period if safe.
6. Collect final logs, exit code/state and any partial artifacts. Confirm whether the process actually stopped; do not assume a disconnected SSH session killed the remote process.
7. Report `TIMEOUT`, `STALLED`, `CANCELLED`, `FAILED` or `COMPLETE` accurately. Never report an incomplete process as complete.

Do not start background builds/tests and return from a child while leaving the parent unable to track them. Never launch duplicate builds because a first command appears slow or its SSH response was delayed. Before retrying, establish the state of the original job. Avoid orphan processes and stale locks. For a remote disconnect, reconnect through `windows-ssh-remote` and check the original job before starting another.

If a command has no safe cancellation mechanism, do not launch it unattended unless the task packet defines a finite recovery procedure and the user has authorized any required risk. A timeout is a result to report, not a reason to wait forever.

## 0.4. Execute the entire project backlog without asking for stage-by-stage permission

The goal is to complete **all** project tasks, not one stage at a time while waiting for the user to approve continuation.

- Build and maintain a complete dependency-aware backlog covering every requirement and every development stage in this document.
- Once a task meets its acceptance criteria, immediately delegate the next ready task. Do not ask the user whether to proceed to the next stage.
- Continue through all stages and all independent tasks until the entire feasible MVP scope is implemented, built, tested, integrated and reviewed.
- Do not stop after a prototype, a single feature, a successful build or a milestone summary. Those are checkpoints, not project completion.
- If one task is blocked, record the exact blocker, identify and execute all other tasks that do not depend on it, then return to the blocked task when its prerequisite becomes available.
- Do not ask the user to choose between routine technical alternatives. Use the documented requirements, official platform documentation, measured results and an ADR to select a safe, reversible option. Ask only when a genuine product decision cannot be resolved from the specification.
- Tasks requiring the user's physical presence, credentials, permission, device availability, security approval, purchase, destructive action or other direct intervention must be deferred to the **final user-action list** whenever possible. Complete all other work first. Do not interrupt the development flow to request routine user intervention early.
- Do not bypass security or user authorization just to keep the pipeline moving. Use mocks, safe test doubles or non-destructive tests, finish all other work, and list the required real-world action at the end.

At the end, provide a consolidated report with completed tasks, verified builds/tests, known defects, `BLOCKED` items, and a clearly ordered **USER ACTIONS REQUIRED** list. Do not claim the MVP is complete while mandatory acceptance criteria remain unverified.

## 0.5. Delegation model and task ownership

The top-level session is `ORCH` (coordination only). Every child receives exactly one bounded task, one owning repository, explicit read/write paths, prerequisites, acceptance criteria, timeout rules and the expected report schema. One writer per file/module at a time. A child must not edit files outside its write scope; it must request an interface/contract change instead.

Use these role templates as needed; they are task roles, not a requirement to start all agents simultaneously:

| Role | Repository | Exclusive scope and responsibility |
|---|---|---|
| CONTRACT | Host | Canonical protocol schema/version, test vectors, compatibility rules, export bundle and Host protocol docs. |
| H-FOUND | Host | Windows app shell, CMake/build baseline, startup/lifecycle, logging. |
| H-CATALOG | Host | App discovery/catalog, stable `app_id`, allow-listed launch and launch state. |
| H-WINDOWS | Host | HWND registry, window lifecycle/relationships, cross-process dialogs and modality. |
| H-MEDIA | Host | Windows capture, frame scheduling, encoders, per-window streams and metrics. |
| H-NET | Host | LAN signaling, WebRTC transport, pairing/session authentication and DataChannel. |
| H-INPUT | Host | Input routing, coordinate mapping, window geometry and allow-listed window actions. |
| H-POWER | Host | Typed sleep/shutdown behavior, authorization and privilege validation. |
| C-FOUND | Client | App bootstrap, Xcode build, localization resources, settings infrastructure. |
| C-CONTRACT | Client | Import pinned Host protocol release, generate bindings, adapter and compatibility tests. |
| C-CONNECTION | Client | Signaling, WebRTC connection lifecycle, reconnect, pairing and DataChannel. |
| C-CATALOG | Client | App catalog UI, search and launch-state presentation. |
| C-WINDOWS | Client | Native remote NSWindows, parent/owned relationships, dialogs, modality and focus. |
| C-INPUT | Client | Keyboard/mouse routing, built-in/custom shortcut mapping and persistence. |
| C-PERF | Client | Video rendering, resize, full-screen, display detection, FPS and metrics. |
| C-POWER | Client | Confirmed sleep/shutdown controls and result presentation. |
| QA | Both, read-only production code | Independent functional, integration, offline, reliability and performance verification; test/report files only. |
| SEC-REVIEW | Both, read-only production code | Security review and findings only. |
| INTEGRATION | Both, only assigned integration paths | Contract synchronization, reviewed merges, end-to-end integration and release evidence. Must follow the same Windows skill rule for Windows work. |

### Required task packet

```text
TASK_ID / TITLE:
GOAL: One observable outcome.
ROLE:
REPOSITORY: remote-windows-host | remote-windows-client
BASE_COMMIT / BRANCH / WORKTREE:
WRITE_PATHS:
READ_ONLY_PATHS:
REQUIREMENTS: IDs and only relevant excerpts.
PREREQUISITES: Exact merged commits and contract version/checksum.
SKILLS: Explicitly list required skills. For any Windows operation: windows-ssh-remote (mandatory).
COMMANDS: Exact planned commands and expected completion condition.
TIMEOUTS: Per-command hard timeout, no-progress interval, polling interval and safe cancellation procedure.
ACCEPTANCE: Observable behavior and required tests/evidence.
EXCLUSIONS / RISKS:
RETURN FORMAT: Structured report below.
```

For `delegate_task`, put the goal in `goal` and the complete bounded task packet in `context`. Do not use vague references such as “continue from above.” Include only the relevant requirement excerpts; do not send the whole specification or conversation history by default.

### Required child report

```text
STATUS: complete | partial | blocked | failed | timeout | stalled
TASK_ID:
REPOSITORY:
BASE_COMMIT:
RESULT_COMMIT: <sha or none>
WORKTREE / BRANCH:
CHANGED_FILES: path — purpose
COMMANDS: command — exit code — observed result
PROCESSES: job/process ID — final state — stopped/confirmed — log path
TESTS_NOT_RUN: test — reason
ACCEPTANCE_EVIDENCE: requirement — evidence
WINDOWS_SKILL: used/not applicable; skill/version or blocker
RISKS_OR_LIMITATIONS:
CONTRACT_CHANGE_REQUEST: none or proposal
NEXT_DEPENDENCY: none or task ID/owner
```

Reject reports that only say “done,” omit changed files, omit exact command outcomes, lack evidence, or claim a test passed without an actual result. A missing/truncated remote report is not `PASS`.

## 0.6. Git, review and acceptance discipline

- Exactly one subagent owns a write scope at a time. Default to one active child because the local model/inference server is shared.
- Use separate task branches/worktrees. Verify actual branch, worktree path, base SHA, diff and clean state; never assume isolation succeeded.
- A child does not merge its own change into the release branch. Delegate review/integration to a separate task/role.
- Review scope, correctness, secrets, generated files, protocol compatibility, tests and unrelated changes before integration.
- Run final tests on the integrated revision, not only in a child worktree. Delegate this work; the main session must not run it directly.
- Update each repository's `docs/STATUS.md` with verified implementation status, limitations, last successful commands and exact test results.
- Keep commits focused. No opportunistic cross-scope edits. Shared protocol changes go through CONTRACT and a versioned Host release.

---

# 1. Product Goal and MVP Behavior

Build two applications:

- **Windows Host:** Runs in the interactive Windows 11 Pro user session. Discovers and launches applications, tracks main and associated secondary windows, captures and encodes each independently rendered window, streams directly over the LAN, receives input, handles geometry/window actions and exposes authorized power operations.
- **macOS Client:** Native AppKit application. Connects to the Host, displays/searches the app catalog, launches apps, presents each remote source as a native macOS window, adapts keyboard shortcuts and provides window, performance and power controls.

The user must be able to launch File Explorer, Microsoft Access, SEE Electrical and other supported apps from macOS; keep multiple windows open; use keyboard/mouse; resize and full-screen windows; adapt shortcuts; adjust inactive FPS; and request Windows sleep/shutdown. Ordinary Open/Save dialogs, folder pickers, confirmation prompts and app-associated auxiliary windows must appear automatically without manual sharing or touching the physical Windows monitor.

Do not impose an arbitrary product window limit. Practical limits are measured from GPU/encoder, network and memory resources. No required Internet access, cloud relay, sign-in, telemetry, online activation or external runtime service.

# 2. Functional Requirements

## FR-01 — Application catalog and launch

- Discover Win32 apps from Start Menu shortcuts, supported packaged apps and user-added entries. Do not claim to enumerate every executable on every disk.
- Normalize/deduplicate entries, assign stable Host-generated `app_id`, cache results, and send catalog-change notifications.
- Client supports search, sorting, icons, availability and launch state.
- Client sends only `app_id`; Host resolves it to an allow-listed local catalog entry. Never accept arbitrary client-supplied paths, shell commands or executable arguments.
- Report launch states separately: accepted, starting, started, window detected, failed. Support existing/reused processes and applications that create multiple windows.

## FR-02 — Window registry and relationships

- Maintain a live registry of top-level windows and track create/show/hide/destroy, title, focus, minimize/restore, geometry, z-order, enabled and modal state. Prefer WinEvent notifications with reconciliation scans for missed events.
- Track HWND, PID/TID, title/class, geometry, visibility, enabled state, application session, remote UUID `window_id`, owner/parent, role and modality. Never treat HWND as a durable remote ID.
- Correlate auxiliary windows using owner/owned relationships (`GetWindow(GW_OWNER)`, `GetAncestor(GA_ROOTOWNER)`), WinEvent, process/application metadata, focus transitions and UI Automation when useful.
- **PID alone is never sufficient** to identify or exclude related dialogs. Brokered pickers may run in another process; define and test correlation fallbacks.
- Treat independently rendered Open/Save, folder picker, overwrite/confirmation, print/export, properties, app-specific modal, popup and document windows as first-class remote windows. Distinguish independent top-level surfaces from child controls already included in a parent capture to avoid duplicate streams.
- Associate each auxiliary window with the app session and, when available, parent/owner remote `window_id`; track role, modality, visibility, z-order and lifecycle.
- UAC secure desktop and protected/system surfaces may be documented as unsupported. Those exceptions do not exclude ordinary app dialogs.

## FR-03 — Capture, stream and modal presentation

- Each independently rendered source window has an independent `Windows.Graphics.Capture` session, `window_id`, video stream, FPS policy, geometry and lifecycle.
- Automatically announce/capture auxiliary windows associated with an already-shared application, even with a different PID. No manual selection or second launch/share request.
- Keep the parent stream alive while a dialog is open. Closing one source releases only its resources and must not stop parent or unrelated streams.
- Handle source-size changes, movement, owner changes, modality, minimized/occluded windows and graphics-resource recreation.
- Client creates a native `NSWindow` (or appropriate native sheet) for each source and preserves owner/parent, z-order, focus and modal blocking. Windows file dialogs must remain actual remote Windows UI, not be replaced with a macOS file picker.
- Route dialog input to its own `window_id`; block parent interaction while a Host-side modal dialog is active. When the source closes, remove only its Client representation and restore the parent.
- Dismissing a remote auxiliary window on macOS sends an allow-listed close/dismiss action; keep it visible until Host confirms closure. Closing a Client presentation of a main window must not silently terminate the Windows app.

## FR-04 — Resize, geometry and full screen

Support configurable local scaling and synchronized source resizing, globally or per window. In local mode resize the NSWindow and scale the current frame. In synchronized mode send desired source dimensions, immediately scale the previous frame while waiting, then apply the actual acknowledged size. Support macOS full screen, DPI/points/backing pixels conversion, independent local/source position unless synchronization is enabled, coalesced resize updates and capture/encoder recreation when needed.

## FR-05 — Input and shortcut adaptation

- Support mouse move/click/double-click/button up/down/scroll and keyboard/modifier events. Route all events by `window_id`; map coordinates through scale, letterbox/crop and DPI transforms.
- Track pressed keys/buttons and release/reset them on focus loss or disconnect. Use supported Windows input APIs with UIPI/elevation awareness; never bypass UAC.
- Adapt keyboard shortcuts in the Client before forwarding normalized Windows key events. Match physical key codes and modifier flags, preserve key-down/up order and repeat state, and retain a normal text-input path for unmatched typing. No clipboard injection as a shortcut substitute.
- Settings include a master toggle, independent built-in toggles and custom mappings (create/edit/enable/disable/reorder/delete, source and target chord, optional app scope, label and persistence). Custom rules take precedence over built-ins. Validate duplicates/conflicts/empty or unsupported chords; do not silently pick one.
- Initial mappings, enabled by default: Command+C/X/V/A/Z/F/S/O/N/P/W → Ctrl+C/X/V/A/Z/F/S/O/N/P/W. Command+Shift+Z → Ctrl+Y (on). Disabled by default: Command+Q → Alt+F4, Command+R → Ctrl+R, Command+T → Ctrl+T, Command+Shift+S → Ctrl+Shift+S. All rules can be disabled/overridden. App-specific behavior must be documented and tested.
- macOS-reserved shortcuts (e.g. Command-Tab/system-level shortcuts) remain local unless supported testing proves otherwise. Do not claim to intercept events consumed by macOS before the Client receives them.
- Modules may include `ShortcutAdaptationService`, built-in catalog, custom store, validator, keyboard translator and Settings UI. Keep IDs/key codes stable and separate from localized labels.

## FR-06 — Dynamic FPS and latency

- `inactive_fps` is configured in Client settings; default 10 FPS, proposed range 1–60.
- Active window target FPS is limited by the display containing its NSWindow (`NSScreen.maximumFramesPerSecond`), encoder capacity and optional user cap. Recalculate when it moves between displays.
- Inactive windows use `inactive_fps`; minimized windows pause by default, with an optional low-FPS policy.
- Gate frames before encoding. Drop obsolete frames rather than queueing stale video. Resume with a fresh frame/keyframe as required. Avoid full GPU-to-CPU readback or expensive per-frame hashes without measurement.
- Display refresh is a ceiling, not a guarantee of unique source frames. Track capture/encode/decode/presentation latency, FPS, bitrate, queue depth and dropped frames.

## FR-07 — LAN-only transport and pairing

- Use direct WebRTC media tracks and DataChannel between Windows Host and the selected macOS Client. Use a small direct TCP/TLS signaling channel for SDP/ICE/pairing; media never traverses signaling, Hermes or a relay.
- No external STUN/TURN, cloud SFU, external DNS, telemetry, registration or online activation. Disable external ICE servers and verify selected candidates are LAN-local. Manual Host IP entry is acceptable for MVP; mDNS may be added later.
- Pair explicitly using a one-time code or trusted fingerprint confirmed on Host. Protect control/media using WebRTC DTLS-SRTP/DataChannel and TLS for signaling as appropriate.
- Bind to selected LAN interfaces, document firewall rules/listening ports, validate all IDs/enums/ranges/sizes/version fields and never trust client-supplied paths, PID, HWND or command strings.
- Secrets belong in secure storage (Client Keychain / appropriate Host protection), never plaintext or Git.

## FR-08 — Sleep and shutdown

- Client exposes separate `Sleep Windows PC…` and `Shut Down Windows PC…` controls. Each requires a confirmation naming the PC; shutdown warns about unsaved data. No packet is sent on Cancel.
- Send only typed `SYSTEM_POWER_REQUEST` enum `SLEEP` or `SHUTDOWN`, after pairing/authorization. Host verifies privilege and uses documented APIs, e.g. `SetSuspendState(FALSE,FALSE,FALSE)` and `InitiateSystemShutdownExW`, after checking/enabling `SE_SHUTDOWN_NAME` and `ERROR_NOT_ALL_ASSIGNED`.
- Never expose arbitrary shell or `shutdown.exe`. Do not force-close apps by default. Report `accepted`/`denied`/`failed`, not a false claim that shutdown completed. Do not auto-retry after timeout because duplicate power actions are dangerous.
- Actual sleep/shutdown tests are mocked by default. Do not trigger them on the only Windows runner without explicit user authorization in the current session and a documented recovery plan. No automatic privilege escalation, SYSTEM service or UAC bypass. Wake-on-LAN is out of scope.

## FR-09 — English MVP and localization readiness

All visible UI (menus, settings, dialogs, validation, errors, onboarding and tooltips) ships in English only. Use centralized resource keys from the beginning: macOS String Catalog `.xcstrings` or equivalent; Windows `.rc`/resource IDs or framework standard. Keep protocol enums, IDs, log events and shortcut IDs language-neutral. Use platform formatting APIs. No language selector required in MVP.

# 3. Technology Stack

| Area | Windows Host | macOS Client |
|---|---|---|
| Language/UI | C++20, MSVC, Win32 | Swift, AppKit; SwiftUI hosted only where useful |
| Capture/render | Windows.Graphics.Capture, D3D11, DXGI | Metal, minimizing frame copies |
| Video | WebRTC Native API; verify hardware encoding; Media Foundation/VideoEncoderFactory only if needed | WebRTC decoder/VideoToolbox; profile hardware decoding |
| Windows/window APIs | Win32, WinRT/C++/WinRT, UI Automation where required | NSApplication, NSWindow, NSWindowController, NSScreen |
| Launch/input/geometry | IShellLinkW, ShellExecuteExW, package activation APIs; SendInput with UIPI awareness; SetWindowPos | AppKit events, key codes/modifier flags and typed normalized events |
| Networking/schema | WebRTC + direct TCP/TLS signaling; Protocol Buffers | WebRTC bridge via Objective-C++; generated Protocol Buffers |
| Build/tests | CMake/MSVC; GN/Ninja for WebRTC if source-built; GoogleTest | Xcode, Swift Package Manager; XCTest |
| Settings/secrets | protected local storage | UserDefaults for ordinary prefs; Keychain for pairing secrets |
| Resources | Win32 `.rc`/framework localization | `.xcstrings` |

Pin SDKs, compiler, CMake, WebRTC revision, protobuf generator and dependencies. Verify actual APIs and versions against current Microsoft/Apple/WebRTC docs during the relevant delegated task; document changes in ADR/docs. Do not invent dependency URLs or claim hardware acceleration without profiling. No dependency may require Internet at product runtime.

# 4. Repository Architecture — Exactly Two Git Repositories

The only product repositories are:

1. `remote-windows-host` — Windows Host implementation and canonical protocol source.
2. `remote-windows-client` — macOS Client implementation and pinned, checksum-verified Host protocol snapshot.

Do not create a third monorepo, shared-code, workspace Git or protocol repository. A normal local directory may contain the two sibling clones, but is not a Git repository.

Suggested Host layout:

```text
remote-windows-host/
  CMakeLists.txt
  windows-host/{app,catalog,windows,capture,video,network,input,power,security,resources,diagnostics,tests}/
  protocol/{remote_windows.proto,VERSION,test-vectors/,README.md}
  tools/{export_protocol.ps1,export_protocol.sh}
  docs/{STATUS.md,ARCHITECTURE.md,DEPENDENCIES.md,PROTOCOL.md,SECURITY.md,TEST_PLAN.md,PERFORMANCE.md,decisions/,agent-runtime/}
  AGENTS.md
```

Suggested Client layout:

```text
remote-windows-client/
  RemoteWindows.xcodeproj
  macos-client/{App,Catalog,RemoteWindows,Keyboard,Network,Performance,Power,Settings,Security,Resources,Tests}/
  Protocol/Contract/{remote_windows.proto,VERSION,SHA256SUMS}
  Protocol/Generated/
  tools/sync_protocol_from_host.sh
  docs/{STATUS.md,ARCHITECTURE.md,DEPENDENCIES.md,CLIENT_PROTOCOL_INTEGRATION.md,SECURITY.md,TEST_PLAN.md,PERFORMANCE.md,LOCALIZATION.md,KEYBOARD_SHORTCUTS.md,decisions/}
  AGENTS.md
```

These are guidance layouts, not a requirement to create empty files. Keep only modules with real responsibilities.

## Ownership and cross-repository contract

- CONTRACT alone changes canonical Host `.proto`, `protocol/VERSION`, test vectors, protocol compatibility tests/docs and export tooling.
- Host feature agents consume the canonical contract but do not edit it.
- Client imports an exact Host tag/release using the sync tool, verifies checksum, generates bindings and records source tag/commit/checksum. Never hand-edit the Client snapshot or invent fields.
- Wire change workflow: request with rationale/semantics/compatibility/tests → CONTRACT approval → version/schema/tests/docs update → Host commit/tag/export → Client sync/checksum/generation → both sides build/test against same version. Breaking changes require explicit version/migration notes.
- Each file/module has a single writer. Cross-scope changes are owner requests, not opportunistic edits.

Each repository needs its own concise root `AGENTS.md`, `docs/STATUS.md`, build/test instructions, dependency pins, security/test docs and ADRs for major decisions. Host plan remains canonical; Client points to relevant requirement IDs and contract snapshot rather than forking product requirements.

# 5. Network Protocol

Minimum messages (all typed, versioned and bounded):

| Message | Purpose |
|---|---|
| `HELLO` | Protocol/app versions and capabilities |
| `PAIRING_CHALLENGE`, `PAIRING_RESULT` | Explicit pairing/trust |
| `APP_CATALOG_REQUEST`, `APP_CATALOG`, `APP_CATALOG_CHANGED` | Catalog request, snapshot and invalidation |
| `LAUNCH_APP`, `LAUNCH_RESULT`, `APP_STATUS` | Launch by `app_id` and lifecycle |
| `WINDOW_ANNOUNCE`, `WINDOW_UPDATE`, `WINDOW_REMOVE` | Stream/window lifecycle, session, relationship, role, modality and geometry |
| `INPUT_EVENT` | Normalized Windows keyboard/mouse input with `window_id` and sequence |
| `RESIZE_REQUEST`, `RESIZE_ACK` | Desired/actual source dimensions |
| `WINDOW_ACTION`, `WINDOW_ACTION_RESULT` | Allow-listed activate/minimize/restore/stop-sharing/auxiliary close actions |
| `FRAME_RATE_REQUEST`, `STREAM_STATUS` | Target/actual FPS, resolution and stream errors |
| `SYSTEM_POWER_REQUEST`, `SYSTEM_POWER_RESULT` | Only `SLEEP` or `SHUTDOWN`, with typed result |
| `HEARTBEAT`, `PERFORMANCE_STATS` | Health, FPS, bitrate, drops and latency |

Every request has protocol version, request ID, typed result/error and timeout. Use sequence/idempotency keys for state-changing commands. Every window gets a UUID `window_id`, never a durable HWND. `WINDOW_ANNOUNCE` carries app-session ID, role, modality, geometry and optional parent/owner remote IDs. Auxiliary dialogs discovered after app sharing are announced automatically, regardless of PID. Client maintains relationship graph for focus, modality, input routing and removal. Failed close requests keep the remote dialog visible. Never automatically retry a power request. Coalesce obsolete pointer/resize events but never reorder/drop key/button down/up events. Validate lengths, strings, enums, versions and numeric ranges. Disconnect must release all pressed input state.

# 6. Development Stages and Dependencies

Complete the stages in order, but continue all ready work without asking the user to approve each transition. Stage gates are evidence checkpoints, not reasons to stop. Use bounded `delegate_task` assignments for every task. Default local-model concurrency: one child at a time.

| Stage | Scope | Acceptance gate |
|---|---|---|
| 0. Environment | Verify Hermes/local-model and delegation preflight; confirm two repos; configure `windows-ssh-remote`; Windows runner/topology; pin SDK/toolchain/dependencies; add ADRs, root AGENTS, skills, STATUS and reproducible build docs. | Tool-use and delegation work; Windows access uses required skill; Mac Client and Windows runner smoke builds report exact versions/commit; no credentials stored. |
| 1. One-window vertical slice | Pairing/signaling; capture one real Windows window; encode H.264; direct WebRTC LAN stream; display in one NSWindow; stage timing metrics. | Real window visible and interactive path testable on LAN with Internet disconnected; no external relay; measured timing recorded. |
| 2. Catalog/launch | Host Start Menu/package/user entries, dedup/cache, app IDs and allow-listed launcher; Client catalog/search/launch status. | File Explorer, Access and SEE Electrical can be discovered/launched by `app_id`; arbitrary paths/commands rejected. |
| 3. Multi-window | Host live registry and independent captures; Client native window registry; per-window stream lifecycle. | Multiple app windows remain independently usable; closing one does not affect others. |
| 3A. Auxiliary/modal windows (MVP blocker) | Cross-process relationship tracker; automatic discover/announce/capture; Client automatic presentation, focus, z-order, modality and source-side dismissal. | Open, Save As, folder selection, overwrite and nested confirmation workflows in File Explorer, Access and SEE Electrical appear automatically, are interactive and modal behavior is correct. No manual share step. |
| 4. Input/shortcuts | Mouse/keyboard, coordinate/DPI mapping, focus/key-state management; built-in and custom shortcut rules/settings/tests. | Required actions work in target apps and dialogs; mapping on/off, conflicts, persistence, key ordering and disconnect cleanup pass. |
| 5. Resize/full screen | Local scale, synchronized resize/ack, immediate old-frame scaling, full screen, multi-display geometry. | Independent resize/full-screen works without prolonged freeze or stale-command buildup. |
| 6. Dynamic FPS | Active display-rate targeting, inactive FPS setting, minimized policy, pre-encode gate and metrics. | Verified per-window FPS policy and no unbounded stale-frame queues. |
| 7. Power | Typed sleep/shutdown, confirmations, authorization, privilege handling, safe diagnostics and mocks. | Mock and privilege-denial tests pass; real test only after explicit user approval/recovery plan. |
| 8. Localization audit | English-only visible copy, centralized resources, language-neutral IDs and formatting APIs. | UI strings are centralized; no business logic depends on visible English text. |
| 9. Security/offline | Pairing, LAN binding/firewall, protocol validation, secrets, ICE candidate audit, offline verification. | Product works with Internet disconnected; no arbitrary command path, no external runtime dependency, security review findings resolved or documented. |
| 10. Reliability/performance | Multi-window load, reconnect, resource cleanup, error paths, FPS/latency and resource profiling. | Measured results, known limits, no orphaned resources/processes; release builds/tests pass. |
| 11. Final integration/release | Independent QA/security review, both repositories aligned to protocol, clean release commits/tags, docs and packaging. | All MVP acceptance gates evidenced on native platforms; unresolved user actions and unsupported behavior explicitly listed. |

Dependency order:

```text
Environment + Host/Client foundations
        ↓
Canonical protocol + test vectors
        ↓
Host network ↔ Client contract/connection
        ↓
Host capture/media ↔ Client rendering (one-window slice)
        ↓
Catalog/launch ↔ Client catalog
        ↓
Window registry/capture ↔ Client multi-window/modal presentation
        ↓
Input/shortcuts → resize/FPS → power
        ↓
Offline/security → performance/reliability → QA/security/release
```

Independent work may be queued only when write paths and stable interface dependencies do not overlap. Do not run multiple model-heavy implementation tasks concurrently by default. QA and security reviews are separate delegated tasks with read-only production scope.

# 7. Testing and Acceptance

## Functional matrix

Test at minimum:

- Pairing success, bad code/fingerprint, reconnect, protocol mismatch and malformed/oversized messages.
- Catalog discovery/dedup/cache/change notification; launch success/failure, existing process, delayed/multiple windows and rejected arbitrary paths.
- File Explorer, Access and SEE Electrical: main/document windows, Open, Save, Save As, Select Folder, overwrite confirmation, nested dialog, print/export where available, cross-process/brokered dialogs, modality, focus, z-order and close/dismiss confirmation.
- Independent stream start/stop, parent retention, unrelated window survival, source destruction, minimized/occluded behavior, resolution changes and resource cleanup.
- Mouse/keyboard, DPI/coordinate transform, scrolling, key/button releases, focus loss, disconnect cleanup and elevated/UIPI limitations.
- Each built-in shortcut enabled/disabled, master switch, custom CRUD/persistence/priority/conflict validation, ordinary text, layout-sensitive keys and OS-reserved shortcut limits.
- Local scaling, synchronized resize, resize coalescing, full screen and moving between displays with different refresh rates.
- Active/inactive/minimized FPS, actual encoded FPS, frame drops, bitrate, queue depth and end-to-end latency.
- Power mock, authorization denial, insufficient privilege and no automatic retry. Real sleep/shutdown only after explicit approval and recovery plan.
- Internet physically disconnected; selected WebRTC candidates LAN-local; no external service required.

## Performance/reliability evidence

Record capture, encode, network, decode and presentation timing; actual FPS and frame drops; stream count; per-window and aggregate memory/GPU/CPU; bitrate; reconnect time; resource release; queue depth. Benchmark target GPU/codec and practical multi-window capacity rather than inventing limits. Do not report latency/FPS as verified without measurements and test conditions.

## Test status vocabulary

Use only `PASS`, `FAIL`, `NOT RUN`, `BLOCKED`, `TIMEOUT` or `STALLED`. Each result identifies exact source commit, platform/device, command, exit code, log/report and test scenario. Missing output is not success. Windows test/build results must come through `windows-ssh-remote` and be tied to exact Host commit SHA and tool versions.

# 8. Build, Dependency and Documentation Requirements

- Windows Host native build/test runs on Windows 11 Pro with pinned MSVC, Windows SDK, CMake and pinned dependencies. A Mac/Clang check is not a Windows build.
- macOS Client build/test/package runs on the Mac mini using pinned Xcode/SDK. Verify MacBook compatibility with a real smoke test before claiming that device is supported; MacBook availability does not block primary development.
- Use GoogleTest/XCTest and deterministic scripts. Keep exact successful commands and versions in each repository's `docs/STATUS.md` and `docs/DEPENDENCIES.md`.
- WebRTC is a large native dependency. Choose a verified upstream/reproducible build, pin commit/hash/license and build flags. Never invent a package URL. No runtime dependency download.
- Significant decisions (transport, codec, catalog, capture strategy, power, shortcuts, localization, dependencies) require ADR with context, decision, alternatives and consequences.
- Never commit pairing material, certificates, private keys, real tokens or local machine credentials. Use `.gitignore` and test credentials.
- Keep logs structured and local; never log passwords, keys, user-typed text or window contents.
- `docs/STATUS.md` must distinguish implemented, tested, not tested, blocked and deferred, and record last successful build/test commands.

# 9. Hermes and Local-Model Configuration

## 9.1. Verify actual installed capabilities

At project start, a delegated preflight task must run `hermes --version`, verify relevant official Hermes documentation for that installed version, and record model/provider/endpoint/terminal backend without secrets. Confirm availability and behavior of `delegate_task`, worktree isolation, skills and context files. Do not assume a feature documented on the current `main` branch exists in the installed version. Do not upgrade Hermes mid-stage; upgrade at a planned checkpoint and rerun preflight.

## 9.2. Local model

Use Hermes-managed Local Models or a user-managed OpenAI-compatible local endpoint (e.g. llama-server). Keep local endpoints on loopback unless documented otherwise. Do not silently route child work to a hosted model; leave child model/provider/base_url inherited from the parent unless a deliberate change is authorized and documented. The app's LAN-only runtime requirement is separate from Hermes inference networking.

Example configuration only; adapt to the verified provider/model and do not overwrite a working profile blindly:

```yaml
model:
  provider: llamacpp
  default: "<served-model-id>"
providers:
  llamacpp:
    base_url: "http://127.0.0.1:8081/v1"
    model: "<served-model-id>"
```

## 9.3. Delegation defaults and preflight

Recommended starting configuration (verify keys against the installed Hermes version before applying):

```yaml
delegation:
  max_concurrent_children: 1
  max_iterations: 20
  worktree_isolation: true
  max_spawn_depth: 1
  orchestrator_enabled: false
```

- Keep one active child until reliability, memory, model response and worktree isolation are measured. Do not increase concurrency to compensate for slow progress.
- Flat delegation tree; top-level session is the only orchestrator. Keep each child task small enough for the iteration budget; split larger tasks.
- Verify worktree path/branch/base SHA and actual diff. Isolation is not a security sandbox and may fall back; if uncertain, stop concurrent writes.
- A child inherits enabled toolsets; scope instructions and worktrees do not guarantee permission isolation.
- Preflight must prove: correct local model; search/read and harmless terminal tools work; a reversible edit/diff works in a throwaway repo; a bounded build/test reports real exit status; `delegate_task` completes a tiny task in an isolated worktree and returns branch/files/evidence; no invented paths/results; external network is disabled except intentionally used official documentation sources.
- **Additional mandatory preflight:** a delegated test task must load/use `windows-ssh-remote`, run a harmless Windows status query, and return exact evidence. If it fails, all Windows operations remain blocked; continue Mac-only work.
- If delegation preflight fails, do not switch to manual implementation in the main session. Repair the delegation configuration or report project tasks as blocked until delegation works.

## 9.4. Task sizing and context

One task = one defect, behavior or coherent vertical slice. Aim for 1–3 production files plus tests; justify larger scope. Brief target under ~4,000 tokens. Give requirement IDs and relevant excerpts rather than full plan. Ask agents for decisions/evidence/tests/limits, not chain-of-thought. Use scripts for deterministic mechanical tasks, but script creation and execution are delegated like all other work.

## 9.5. Project-local instructions and skills

Create concise `AGENTS.md` in both repositories. Host skills should cover build/test, capture, window relationships, protocol and Host security/power. Client skills should cover build/test, shortcuts, remote-window lifecycle, performance and contract sync. Add only skills relevant to the repository. Follow the installed Hermes skill format and trust each repository using the supported command; verify skills are active before relying on them.

In addition, `windows-ssh-remote` is a mandatory environment skill, not an optional project helper. Every child that touches Windows must load it and explicitly report use in its result.

# 10. Development and Runtime Topology

## Machine roles

| Machine | Responsibilities | Restrictions |
|---|---|---|
| Mac mini | Hermes/local LLM; two working Git clones; subagent worktrees; task coordination; Client development/build/test/package; primary integration reports | Hermes is not a product relay. Client is a separate process. No silent cloud-model fallback. |
| Windows 11 Pro PC | Single Windows Host target; native MSVC/SDK build/test runner; product Host | Remote access only through `windows-ssh-remote`; no arbitrary product commands; no automated destructive power tests without approval. |
| MacBook (optional) | Secondary Client run/compatibility tests | Not required for daily development or primary package acceptance. |

Hermes and Client are independent processes. Product data path is always Windows Host ↔ the Mac currently running Client. If Client runs on MacBook, stopping Hermes or powering off Mac mini must not interrupt that session. If Client runs on Mac mini, powering off that Mac naturally stops its Client endpoint, not because Hermes relays traffic.

## Remote Windows development access

- Windows PC is both the product target and remote build/test runner. Before any remote operation, use `windows-ssh-remote` skill as mandated in Section 0.2.
- Use key-based SSH as configured by the skill; no unattended password prompts. Limit access to LAN/Mac mini where firewall controls allow. Never store credentials in either repository.
- Use a dedicated build account, not local administrator for convenience and without power privileges for routine build/test.
- Keep runner checkout/build outputs in a known controlled directory. Each run pins exact Host commit SHA or checksummed source bundle and verifies it before building.
- Prefer a fixed allow-listed runner interface such as `status`, `prepare-source`, `build-debug`, `build-release`, `unit-tests`, `integration-tests`. Unknown actions are rejected. Product protocol never becomes arbitrary shell execution.
- Remote report includes SHA, versions, commands, exit codes, test outcomes, logs and artifact paths. A missing/truncated report is not PASS.
- Windows runner SSH/admin port is separate from product runtime ports. Development control path, source path and product media path are separately documented in `docs/SECURITY.md` and `docs/TEST_PLAN.md`.
- After a lost SSH connection, reconnect with the skill and inspect original job status before retrying. Use Section 0.3 process supervision; no indefinite remote waits or ambiguous background builds.

## Topology preflight checklist

- [ ] Hermes version, local provider/model and `delegate_task` verified on Mac mini; no unplanned cloud fallback.
- [ ] Windows PC and Mac mini addresses/OS/roles documented without secrets.
- [ ] Exactly two local product Git clones; worktree isolation verified in each.
- [ ] `windows-ssh-remote` available/trusted; harmless Windows status task succeeds through that skill.
- [ ] Windows runner builds a specific Host commit and returns same SHA, tool versions and test outcomes.
- [ ] Mac mini builds, launches, tests and packages Client.
- [ ] Optional MacBook smoke test is recorded if device available; absence does not block Mac mini.
- [ ] Product pairing, launch, capture, dialog mirroring, input and resize work with Internet disconnected.
- [ ] Selected media candidates are LAN-local; no Hermes relay.
- [ ] Only required LAN ports are exposed.
- [ ] Real power actions remain mocked unless explicitly authorized with recovery plan.

If a preflight item fails, identify exact blocker and block only dependent work. Continue all independent tasks.

# 11. MVP Definition of Done

The MVP is complete only when all applicable items below have evidence:

- [ ] Exactly two independently buildable/versioned product repositories; canonical Host protocol and pinned Client snapshot/checksum.
- [ ] Host runs in Windows 11 Pro interactive user session; Client is native macOS and runs on Mac mini.
- [ ] Explicit pairing and versioned handshake.
- [ ] Direct LAN signaling/media/control; physically disconnected Internet does not break MVP behavior.
- [ ] Catalog discovers supported apps and launches File Explorer, Access and SEE Electrical by `app_id`.
- [ ] Multiple independent windows stream simultaneously with per-window lifecycle and practical capacity measured.
- [ ] Ordinary app-owned dialogs, file/folder pickers and modal prompts appear automatically, including cross-process/brokered dialogs; no manual share action.
- [ ] Parent remains visible/blocked correctly while modal; dialog input targets dialog; source-side dismissal is confirmed.
- [ ] Keyboard/mouse, coordinate/DPI mapping and cleanup on disconnect work.
- [ ] Built-in and custom shortcut adaptation works and persists; macOS-reserved limits are disclosed.
- [ ] Local/synchronized resize, full-screen and per-window FPS policy work.
- [ ] Sleep/shutdown typed controls, confirmation and privilege/error behavior implemented; real action test documented or explicitly pending user approval.
- [ ] Security, offline, malformed-message, reconnect and resource-cleanup tests pass.
- [ ] Performance and practical multi-window constraints are measured and documented.
- [ ] Windows native build/tests and macOS build/tests pass with exact tool versions/commits. Optional MacBook support is claimed only after an actual MacBook smoke test.
- [ ] Independent QA and security reports are reviewed; all unresolved issues are stated.
- [ ] Each repository's README, AGENTS, STATUS, architecture, dependency, security, test and relevant user/developer docs are updated.
- [ ] No untracked secrets, orphaned remote jobs, unexplained running builds or unreviewed production changes remain.

# 12. First Execution Backlog

The top-level session must create and dispatch this dependency-aware backlog through `delegate_task`, and continue automatically through all ready tasks:

1. Hermes/delegation preflight and Windows skill/remote-access preflight.
2. Verify Mac mini and Windows environment/toolchains; record topology and exact paths.
3. Inspect/create the two repositories and establish clean build/status baselines.
4. Add root AGENTS, repository skills, trust settings, dependency pins and ADR structure.
5. Create canonical Host protocol schema, versioning, vectors and export workflow.
6. Import pinned Host contract into Client and verify checksum/generation.
7. Build Host and Client foundations independently.
8. Implement LAN pairing/signaling and one-window video slice.
9. Implement catalog and allow-listed launch on both sides.
10. Implement Host window registry, automatic auxiliary-window capture and Client relationship/modal presentation.
11. Implement input, shortcut adaptation and dialog interaction.
12. Implement resize/full-screen and dynamic FPS.
13. Implement safe typed power controls and mocks.
14. Complete offline/security, reliability, performance and localization audits.
15. Run independent QA and security review; resolve findings through delegated owner tasks.
16. Integrate, rebuild/retest, update docs/status and prepare release artifacts.
17. Only after all other feasible tasks are complete, present consolidated physical/user actions, approvals, unavailable-device tests and other remaining blockers.

Do not pause for user confirmation between these tasks. Do not mark a task done without evidence. Do not leave work merely because one task is blocked; continue all independent work.

# 13. Official Technical References

Verify current API details in the relevant delegated task; use the installed SDK version as the implementation constraint.

**Hermes:**
- https://github.com/NousResearch/hermes-agent/releases
- https://hermes-agent.nousresearch.com/docs/user-guide/local-models
- https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/delegation.md
- https://hermes-agent.nousresearch.com/docs/user-guide/features/context-files
- https://hermes-agent.nousresearch.com/docs/user-guide/features/skills

**Microsoft:**
- https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture
- https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow
- https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishelllinkw
- https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw
- https://learn.microsoft.com/en-us/windows/win32/api/powrprof/nf-powrprof-setsuspendstate
- https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-initiatesystemshutdownexw
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook
- https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog
- https://learn.microsoft.com/en-us/windows/win32/winauto/entry-uiauto-win32
- https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh-overview

**Apple:**
- https://developer.apple.com/documentation/appkit/nswindow
- https://developer.apple.com/documentation/appkit/nsscreen/maximumframespersecond
- https://developer.apple.com/documentation/videotoolbox
- https://developer.apple.com/documentation/metal
- https://developer.apple.com/documentation/xcode/localizing-and-varying-text-with-a-string-catalog

**WebRTC:**
- https://webrtc.org/getting-started/peer-connections
- https://webrtc.github.io/webrtc-org/native-code/native-apis/

At project start, record the date the relevant references were checked and the installed Hermes version. Documentation is mutable; do not assume a feature exists until verified against the installed release.
