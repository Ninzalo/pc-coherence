# Remote Windows for macOS — Hermes Agent + Local LLM Development Plan

**Document status:** MVP technical specification and Hermes Agent execution playbook.  
**Document language:** English.  
**MVP UI language:** English only; architecture must be ready for future localization.  
**Target runtime environment:** One physical Windows 11 Pro PC running the Windows Host and one supported macOS computer running the macOS Client. The Client must run on either the Mac mini used for development or a MacBook; the selected Client device connects directly to the Windows PC over the same LAN.  
**Development/orchestration environment:** The Mac mini runs Hermes Agent and the local LLM and is the primary development, build, test, and packaging machine for the macOS Client. The MacBook is an additional supported Client target and can be used for compatibility testing; it is not required for daily development. Hermes is a development controller, not a product runtime relay.  
**Core constraints:** No required Internet access at runtime, low input-to-display latency, multiple independent Windows application windows displayed on macOS.  
**Agent runtime baseline:** Hermes Agent v0.22.5 was the latest published release found on 2026-10-29; verify the installed version and current official documentation before execution.

---

## 0. Instructions for the Coding Agent

You are the coordinating engineering agent for this project, running under Hermes Agent with a local LLM. Delegate only bounded tasks that fit the local model's demonstrated tool-use and coding capabilities. Integrate verified outputs across exactly two Git repositories: `remote-windows-host` and `remote-windows-client`. Implement the Windows Host and macOS Client incrementally, in small, testable stages. Do not attempt to implement the entire product in one large change. Follow Section 16 (Hermes Agent + Local LLM Operating Playbook) and Section 18 (Development and Runtime Topology) as the execution policy; these sections take precedence over generic assumptions about large cloud-model agent teams or separate mandatory Client hardware.

### Mandatory engineering rules

1. **Inspect before changing.** Review the repository, source files, build system, documentation, tests, and available development environment before making changes. Do not overwrite existing files until their purpose is understood.
2. **Verify APIs.** Check current Microsoft, Apple, and WebRTC documentation before implementing platform APIs. If actual API behavior differs from this document, follow the official documentation, record the difference, and update the project documentation.
3. **Never invent results.** Do not report that a stage is complete unless the code was built and the relevant tests were run. If a target platform is unavailable, state that fact and provide reproducible target-platform test instructions.
4. **Keep the repository buildable.** After each vertical slice, run available builds, tests, formatting, and static analysis. Fix regressions before proceeding.
5. **Build the smallest end-to-end slice first.** Start with one window captured on Windows, encoded, sent directly over the LAN, and displayed on macOS. Add other capabilities incrementally.
6. **Do not ship fake functionality.** A UI control is not implemented unless it performs its stated action. Mark incomplete functionality as `TODO` and list it in `docs/STATUS.md`.
7. **Justify dependencies.** For each new dependency, document its purpose, license, pinned version or commit, build procedure, and whether it requires Internet access at runtime.
8. **No required external services.** Do not add telemetry, cloud brokers, external STUN/TURN servers, mandatory account sign-in, online activation, or automatic Internet calls. The product must remain functional with the Internet physically disconnected.
9. **Security first.** Do not implement arbitrary remote shell execution or arbitrary command strings. Do not bypass UAC or operating-system security boundaries. Power commands require an authenticated paired client and explicit confirmation.
10. **Record architectural decisions.** Create an Architecture Decision Record (ADR) under `docs/decisions/` for significant trade-offs, including context, decision, alternatives, and consequences.
11. **Never commit secrets.** Do not commit pairing keys, certificates, private keys, or real tokens. Add appropriate files to `.gitignore` and use test credentials in automated tests.
12. **Keep project status current.** Maintain `docs/STATUS.md` with implemented features, verified results, limitations, next tasks, and exact commands used for the last successful build.
13. **Version the protocol.** Every handshake must include a protocol version. Test compatibility and version mismatches. Breaking protocol changes require an explicit version or schema change.
14. **Use small, reviewable changes.** Each stage must have a concrete output, acceptance criteria, and reproducible verification steps.
15. **Keep user-facing copy localizable.** The MVP UI is English-only, but do not scatter user-facing strings throughout business logic. Use resource keys and centralized string resources from the beginning.
16. **Treat shortcut adaptation as real input processing.** Settings toggles must change keyboard behavior. Do not implement a decorative list of shortcut mappings without wiring it into the input pipeline.
17. **Use the correct product names consistently.** The Windows application is the **Windows Host**. The macOS application is the **macOS Client**. Do not call them “Windows Agent” or “macOS Host” in new UI, docs, logs, or protocol descriptions.
18. **Treat auxiliary-window mirroring as an MVP blocker.** Never mark multi-window capture/input as complete until standard file/folder dialogs and modal prompts from the target applications appear and remain usable on the Client without manual selection. Do not use PID mismatch alone to exclude an auxiliary window.
19. **Use exactly two Git repositories.** The product repositories are `remote-windows-host` and `remote-windows-client`. Do not create a third shared-code, monorepo, or protocol repository.
20. **Use single-writer ownership.** Every source path and protocol contract has one designated owner. Subagents must not edit files outside their assigned write scope.
21. **Integrate through versioned contracts.** The Host repository owns the canonical wire schema. The Client consumes a pinned, verifiable schema snapshot exported from a Host release. Client agents must not independently edit the wire schema.
22. **Restrict context per subagent.** Give each subagent only its task brief, relevant specification sections, repository-local code needed for the task, and the approved interface contract. Do not pass the full conversation history by default.

### Multi-agent operating model

The coordinating agent owns planning, dependency ordering, acceptance decisions, and cross-repository integration. It does not independently implement feature code assigned to a subagent. Subagents work only in their assigned repository and write scope, unless an explicitly approved task grants narrower additional access.

#### Required subagent roles

| ID | Subagent | Repository access | Exclusive write scope | Responsibilities | Must not do |
|---|---|---|---|---|---|
| ORCH | Project Orchestrator / Technical Lead | Read both repositories; create tasks/branches and review PRs | Coordination metadata only; no production source files | Break work into tasks, order dependencies, assign owners, enforce acceptance gates, resolve cross-repo sequencing, maintain release checklist | Implement feature code; silently alter protocol or requirements |
| CONTRACT | Protocol Contract Steward | Write `remote-windows-host`; read Client contract snapshot | Host `protocol/`, protocol compatibility tests/vectors, Host `docs/PROTOCOL.md`, protocol export tooling | Own message schema, versioning, compatibility rules, test vectors, release contract bundle | Change Host/Client feature code; change schemas without migration notes |
| H-FOUND | Host Foundation Agent | `remote-windows-host` only | Host app shell, build files, startup/lifecycle, logging scaffolding, Host-local status/docs except protocol-owned docs | Bootstrap C++/CMake solution, build/test baseline, tray app/lifecycle, structured logging | Touch catalog, capture, window tracking, input, power, or protocol schema implementation |
| H-CATALOG | Host Catalog & Launcher Agent | `remote-windows-host` only | `catalog/` and its unit tests | Scan app sources, normalize catalog records, create stable `app_id`s, launch allow-listed catalog entries, report launch state | Change window capture or network protocol schema; execute client-supplied paths/commands |
| H-WINDOWS | Host Window Registry & Auxiliary Dialog Agent | `remote-windows-host` only | `windows/` plus window-relationship tests | Discover top-level windows, track owner/parent relationships, correlate cross-process dialogs, classify modality, lifecycle and geometry | Implement video encoding or Client UI; use PID/title alone as ownership proof |
| H-MEDIA | Host Capture & Media Agent | `remote-windows-host` only | `capture/`, `video/`, media-specific tests and benchmarks | Capture independent window surfaces, frame scheduling, hardware encoding integration, stream lifecycle, metrics | Change signaling/pairing or Client rendering; invent unmeasured performance claims |
| H-NET | Host Transport & Pairing Agent | `remote-windows-host` only | `network/` and network-specific tests, excluding canonical schemas | LAN signaling, WebRTC peer connection lifecycle, tracks/DataChannel, pairing integration, authentication of sessions | Change `.proto` contract directly; add external runtime services |
| H-INPUT | Host Input & Geometry Agent | `remote-windows-host` only | `input/`, `windows/WindowGeometry.*` only with coordination, corresponding tests | Route normalized keyboard/mouse input to `window_id`, map coordinates, implement allow-listed window actions and resize handling | Change Client shortcut preferences or protocol schema; bypass UIPI/UAC |
| H-POWER | Host Power & Authorization Agent | `remote-windows-host` only | `power/`, power-specific authorization rules and tests | Implement typed sleep/shutdown operations, privilege validation, safe diagnostics | Add shell execution, force-close apps by default, or elevate automatically |
| C-FOUND | Client Foundation & Localization Agent | `remote-windows-client` only | App bootstrap, build settings, centralized English resources, Client-local docs except contract snapshot | Bootstrap Swift/AppKit app, localization-ready resource layout, build/test baseline, preferences infrastructure | Implement network protocol or remote-window features |
| C-CONTRACT | Client Protocol Integration Agent | Read Host protocol release; write `remote-windows-client` | `Protocol/Contract/`, generated protocol bindings, Client protocol adapter and compatibility tests | Import a pinned Host contract snapshot, verify checksum/version, integrate generated Client bindings | Edit canonical `.proto` schema or change Host code |
| C-CONNECTION | Client Connection Agent | `remote-windows-client` only | `Network/` except UI-owned pairing views, connection tests | Implement signaling client, peer connection bridge, reconnect state, DataChannel transport, pairing handshake | Implement catalog/remote window presentation; bypass authentication |
| C-CATALOG | Client Catalog & Launch UI Agent | `remote-windows-client` only | `Catalog/` and catalog UI tests | Display/search catalog, initiate `LAUNCH_APP`, show launch lifecycle and errors | Launch arbitrary paths or edit Host catalog implementation |
| C-WINDOWS | Client Remote Window & Modal Agent | `remote-windows-client` only | `RemoteWindows/RemoteWindow*`, `RemoteWindowRelationshipCoordinator`, `ModalWindowCoordinator`, related tests | Create one native window per stream; represent parent/owned relationships; preserve modality, focus and z-order; automatically present Host-announced dialogs | Implement Host-side window discovery or hide a dialog before Host confirms closure |
| C-INPUT | Client Input & Shortcut Agent | `remote-windows-client` only | `Keyboard/`, `RemoteWindows/InputRouter.swift`, keyboard/input tests | Translate input, implement built-in shortcut toggles, custom mappings, conflict checking, persistence and key-state cleanup | Change Host input implementation or protocol schema; claim OS-reserved shortcuts are interceptable when they are not |
| C-PERF | Client Rendering, Resize & FPS Agent | `remote-windows-client` only | `RemoteWindows/VideoSurfaceView.swift`, `ResizeController.swift`, `Performance/`, rendering/performance tests | Render frames efficiently, resize/full-screen behavior, display detection, per-window FPS policy and metrics | Change Host encoder/network code or unrelated settings views |
| C-POWER | Client Power & Device Controls Agent | `remote-windows-client` only | `Power/` and its UI/tests | Add separately confirmed sleep/shutdown UI and typed requests, present asynchronous outcomes safely | Implement Host privilege logic or resend power requests automatically |
| QA | Cross-Repository Verification Agent | Read both repositories; may run builds/tests on available platforms | Test-only files and test reports in the designated test/report locations; no production source | Run integration matrices, auxiliary-dialog workflows, protocol compatibility, offline and performance tests; produce evidence-based reports | Fix production code directly; mark unrun tests as passed |
| SEC-REVIEW | Security Review Agent | Read both repositories | Findings/review reports only | Review LAN exposure, pairing, input, app launch, auxiliary-window actions, power controls, secrets and malformed-message handling | Implement fixes or widen permissions; assign a finding to the correct owner instead |

These are role templates, not 17 agents that should be launched at once. Under a local LLM, activate only the role needed for the current task and normally run one implementation subagent at a time. If the local model fails the delegation/tool-use preflight in Section 16, execute the same roles sequentially as fresh, narrowly scoped tasks without spawning children. Preserve the same ownership boundaries and review checkpoints in either mode.

#### Task brief template — required for every subagent

Every assigned task must contain only the following bounded context:

1. `Task ID` and one-sentence outcome.
2. Owning repository and branch/worktree.
3. Assigned role and exact write paths.
4. Relevant requirements by ID and only the specification excerpts needed for the task.
5. Approved interface/protocol version and dependencies that are already complete.
6. Files/modules the agent may read to understand its boundaries.
7. Acceptance criteria and exact build/test commands.
8. Explicit exclusions and known risks.
9. Required report format: changed files, tests actually run, results, limitations, contract changes requested, and next dependency.

Subagents must not be given the entire chat history by default. The Orchestrator may consult the full plan, but each implementation agent receives a narrow task packet. If the task cannot be completed within its write scope, the agent must return an interface-change request instead of editing another owner's files.

#### Branching, review, and single-writer rules

- Use one task branch per agent, such as `agent/H-WINDOWS/RW-123`; never let two agents concurrently edit the same file or module.
- Every task ends with a pull request or equivalent reviewable commit. The owning subagent must provide tests and a concise implementation report.
- The owning module agent reviews local design consistency; ORCH checks acceptance criteria and cross-repository compatibility; QA performs independent verification where applicable.
- No agent may merge its own change directly to the release branch without review.
- Keep commits focused. Do not bundle unrelated formatting, renaming, dependency upgrades, or refactoring into a functional task.
- If another owner’s file must change, open a dependency request and wait for the owner/Orchestrator to assign it. Do not make opportunistic cross-scope edits.
- Shared interface changes require an approved contract update before dependent implementation proceeds.
- At integration checkpoints, freeze the relevant interface version and build both repositories against that exact version.

### Dependency graph and sequential work gates

```text
ORCH + H-FOUND + C-FOUND
          |
          v
CONTRACT: protocol v0.x + test vectors
          |
          v
H-NET <----> C-CONTRACT + C-CONNECTION
          |
          v
H-MEDIA <----> C-PERF (one-window end-to-end slice)
          |
          v
H-CATALOG <----> C-CATALOG
          |
          v
H-WINDOWS <----> C-WINDOWS (main + auxiliary/modal windows)
          |
          v
H-INPUT + C-INPUT
          |
          v
H-INPUT geometry + C-PERF resize/FPS
          |
          v
H-POWER <----> C-POWER
          |
          v
QA + SEC-REVIEW + ORCH acceptance
```

Parallel work is permitted only when tasks have distinct write scopes and stable interface dependencies. For the default local-LLM profile, do not run parallel implementation agents. After measurement, at most two independent read-only reviews or mechanical tasks may be considered concurrently. Host and Client implementation remains a serialized contract hand-off unless separate inference capacity and safe repository worktrees are explicitly verified.

### Required task workflow

For every task:

1. Read the relevant specification section and any related ADR.
2. Inspect the current implementation and tests.
3. Write a short implementation plan for the requested change.
4. Implement the smallest complete vertical slice.
5. Add or update automated tests.
6. Run formatting, build, and tests available in the environment.
7. Check error handling, connection loss, and resource cleanup.
8. Update `docs/STATUS.md`, protocol documentation, and user/developer documentation as needed.
9. Report changed files, commands run, actual test outcomes, and known limitations. Never claim that untested code works.

---

## 1. Product Goal

Build two applications:

- **Windows Host:** Runs in the interactive user session on Windows 11 Pro. It discovers and launches applications, tracks main and associated auxiliary windows, automatically captures ordinary app-associated dialogs, encodes and streams video, receives input events, changes source-window geometry, and exposes approved system-power actions.
- **macOS Client:** A native macOS application. It connects to a running Windows Host, displays the Windows application catalog, launches selected applications, presents each streamed Windows window as a separate native `NSWindow`, adapts keyboard shortcuts, and provides window, performance, and power controls.

The user must be able to work without switching to the physical Windows monitor:

1. Browse applications available on Windows.
2. Launch File Explorer, Microsoft Access, SEE Electrical, and other supported applications.
3. See each captured application window as a separate macOS window.
4. Keep any practically supported number of remote windows open concurrently; do not impose an arbitrary product limit.
5. Resize each remote window, use local scaling or synchronized source resizing, and enter macOS full-screen mode.
6. Interact with applications using the mouse and keyboard.
7. Adapt common macOS keyboard shortcuts to their Windows equivalents and define custom shortcut mappings.
8. Dynamically adjust each stream's frame rate according to focus, display capability, and Client settings.
9. Request Windows sleep or shutdown from the macOS Client.
10. Use all MVP features over a local network without required Internet access.
11. Automatically mirror secondary windows created by a shared Windows application—including file-open/save dialogs, folder pickers, confirmation prompts, and other owned or app-associated dialogs—on the macOS Client, without requiring the user to share each dialog separately.

“Any number of windows” means no arbitrary fixed limit in the implementation. The practical number is constrained by the PC GPU/encoder, the Mac, memory, and network capacity. Measure and expose those limits rather than hiding them.

---

## 2. Functional Requirements

### FR-01. Application Catalog

- The macOS Client receives a catalog of applications available to launch for the current Windows user.
- Include Win32 applications discovered from Start Menu shortcuts, supported packaged applications, and user-added entries.
- Support search, sorting, icons, availability indication, and launch state.
- Send catalog changes to the Client without requiring a complete rescan every time the catalog view opens.
- Deduplicate records from multiple sources using stable identity and launch metadata.
- Do not claim to enumerate literally every executable file on every disk. The catalog is based on standard application sources plus explicit user-added entries.

### FR-02. Launch Applications from macOS

- The user launches an application by its Host-generated `app_id`.
- The Windows Host resolves the `app_id` to a known catalog entry. The Client must not submit arbitrary paths, shell commands, or executable arguments for remote execution.
- Support Win32 and packaged applications to the extent allowed by the relevant Windows APIs.
- Return launch states such as `accepted`, `starting`, `started`, `window_detected`, and `failed`.
- Process startup and appearance of a capturable window are separate events.
- Support applications that reuse an existing process or create multiple windows. Do not rely only on the PID of the process originally launched.

### FR-03. Window Discovery, Relationships, and Registry

- Maintain a live registry of top-level Windows windows, not only the main window originally selected by the user.
- Track window creation, showing/hiding, destruction, title changes, activation/focus, minimization/restoration, location changes, and modal state. Use event-driven notifications where possible and reconciliation scans to recover missed events.
- Track each window's native `HWND`, process/thread information, title, class, geometry, visibility, enabled state, z-order, associated application session, remote `window_id`, and any owner/parent relationship.
- Never use `HWND` as a durable remote identity because Windows may reuse handle values.
- Implement an `AuxiliaryWindowTracker`/`WindowRelationshipTracker` that identifies secondary windows using owner/owned relationships, `GetWindow(..., GW_OWNER)`, `GetAncestor(..., GA_ROOTOWNER)`, WinEvent notifications, process and application metadata, foreground/focus transitions, and UI Automation or another supported correlation mechanism where appropriate.
- **Do not identify related windows by PID alone.** Some dialogs or pickers can be brokered or hosted in a different process. The Host must correlate them with the shared application using available ownership/accessibility/window-event evidence and tested fallback rules.
- Treat visible, independently rendered secondary windows as first-class remote windows. This includes standard Open/Save dialogs, folder-selection dialogs, overwrite and other confirmation prompts, print/export dialogs, properties dialogs, app-specific modal dialogs, and additional document windows.
- This automatic discovery and presentation is mandatory for the MVP: when a shared application opens an associated dialog, it must appear on the macOS Client automatically. The user must not need to select or share that dialog manually, switch to the physical Windows monitor, or rely on the main-window capture alone.
- Distinguish true child controls contained within the captured parent surface from separately rendered top-level/owned/popup surfaces. Do not create a duplicate stream for content already included in the parent frame. Capture a separate surface when the pop-up is independently rendered and capturable.
- Associate each auxiliary window with an application session and, where available, a parent/owner `window_id`. Track its role (main, document, dialog, file picker, popup, tool, or unknown), modality, visibility, z-order relationship, and lifecycle.
- Define documented behavior for system surfaces that cannot be captured from the normal interactive session, including UAC secure desktop and protected content. These exceptions must not be used to exclude ordinary application dialogs.

### FR-04. Multi-Window and Auxiliary-Window Capture

- Capture every selected main source and automatically include independently rendered, user-visible secondary windows associated with that shared application session. Auxiliary windows do not require a separate manual share action.
- Capture each independently rendered source window with its own `Windows.Graphics.Capture` session and stream, including application-created modal dialogs and file/folder pickers.
- Give every stream an independent `window_id`, FPS policy, geometry, modality/relationship metadata, and lifecycle state.
- Keep the parent application's stream alive while a dialog is open unless Windows itself closes or invalidates the source.
- Release capture, encoder, and transport resources when each individual source closes or becomes unavailable; a dialog closing must not stop the parent stream or unrelated application streams.
- Handle source-resolution changes, dialog movement, owner changes, modality transitions, and graphics-resource recreation.
- Test minimized, occluded, modal, full-screen, brokered, popup, and protected windows. Document unsupported system surfaces, but ordinary Save/Open/folder-selection dialogs are MVP acceptance cases.

### FR-05. Native macOS Windows and Modal Relationships

- Display each received main or auxiliary window in a separate native `NSWindow`, or an equivalent native sheet presentation where appropriate, while maintaining a distinct source `window_id` and stream.
- Automatically create the Client-side representation as soon as an associated source window is discovered and announced by the Host. No user selection or manual sharing step is allowed for ordinary associated dialogs.
- Preserve owner/parent relationships, focus, z-order, and modal behavior. Present an associated modal dialog above its parent and prevent interaction with the blocked parent while the Host-side dialog remains modal. Route all input to the dialog's own `window_id`.
- Display Windows-hosted file/folder dialogs as their actual remote Windows UI; do not substitute a native macOS file picker, because the operation must act on the Windows file system and preserve the Windows application's own dialog state.
- When a dialog opens, keep its parent visible behind it where appropriate. When the dialog closes on Windows, remove only its Client-side representation and restore interaction with the parent.
- If the user closes/dismisses a remote auxiliary dialog on macOS, send an explicit allow-listed close/dismiss request to the Host and wait for the Host to confirm closure. Do not merely hide the Client-side copy while leaving a modal dialog open on Windows.
- Closing the presentation of a main application window must not unexpectedly terminate the application. The UI must distinguish stopping the local presentation from explicitly requesting that a source window be closed.
- Display the application/window title and connection state, and make it clear when the active window is a modal dialog.

### FR-06. Window Resizing and Full Screen

Support two configurable modes, globally or per window:

1. **Local scale:** Resize the macOS `NSWindow`; keep the Windows source dimensions unchanged and scale the current image.
2. **Synchronized resize:** Send a resize request to Windows. While waiting for a frame at the new source size, immediately scale the most recent frame on macOS.

Additional requirements:

- Support native macOS full-screen mode.
- Convert correctly between macOS points, backing pixels, and the Windows client area, accounting for DPI scaling.
- Manage the macOS window geometry independently from the position of the source window on Windows unless source-position synchronization is explicitly enabled.
- Coalesce or rate-limit geometry updates during interactive resizing to prevent queues of stale commands.
- Process the actual size acknowledged by Windows and recreate capture/encoder resources when necessary.

### FR-07. Mouse and Keyboard Input

- Support pointer movement, clicks, double-clicks, button releases, scrolling, keyboard events, and modifier keys.
- Route events to the intended source using `window_id`.
- Convert macOS window coordinates into Windows client coordinates, accounting for scaling, letterboxing, and cropping.
- Track pressed keys and mouse buttons. On disconnect, reset the input state so a key or button cannot remain logically pressed.
- The primary mode sends input to the selected active remote window. Simultaneous independent keyboard focus in several Windows applications is not guaranteed by the normal Windows input model.
- The Host may activate the target window and use supported Windows input APIs. Document UIPI/elevation limitations; never bypass UAC.
- macOS-reserved global shortcuts such as Command-Tab and system-level shortcuts remain local to macOS in the MVP unless a documented, supported implementation proves otherwise. Do not promise to intercept shortcuts consumed by the OS before the Client receives them.

### FR-08. Keyboard Shortcut Adaptation

The macOS Client must provide an actual shortcut-remapping layer. Keyboard input must not be forwarded as a naive one-to-one copy of macOS key events because common modifier conventions differ between macOS and Windows.

#### 8.1 Input-processing rules

- Apply shortcut adaptation only while a remote window is the active input target.
- Match shortcut rules before emitting the normalized key events to Windows.
- Use physical key codes and modifier flags for shortcut matching; do not rely exclusively on localized character strings.
- When a rule matches, emit the target key combination as ordered Windows key-down/key-up events, including correct modifier-down and modifier-up ordering.
- When no adaptation rule matches, forward ordinary keyboard input through the baseline key/text translation path so typing still works with supported keyboard layouts.
- Preserve key-up events, key-repeat behavior where appropriate, and modifier state. Reset outstanding key state on focus loss or connection loss.
- Do not use clipboard injection or synthetic text replacement as a substitute for proper shortcut translation.
- All mapping rules must be deterministic and tested. A matching custom rule takes precedence over a built-in rule.

#### 8.2 Built-in shortcut settings

Add a **Keyboard Shortcuts** section to macOS Client Settings. It must contain:

- A master toggle: `Adapt macOS shortcuts for Windows`.
- A separate on/off toggle for every built-in mapping.
- A visible source shortcut and its Windows target shortcut for each rule.
- A short explanation that macOS global shortcuts may be intercepted by the operating system and remain local.

Initial built-in mappings and default states:

| macOS shortcut | Windows target | Default | Notes |
|---|---|---:|---|
| `Command+C` | `Ctrl+C` | On | Copy |
| `Command+X` | `Ctrl+X` | On | Cut |
| `Command+V` | `Ctrl+V` | On | Paste |
| `Command+A` | `Ctrl+A` | On | Select all |
| `Command+Z` | `Ctrl+Z` | On | Undo |
| `Command+Shift+Z` | `Ctrl+Y` | On | Redo; some applications use a different convention, so allow disabling or overriding it |
| `Command+F` | `Ctrl+F` | On | Find |
| `Command+S` | `Ctrl+S` | On | Save |
| `Command+O` | `Ctrl+O` | On | Open |
| `Command+N` | `Ctrl+N` | On | New |
| `Command+P` | `Ctrl+P` | On | Print |
| `Command+W` | `Ctrl+W` | On | Close document/window in many applications; behavior is application-specific |
| `Command+Q` | `Alt+F4` | Off | Quit/close application; potentially destructive or app-specific |
| `Command+R` | `Ctrl+R` | Off | Application-specific, for example refresh in some programs |
| `Command+T` | `Ctrl+T` | Off | Application-specific, for example a new tab in some programs |
| `Command+Shift+S` | `Ctrl+Shift+S` | Off | Save As; behavior varies across applications |

These are initial defaults, not a claim that every Windows program assigns identical meanings to the same shortcut. The user must be able to disable every built-in mapping independently. The master toggle disables all built-in and custom adaptation without deleting saved rules.

#### 8.3 Custom shortcut mappings

Provide a separate **Custom Mappings** list in Settings. The user can create, edit, enable/disable, reorder, and delete a custom rule.

Each custom mapping includes:

- A source shortcut captured on macOS, including modifier keys.
- A target Windows shortcut, including target modifier keys.
- An enabled/disabled state.
- An optional scope: all remote applications or one selected catalog application. If per-application scope is deferred, document it explicitly and provide a global scope in the MVP.
- A user-editable label or description.

Behavior and validation:

- Custom mappings take precedence over built-in mappings.
- Detect duplicate source combinations and conflicting rules; show a clear validation message rather than silently selecting an arbitrary rule.
- Reject empty mappings and unsupported key combinations.
- Provide a way to test/display the source and target combinations before saving.
- Store custom mappings persistently on the macOS Client using a versioned settings model. Do not store them on the Windows Host unless a future synchronization feature explicitly requires it.
- Built-in defaults must be separately identifiable from user-defined mappings so application updates can add or revise defaults without overwriting custom rules.
- Do not intercept macOS global/system shortcuts from outside the Client process in the MVP. If a rule is unavailable because macOS consumes the shortcut first, show this limitation to the user.

#### 8.4 Shortcut adaptation architecture

Implement the feature as a dedicated Client module, for example:

- `ShortcutAdaptationService`: evaluates incoming keyboard events against active rules.
- `BuiltInShortcutCatalog`: defines built-in mappings and default toggle states.
- `CustomShortcutStore`: persists custom rules and schema version.
- `ShortcutRuleValidator`: detects invalid or conflicting rules.
- `KeyboardEventTranslator`: translates matched rules into target Windows key events and forwards unmatched typing through the baseline input path.
- `KeyboardShortcutsSettingsView`: exposes the master toggle, per-rule toggles, and custom mapping editor.

Keep rule definitions independent of UI strings. Rule IDs and key codes are stable, non-localized identifiers. Localize only their visible names and explanations.

### FR-09. Dynamic Frame Rate

- The `inactive_fps` setting lives in the macOS Client; default is 10 FPS.
- An active window's target FPS is the maximum supported refresh rate of the macOS display on which the window resides, subject to actual stream/encoder performance and any explicit user cap.
- Read the display target using `NSScreen.maximumFramesPerSecond`; recalculate when the window moves to another display.
- Limit inactive windows to `inactive_fps`.
- By default, pause frame transmission for minimized windows; provide a setting to use a low FPS instead.
- Apply the frame-rate gate before encoding, not only after encoding.
- When a stream resumes or its FPS increases, deliver a fresh frame and request a keyframe if required by the codec.
- Do not read entire GPU surfaces back to the CPU on every frame solely to compare pixels. Avoid retransmitting unchanged frames when this can be detected with low overhead.
- The display refresh rate is a target ceiling, not a promise of that many unique frames from the source application.

### FR-10. LAN-Only Operation

- All MVP features must work with the Internet disconnected.
- Video and input events travel directly between the Windows Host and macOS Client over the LAN.
- Do not require external STUN/TURN, cloud SFU services, registration, telemetry, online activation, or external DNS names.
- Manual entry of the Windows Host IP address is acceptable for the MVP. Add local discovery through mDNS/multicast later only if needed.
- Disable external ICE servers and verify the actual selected WebRTC ICE candidates. Filter or reject non-LAN candidates if necessary.
- A small direct TCP/TLS signaling channel may exchange SDP/ICE and pairing information; video must not pass through signaling.
- Document firewall requirements and the exact listening interfaces/ports.

### FR-11. Put Windows to Sleep

- The macOS Client exposes `Sleep Windows PC…`.
- Before sending the request, display a confirmation dialog containing the target PC name and a warning that the network connection will be lost.
- After confirmation, send only a typed `SYSTEM_POWER_REQUEST` with enum `SLEEP`; do not accept arbitrary client commands.
- The Windows Host verifies pairing, authorization, required privilege, and whether the action is allowed, then calls a documented sleep API.
- For a desktop Win32 app, `SetSuspendState(FALSE, FALSE, FALSE)` from `PowrProf.dll` may be used after verifying the required `SE_SHUTDOWN_NAME` privilege. Check the return value and applicable `GetLastError` result.
- If feasible, send an acceptance response before entering sleep. Disconnection immediately after acceptance is expected and is not necessarily an application error.
- The Client displays a state such as `Windows is entering sleep`; it must not promise that the connection will remain open.
- Wake-on-LAN is outside the MVP. Do not claim that the Client can wake a sleeping PC unless Wake-on-LAN is separately implemented and tested.

### FR-12. Shut Down Windows

- The macOS Client exposes `Shut Down Windows PC…` as a distinct action.
- Require a separate explicit confirmation; make the action visually distinct from sleep.
- Do not force-close applications by default (`forceAppsClosed = false`) to avoid data loss.
- The Windows Host uses a verified Windows API such as `InitiateSystemShutdownExW`, with `SE_SHUTDOWN_NAME`, an appropriate planned-shutdown reason, and no forced application closure in normal mode.
- Shutdown is asynchronous. Acceptance of the request does not prove that Windows has finished shutting down. Show `Shutdown requested` rather than claiming the system is already off.
- Warn that open documents can block shutdown or prompt the user to save/close them on Windows.
- Do not expose arbitrary `shutdown.exe` command execution or shell commands through the protocol.
- Restrict power control to paired/authenticated clients. Record the outcome in local diagnostics without logging secrets.
- If privileges are insufficient, return a clear error. Do not install a privileged service or bypass Windows policies to avoid the error.

### FR-13. Security and Pairing

- Pair the macOS Client with the Windows Host explicitly, using a one-time code or a trusted fingerprint confirmed on the Host.
- Protect control messages and media with WebRTC's cryptographic mechanisms (DTLS-SRTP/DataChannel) and/or TLS for signaling.
- Validate `app_id`, `window_id`, and system-action enums on the Host. Never trust a client-provided filesystem path, PID, HWND, or command string.
- Bind to a selected LAN interface/address; use Windows Firewall to restrict incoming connections.
- Do not enable power commands before pairing succeeds.
- Never store passwords or keys in plaintext.
- Validate message sizes, protocol versions, enums, and list bounds. Malformed input must not crash the application.

### FR-14. English-Only MVP and Future Localization

- All user-facing text in both applications must be English in the MVP, including menus, dialogs, settings, validation errors, status labels, tooltips, and onboarding.
- The same macOS Client product must be buildable and runnable on the Mac mini used for Hermes development and on a supported MacBook. Do not hard-code paths, device names, hardware assumptions, display identifiers, or runtime ports that only work on one Mac.
- The primary supported workflow is to develop, build, test, and package the macOS Client on the Mac mini. A MacBook is an optional secondary runtime/compatibility target; it is not a required third development machine.
- Keep the codebase localization-ready from the beginning. Do not hard-code visible English strings throughout business logic.
- Use stable localization keys and centralized resources:
  - macOS: Xcode String Catalogs (`.xcstrings`) or an equivalent centralized resource mechanism.
  - Windows Host: Win32 resource strings (`.rc`/resource IDs) for native UI; if a different UI framework is chosen, use that framework's standard resource/localization mechanism.
- Provide an English (`en`) or English-US (`en-US`) resource set as the only shipped language in the MVP.
- Keep machine-readable protocol IDs, enum values, log event names, app IDs, and shortcut rule IDs independent of user-facing translations.
- Format numbers, dates, and units through platform localization APIs rather than assuming all future locales use English formats.
- Do not add a language selector in the MVP unless it is needed for testing; adding another language later must not require changes to core business logic or protocol schemas.

---

## 3. Recommended Technology Stack

### 3.1. Windows Host

| Area | Technology / choice |
|---|---|
| Primary language | C++20, MSVC |
| Desktop and system APIs | Win32 API, Windows SDK, C++/WinRT for WinRT APIs |
| Window capture | `Windows.Graphics.Capture` + `IGraphicsCaptureItemInterop::CreateForWindow` |
| Graphics surfaces | Direct3D 11, DXGI |
| Video transport and encoding | WebRTC Native API; verify hardware encoding in the pinned build. If needed, implement a `VideoEncoderFactory` or Media Foundation adapter without encoding the same frame twice |
| Window discovery and relationships | `EnumWindows`, `SetWinEventHook`, `GetWindow(GW_OWNER)`, `GetAncestor(GA_ROOTOWNER)`, process APIs, and UI Automation where useful |
| Win32 application launch | Shell Link API (`IShellLinkW`), `ShellExecuteExW` |
| Packaged application launch | Windows App SDK/WinRT package APIs, `AppListEntry` or `IApplicationActivationManager` where applicable |
| Window geometry | `SetWindowPos` and other appropriate Win32 APIs |
| Input | Win32 input APIs; use `SendInput` with awareness of UIPI and compatibility limits |
| Sleep | `SetSuspendState(FALSE, FALSE, FALSE)` after privilege verification |
| Shutdown | `InitiateSystemShutdownExW` or another verified API; do not force-close apps by default |
| Transport | WebRTC Native API; initially evaluate one PeerConnection for a device pair with multiple video tracks/transceivers and a DataChannel |
| Serialization | Protocol Buffers for control messages, with pinned code generator version |
| Signaling | Direct local TCP/TLS channel |
| Build | CMake + MSVC for project code; GN/Ninja for WebRTC if built from source |
| Tests | GoogleTest unit tests and integration tests on Windows 11 Pro |
| Logging | Structured local logs without passwords, window contents, user-entered text, or secrets |
| UI resources | Win32 `.rc` string resources or the selected UI framework's standard localization resources; English only in the MVP |

**WebRTC build note:** Native WebRTC is a large dependency with its own build process and platform flags. Do not invent a binary package URL or version. Select an official upstream or a verified reproducible build, pin its commit/hash, document licensing, and maintain build instructions in `docs/DEPENDENCIES.md`. No dependency download may be required at runtime.

### 3.2. macOS Client

| Area | Technology / choice |
|---|---|
| Primary language | Swift |
| Native windows | AppKit (`NSApplication`, `NSWindow`, `NSWindowController`) |
| Catalog and settings UI | SwiftUI hosted within an AppKit application, unless AppKit-only is simpler; avoid using two UI stacks without a clear reason |
| Video rendering | Metal, minimizing frame copies between decoder and renderer |
| Video decoding | WebRTC native decoder/VideoToolbox; verify hardware decoding using profiling tools |
| WebRTC bridge | Objective-C++ bridge for calling C++ WebRTC from Swift |
| Display refresh rate | `NSScreen.maximumFramesPerSecond`; track window movement between displays |
| Preferences | `UserDefaults` for non-sensitive settings; Keychain for pairing secrets |
| Shortcut adaptation | Swift service with built-in rule catalog, per-rule toggles, custom mappings, validation, and persistence |
| Keyboard events | AppKit event handling for focused remote windows; use key codes/modifier flags and a defined Windows key-event representation |
| Networking | WebRTC + direct local signaling channel |
| Serialization | Generated Protocol Buffers types |
| Build | Xcode; Swift Package Manager for project-owned Swift modules; CMake/GN/Ninja for native dependencies where needed |
| Tests | XCTest, protocol unit tests, keyboard mapping tests, and integration tests on a supported macOS target; Mac mini is the primary test device and MacBook is an additional compatibility target |
| Localization resources | Xcode String Catalog (`.xcstrings`), with English as the only shipped locale in the MVP |

### 3.3. Shared Protocol and Build Decisions

- Store the canonical control schema in `remote-windows-host/protocol/` as `.proto` files.
- Store a versioned, checksum-verified snapshot in `remote-windows-client/Protocol/Contract/`.
- Require protocol version negotiation during handshake.
- Carry video on WebRTC media tracks and control messages on DataChannel.
- Host a small signaling server inside the Windows Host, bound only to the selected LAN interface. Do not require a third machine or server.
- Keep audio streaming, remote clipboard synchronization, file transfer, and full-desktop streaming out of the MVP.
- Shortcut remapping runs on the macOS Client before keyboard events are sent. The Windows Host receives normalized target key events and does not need the Client's UI preferences or localized labels.

---

## 4. Project Architecture and Suggested Layout

### 4.1. Windows Host

```text
windows-host/
  CMakeLists.txt
  app/
    main.cpp
    AppController.*
    TrayController.*
  catalog/
    ApplicationCatalog.*
    StartMenuScanner.*
    PackageAppScanner.*
    ApplicationLauncher.*
  windows/
    WindowRegistry.*
    WindowEventMonitor.*
    WindowRelationshipTracker.*
    AuxiliaryWindowTracker.*
    WindowGeometry.*
    WindowState.*
  capture/
    WindowCaptureSession.*
    CaptureFramePool.*
    FrameScheduler.*
  video/
    VideoEncoderFactory.*
    EncoderSession.*
    VideoStreamRegistry.*
  network/
    SignalingServer.*
    PeerConnectionManager.*
    DataChannelRouter.*
    PairingManager.*
  input/
    InputController.*
    CoordinateMapper.*
  power/
    SystemPowerController.*
  security/
    AuthorizationPolicy.*
    LocalFirewallInstructions.md
  resources/
    en-US.rc
  diagnostics/
    Logger.*
    MetricsCollector.*
  tests/
```

This is a guide, not a requirement to create empty files in advance. Keep only modules with real responsibilities.

### 4.2. macOS Client

```text
macos-client/
  RemoteWindows.xcodeproj
  App/
    RemoteWindowsApp.swift
    AppDelegate.swift
    AppController.swift
  Catalog/
    ApplicationCatalogView.swift
    ApplicationCatalogModel.swift
    ApplicationLaunchService.swift
  RemoteWindows/
    RemoteWindowController.swift
    RemoteWindowRegistry.swift
    RemoteWindowRelationshipCoordinator.swift
    ModalWindowCoordinator.swift
    VideoSurfaceView.swift
    ResizeController.swift
    InputRouter.swift
  Keyboard/
    ShortcutAdaptationService.swift
    BuiltInShortcutCatalog.swift
    CustomShortcutStore.swift
    ShortcutRuleValidator.swift
    KeyboardEventTranslator.swift
    KeyboardShortcutsSettingsView.swift
  Network/
    ConnectionManager.swift
    PeerConnectionBridge.mm
    SignalingClient.swift
    ProtocolAdapter.swift
  Performance/
    FrameRateController.swift
    DisplayMonitor.swift
    PerformanceMetrics.swift
  Power/
    PowerControlsView.swift
    PowerCommandService.swift
  Settings/
    SettingsView.swift
    Preferences.swift
  Security/
    PairingView.swift
    CredentialStore.swift
  Resources/
    Localizable.xcstrings
  Tests/
```

### 4.3. Two-Repository Model — Mandatory

The product is maintained in exactly two Git repositories. Do not create a third repository for shared code, the protocol, tests, or project documentation.

#### Repository A: `remote-windows-host`

Owns the Windows Host implementation and the canonical network protocol contract.

```text
remote-windows-host/
  README.md
  CONTRIBUTING.md
  LICENSE
  .gitignore
  CMakeLists.txt
  windows-host/
    app/
    catalog/
    windows/
    capture/
    video/
    network/
    input/
    power/
    security/
    resources/
    diagnostics/
    tests/
  protocol/                         # canonical source of truth
    remote_windows.proto
    VERSION
    test-vectors/
    README.md
  tools/
    export_protocol.ps1
    export_protocol.sh
  docs/
    STATUS.md
    ARCHITECTURE.md
    DEPENDENCIES.md
    PROTOCOL.md
    SECURITY.md
    TEST_PLAN.md
    PERFORMANCE.md
    decisions/
```

The `CONTRACT` subagent is the only role allowed to modify `protocol/remote_windows.proto`, `protocol/VERSION`, the canonical test vectors, and the protocol documentation. Host feature agents consume the released schema and may request changes but must not make direct schema edits.

#### Repository B: `remote-windows-client`

Owns the macOS Client implementation and a pinned, verifiable snapshot of the Host-owned protocol contract.

```text
remote-windows-client/
  README.md
  CONTRIBUTING.md
  LICENSE
  .gitignore
  RemoteWindows.xcodeproj
  macos-client/
    App/
    Catalog/
    RemoteWindows/
    Keyboard/
    Network/
    Performance/
    Power/
    Settings/
    Security/
    Resources/
    Tests/
  Protocol/
    Contract/
      remote_windows.proto          # exported snapshot; never edited manually
      VERSION                       # exact Host protocol version/tag
      SHA256SUMS                    # integrity check for the snapshot
    Generated/                      # generated Client bindings if checked in
  tools/
    sync_protocol_from_host.sh
  docs/
    STATUS.md
    ARCHITECTURE.md
    DEPENDENCIES.md
    CLIENT_PROTOCOL_INTEGRATION.md
    SECURITY.md
    TEST_PLAN.md
    PERFORMANCE.md
    LOCALIZATION.md
    KEYBOARD_SHORTCUTS.md
    decisions/
```

The Client contract snapshot is imported from an exact Host tag/release and verified against its checksum. The Client `C-CONTRACT` agent may update the snapshot only by running the approved sync tool against a specified Host tag. It must never hand-edit `.proto` files or independently invent message fields. Store the Host tag/commit and checksum in the Client repository so each Client commit is reproducible.

#### Protocol change workflow

1. Any agent that needs a wire change submits a short `Protocol Change Request`: reason, proposed fields/semantics, compatibility impact, tests, and affected requirements.
2. `CONTRACT` reviews the proposal and modifies the canonical schema in the Host repository only if approved.
3. `CONTRACT` updates `protocol/VERSION`, test vectors, compatibility tests, and `docs/PROTOCOL.md`; it exports a versioned contract bundle and tags/releases it.
4. Host integration consumes the new canonical schema and proves the Host build/tests pass.
5. `C-CONTRACT` imports the exact Host tag into the Client repo using `tools/sync_protocol_from_host.sh`, verifies `SHA256SUMS`, regenerates bindings, and updates the pinned contract metadata.
6. Client integration builds/tests against that schema version. `ORCH` does not mark the change integrated until both repositories agree on the protocol version.
7. Breaking changes require an explicit protocol-version change and a compatibility note. Never silently modify a schema while retaining the same version.

#### Cross-repository integration workspace

Developers and QA may create a local workspace containing two sibling clones, for example:

```text
remote-windows-workspace/
  remote-windows-host/       # Git repository A
  remote-windows-client/     # Git repository B
```

This workspace is not a third Git repository. Integration scripts must accept both paths as inputs and must not assume that the repositories share a parent Git root. Store shared acceptance reports in the appropriate repository's test/report directory, or attach them to the integration task/release record. Do not put product source files in the workspace root.


---

## 5. Network Protocol

### 5.1. Minimum Control Messages

| Message | Direction | Purpose |
|---|---|---|
| `HELLO` | Bidirectional | Protocol version, app versions, capabilities |
| `PAIRING_CHALLENGE` / `PAIRING_RESULT` | Bidirectional | Explicit pairing and trust confirmation |
| `APP_CATALOG_REQUEST` | Client → Host | Request catalog |
| `APP_CATALOG` | Host → Client | Catalog entries with `app_id`, name, type, and icon/reference |
| `APP_CATALOG_CHANGED` | Host → Client | Notify Client to refresh/invalidate catalog cache |
| `LAUNCH_APP` | Client → Host | Launch an application by `app_id` |
| `LAUNCH_RESULT` | Host → Client | Launch result or error |
| `APP_STATUS` | Host → Client | Application running/closed/unknown state |
| `WINDOW_ANNOUNCE` | Host → Client | Announce a new main or auxiliary window/stream, including application-session ID, role, parent/owner `window_id`, modality, geometry, and metadata |
| `WINDOW_UPDATE` | Host → Client | Title, geometry, active state, visibility, modality, owner relationship, or window state changed |
| `WINDOW_REMOVE` | Host → Client | Source window closed or became unavailable; remove only the corresponding remote representation and related resources |
| `INPUT_EVENT` | Client → Host | Mouse or normalized Windows keyboard event, including `window_id` and sequence number |
| `RESIZE_REQUEST` | Client → Host | Desired client-area width/height in pixels |
| `RESIZE_ACK` | Host → Client | Actual source dimensions and operation result |
| `WINDOW_ACTION` | Client → Host | Allow-listed action: activate, minimize, restore, stop sharing, or request closure/dismissal of an auxiliary source window |
| `WINDOW_ACTION_RESULT` | Host → Client | Result of an auxiliary-window close/dismiss request; the source representation remains visible if the request fails |
| `FRAME_RATE_REQUEST` | Client → Host | Target FPS and reason for change |
| `STREAM_STATUS` | Host → Client | Stream status, resolution, actual FPS, and error information |
| `SYSTEM_POWER_REQUEST` | Client → Host | Only enum values `SLEEP` or `SHUTDOWN` |
| `SYSTEM_POWER_RESULT` | Host → Client | `accepted`, `denied`, `failed`, error code, and safe description |
| `HEARTBEAT` | Bidirectional | Connection health |
| `PERFORMANCE_STATS` | Host → Client | FPS, bitrate, dropped frames, capture/encode latency |

Shortcut mapping settings remain on the macOS Client. After a rule is matched, the Client sends the normalized target combination as ordered Windows key events. Do not send user-defined shell strings, executable paths, or arbitrary commands over the protocol.

Each request must have a `request_id`, protocol version, typed result/error, and timeout. Use sequence numbers or idempotency keys for state-changing commands where duplicate execution could be harmful.

### 5.2. Protocol Rules

- Every remote window, including a dialog or picker, uses a UUID `window_id`, independent of the native `HWND`.
- Each `WINDOW_ANNOUNCE` includes an `application_session_id` and, when known, `parent_window_id`, `owner_window_id`, a language-neutral `window_role`, modality state, visibility, and geometry. Relationship IDs refer to other remote windows when those windows are being streamed; otherwise they may be absent while the application-session association remains present.
- Auxiliary windows discovered after the main window has been shared must be announced automatically, even when they have a different PID, and must not require another `LAUNCH_APP` or share-selection command.
- The Client maintains the relationship graph and uses it to order windows, coordinate modal state, route input, and remove the correct window when a source disappears.
- If the user requests closure of a remote modal/auxiliary window, send an explicit allow-listed window action. Do not optimistically hide the Client window until Host-side closure is confirmed by `WINDOW_REMOVE` or an explicit result. If closure fails, keep the remote representation visible and report the failure. For main windows, closing the Client presentation remains distinct from closing the Windows application.
- Each launchable application uses a Host-generated `app_id` resolved only through the local catalog.
- Do not automatically retry `SYSTEM_POWER_REQUEST` after a timeout. Report an unknown result instead of potentially sending shutdown twice.
- Coalesce obsolete resize commands and pointer-move events, but never reorder or drop key/button down/up events.
- Validate strings, lists, icon payloads, enums, message lengths, protocol versions, and numeric ranges.
- Keyboard event messages must specify target key identity, modifier state, key-down/key-up state, repeat information where applicable, and a sequence/order value.
- Ensure that a disconnect or rejected event cannot leave keys or mouse buttons logically pressed in the Host.

---

## 6. Keyboard Shortcut Adaptation — Implementation Notes

### 6.1. UI Requirements

Add `Settings → Keyboard Shortcuts` to the macOS Client. It contains:

- A master `Adapt macOS shortcuts for Windows` toggle.
- A Built-in Mappings section with one toggle per mapping.
- A Custom Mappings section with add, edit, enable/disable, reorder, and delete actions.
- A clear source-to-target display, such as `⌘C → Ctrl+C`.
- Validation for duplicate or conflicting source combinations.
- A notice that macOS system-reserved shortcuts may remain local because they can be intercepted before the Client receives them.

The default setting is to adapt the enabled built-in shortcuts. Turning off the master switch disables all remapping but does not delete user settings. Turning off a single built-in rule affects only that rule.

### 6.2. Matching and Rule Priority

Use this priority:

1. A valid enabled custom mapping whose scope matches the current application.
2. A valid enabled built-in mapping.
3. Baseline key/text translation with no shortcut remapping.

If two enabled custom rules have the same source combination and overlapping scope, flag the conflict. Do not silently resolve it by array order. Rule matching must use a stable key-code/modifier representation and must preserve key-up/key-down ordering.

The MVP should support single-chord shortcuts (for example, `Command+Shift+S`). Sequences involving several successive chords may be added later and must not block first-version delivery.

### 6.3. Application-Specific Differences

Some Windows shortcuts vary by application. Keep ambiguous built-ins, such as `Command+Q → Alt+F4`, disabled by default. Allow the user to disable every built-in rule and override it with a custom rule. If per-application custom scope is included in the MVP, resolve it through the Host's `app_id`; never match on localized window titles alone.

Do not claim that a built-in mapping works identically in every application. Test the target applications, including File Explorer, Access, and SEE Electrical, and document exceptions.

### 6.4. Testing Requirements

Add unit tests for:

- Every built-in mapping, including toggle-off behavior.
- Master toggle behavior.
- Custom-rule create/edit/delete/enable/disable persistence.
- Custom-over-built-in precedence.
- Duplicate/conflict validation.
- Correct key-down and key-up ordering.
- Modifier release on focus loss and connection loss.
- Unmatched ordinary text input.
- Layout-sensitive keys and supported keyboard layouts.
- Reserved macOS shortcuts that must remain local.

Add integration tests proving that copy, cut, paste, select-all, undo, redo, save, open, print, and close behavior reaches the expected target application when the respective mapping is enabled.

---

## 7. System Power Behavior

### 7.1. macOS Client UI

Provide separate device actions:

- `Sleep Windows PC…`
- `Shut Down Windows PC…`

Both show a confirmation dialog naming the target PC. Shutdown must warn about unsaved data and explain that the Client cannot reconnect until the Host is started again.

Confirmation occurs before sending the command. After confirmation, the request may no longer be cancellable. `Cancel` closes the dialog without transmitting anything.

### 7.2. Windows Host Implementation

`SystemPowerController` must:

1. Verify the connection is paired and authorized for power control.
2. Validate that the action is a known enum; reject unknown values.
3. Check and, where appropriate, enable `SE_SHUTDOWN_NAME` for the current process using `OpenProcessToken`, `LookupPrivilegeValue`, and `AdjustTokenPrivileges`. After `AdjustTokenPrivileges`, check `GetLastError() == ERROR_NOT_ALL_ASSIGNED`.
4. For sleep, call `SetSuspendState(FALSE, FALSE, FALSE)` from `PowrProf.dll`, handle failures, and return safe diagnostics.
5. For shutdown, use `InitiateSystemShutdownExW` or another verified Microsoft API. In normal mode set `bForceAppsClosed = FALSE` and `bRebootAfterShutdown = FALSE`, and provide an appropriate planned reason code.
6. Do not run as a `SYSTEM` service, automatically elevate, or bypass UAC/local policy.
7. Return `accepted` if the request was accepted, not a false claim that shutdown has completed.
8. Write diagnostic results locally without secrets, application content, or typed text.

After sleep or shutdown is accepted, connection loss is expected. Do not automatically resend a power request after a timeout. Wake-on-LAN is out of scope for the MVP.

---

## 8. Frame Rate and Latency Management

### 8.1. Target FPS Policy

On the Client, identify the display containing `NSWindow` and read its `NSScreen.maximumFramesPerSecond`. Recalculate when a window moves to another display. Use a safe fallback if `window.screen` is unavailable.

Example policy:

```text
if window_is_minimized and minimized_policy == pause:
    target_fps = 0
else if window_is_active:
    target_fps = min(screen_max_fps, encoder_limit, user_active_cap_if_configured)
else:
    target_fps = inactive_fps_setting
```

`inactive_fps_setting` is configurable in the Client and defaults to 10 FPS. A proposed allowed range is 1–60 FPS. The active target is a ceiling, not a guarantee of unique frames at that rate.

### 8.2. Reduce Load and Queueing

- Process only new frames and apply a frame-rate gate before encoding.
- Avoid queues of stale frames. Under load, prefer dropping obsolete frames over increasing interaction latency.
- Track WebRTC queue depth, encode time, bitrate, and dropped frames.
- Do not compute per-frame CPU pixel hashes without measuring their cost.
- On stream resume or a significant rate change, request a keyframe/update if needed.
- Benchmark H.264 GOP, bitrate, and low-latency settings on the target GPU instead of guessing.

---

## 9. Development Stages

### Stage 0. Environment and Architecture Decisions

- Inspect available Windows/macOS build hosts and target OS versions.
- Pin Windows SDK, Xcode/SDK, MSVC, CMake, GN/Ninja, protobuf, and WebRTC revisions.
- Create/update `README.md`, `docs/STATUS.md`, `docs/ARCHITECTURE.md`, and `docs/DEPENDENCIES.md`.
- Add ADRs for transport, codec, application catalog, power control, keyboard shortcut adaptation, and localization-ready UI.
- Define test devices and capture a baseline latency measurement plan.

**Done when:** reproducible instructions exist, dependencies are pinned, and platform/build limitations are documented.

### Stage 1. One-Window End-to-End Prototype

- Windows Host accepts a local connection.
- Capture one selected window with `Windows.Graphics.Capture`.
- Encode and send H.264 over direct WebRTC.
- macOS Client decodes and shows the stream in one `NSWindow`.
- Add timestamps for capture, encode, receive, decode, and presentation.

**Done when:** a real Windows window is visible on macOS over LAN with no Internet; video does not go through an external server; basic timing metrics are recorded.

### Stage 2. Application Catalog and Launch

- Scan per-user and common Start Menu entries and packaged application sources.
- Normalize, deduplicate, and cache catalog records.
- Support manual application entries.
- Implement `LAUNCH_APP` by `app_id`.
- Test File Explorer, Microsoft Access, and SEE Electrical.
- Handle process startup, existing instances, multiple windows, and delayed window appearance separately.

**Done when:** all three target applications can be found and launched from macOS; the Host refuses arbitrary paths or commands supplied by the Client.

### Stage 3. Multiple Windows

- Implement the Host window registry, including the relationship tracker for owners, owned windows, and app-associated secondary windows.
- Create independent capture/encoder sessions per independently rendered source.
- Map each `window_id` to a video track and include parent/owner, application-session, role, and modality metadata.
- Automatically create and remove corresponding macOS `NSWindow` instances.
- Automatically discover and publish application-created dialogs and auxiliary windows; do not require the user to select each one separately.
- Keep a parent stream alive when a modal dialog opens; close only the dialog stream when that dialog closes.
- Handle source app/window closure without disrupting other streams.

**Done when:** File Explorer, Access, and SEE Electrical are visible simultaneously as separate macOS windows, and ordinary secondary dialogs opened from those applications appear automatically without manual sharing. Closing one window does not break unrelated streams.

### Stage 3A. Automatic Auxiliary-Window and Dialog Mirroring

This is a mandatory MVP sub-stage, not a post-MVP enhancement.

- Track `EVENT_OBJECT_CREATE`, `EVENT_OBJECT_SHOW`, `EVENT_OBJECT_DESTROY`, `EVENT_OBJECT_NAMECHANGE`, `EVENT_OBJECT_LOCATIONCHANGE`, focus/foreground changes, and other events required by testing.
- Resolve owner/parent/root-owner relationships and correlate brokered dialogs that do not share the originating application's PID. Use supported Win32 APIs and UI Automation where necessary; document and test the association strategy.
- When a shared app opens a standard Open/Save dialog, folder picker, overwrite confirmation, print/export dialog, or app-specific modal window, automatically create a capture session and send `WINDOW_ANNOUNCE` with its relationship and modality metadata.
- On macOS, automatically show the dialog above the correct parent, route input to the dialog, prevent interaction with a blocked modal parent, and preserve correct focus/z-order behavior.
- When the Host-side dialog closes, send `WINDOW_REMOVE` and restore the parent window's normal interaction state.
- When the user dismisses a remote auxiliary window on the Client, request source-side closure and wait for Host confirmation rather than only hiding the local copy.
- Avoid duplicate rendering when a popup is already part of the captured parent surface. Capture independently rendered popups when the platform exposes them as a separately capturable window.
- Do not treat a different PID as sufficient reason to exclude a dialog. Do not claim support for UAC secure desktop or protected system surfaces outside the ordinary user session.

**Required test scenarios:** trigger Open, Save As, Select Folder, overwrite confirmation, and a second nested confirmation from File Explorer, Microsoft Access, and SEE Electrical wherever the application exposes those workflows. Verify that every ordinary visible dialog appears automatically on the Client, remains interactive, blocks the parent when modal, and disappears only after it closes on the Host.

**Done when:** the above scenarios pass with no manual dialog-sharing step, the window relationship graph remains correct, and dismissal never leaves an invisible modal dialog blocking the Windows application.

### Stage 4. Input and Keyboard Shortcut Adaptation

- Implement pointer, button, wheel, and keyboard input.
- Add coordinate mapping and DPI handling.
- Implement focus and modifier management.
- Add built-in shortcut rules, independent per-rule toggles, and the master adaptation toggle.
- Add custom shortcut mapping editor, persistence, conflict validation, and custom-over-built-in precedence.
- Implement normalized Windows key events and safe release on focus/network loss.
- Test context menus, standard Open/Save/folder dialogs, nested confirmation prompts, scrolling, text input, and shortcuts in each target application.
- Verify that a modal dialog receives input and its blocked parent does not receive it until the dialog closes.
- Verify that dismissing a Client-side dialog sends a source-close request and that the Client waits for Host-side confirmation.
- Document limitations for elevated applications and the secure desktop.

**Done when:** built-in mappings can be individually enabled/disabled, custom mappings work and persist, keyboard mapping tests pass, and supported base interactions work in each target application.

### Stage 5. Resize and Full Screen

- Implement local scaling.
- Implement synchronized resize and acknowledge actual source geometry.
- Scale the last frame immediately during interactive resizing.
- Support macOS full-screen mode and moving windows between displays.
- Recreate capture/encoder resources when source size changes.

**Done when:** each target window can resize independently and enter full screen without prolonged freezing.

### Stage 6. Dynamic FPS

- Add the `inactive_fps` setting in the Client, default 10.
- Set active-window target FPS from `NSScreen.maximumFramesPerSecond`.
- Limit inactive streams to the configured rate.
- Support minimized-window pause or a configured low FPS.
- Apply the frame-rate gate before encoding and measure its effect on bitrate and CPU/GPU load.

**Done when:** focus changes the appropriate stream FPS, inactive streams continue at the configured low rate, and queues do not accumulate stale frames.

### Stage 7. Power Control

- Add Sleep and Shut Down commands and confirmations to the Client UI.
- Add typed power enums to the protocol.
- Implement privilege and error checks in the Host's `SystemPowerController`.
- Do not force-close applications by default.
- Test connection loss and ensure the Client does not automatically resend a power command after a timeout.

**Done when:** a paired Client can request sleep and shutdown, privilege errors are clear, and no arbitrary remote command execution is possible.

### Stage 8. Localization Readiness and English UI Audit

- Ensure all visible UI strings use centralized resource keys.
- Ship only the English resource set in the MVP.
- Audit both apps for hard-coded user-facing strings, including errors, menus, dialogs, tooltips, status labels, and shortcut descriptions.
- Ensure protocol identifiers, logs, enums, and rule IDs are language-neutral.
- Document how to add a future locale without changing core business logic.

**Done when:** all MVP UI is English and visible text can be translated by adding resources rather than editing business logic.

### Stage 9. Security and Offline Operation

- Implement explicit device pairing and trust storage.
- Protect signaling and control traffic.
- Validate all control messages, lengths, IDs, and enums.
- Configure Windows Firewall guidance for the LAN.
- Test with WAN/Internet disconnected, external DNS unavailable, and no external STUN/TURN.
- Verify logs do not contain secrets or window contents.

**Done when:** every MVP feature passes the LAN-only test and unpaired clients cannot launch applications, inject input, or control system power.

### Stage 10. Performance and Reliability

- Measure latency, bitrate, FPS, dropped frames, CPU/GPU, and memory with 1, 3, and 5 windows.
- Test wired Gigabit Ethernet and Wi-Fi if Wi-Fi is a target deployment mode.
- Test disconnection/reconnection, interface changes, app restarts, window closure, and graphics-device loss.
- Eliminate unbounded queues, leaks, and unreleased capture/encoder sessions.
- Package both applications and prepare troubleshooting instructions.

**Done when:** results are saved in `docs/performance/`, failures are reproducible, and known limitations are clearly documented.

---

## 10. Testing and Acceptance Criteria

### 10.1. Functional Matrix

| Test | Expected result |
|---|---|
| LAN without Internet | Pairing, catalog, launch, video, input, shortcut adaptation, and power control work without external services |
| Application catalog | File Explorer, Access, and SEE Electrical appear when valid launch entries are available |
| Launch | Selected applications can be launched from the Client without switching to the Windows monitor |
| Multiple windows | Three target applications stream concurrently as independent `NSWindow` instances |
| Automatic auxiliary windows | Open/Save/folder dialogs and associated prompts from a shared app appear automatically without a separate share action |
| Modal dialog relationships | Dialog appears above its parent, receives input, and blocks parent interaction while modal |
| Dialog dismissal | Closing/canceling the Host-side dialog removes its Client representation and restores parent interaction; Client-side dismissal requests Host closure and waits for confirmation |
| Cross-process dialogs | A supported dialog is associated with its shared app even when hosted by a different process; PID alone is not used to reject it |
| Window closure | Associated stream/resources stop; other windows remain functional |
| Resize | Local scaling and synchronized resize behave independently |
| Full screen | Remote window fills the display and updates its FPS target correctly |
| Focus change | Active stream uses the display target; inactive streams use configured FPS |
| Minimize/restore | Configured minimized policy applies; stream resumes correctly after restoration |
| Mouse/keyboard | Coordinates and clicks remain correct at different DPI scales; no stuck keys/buttons |
| Built-in shortcuts | Each built-in mapping works when enabled and stops remapping when disabled |
| Master shortcut toggle | Disabling adaptation restores baseline key translation without deleting mappings |
| Custom shortcuts | Create, edit, disable, delete, persist, validate conflicts, and override built-ins |
| Ordinary typing | Text entry and supported keyboard layouts work when no shortcut rule matches |
| Sleep | After confirmation, the Host requests sleep and the disconnection is treated as expected |
| Shutdown | After confirmation, shutdown is requested without force-closing apps by default |
| Insufficient privileges | Clear error is returned; no privilege bypass occurs |
| Unpaired Client | Application launch, input, and power requests are rejected |
| Malformed protocol data | No crash or arbitrary execution occurs |
| English-only UI | All visible text is English, with centralized resource keys ready for future translations |

### 10.2. Performance Metrics

Collect and distinguish:

- capture-to-encode latency;
- encode time;
- network/transport timing;
- decode time;
- decode-to-present timing;
- action-to-visible-response latency for keyboard and pointer input;
- per-stream FPS and dropped frames;
- per-stream and total bitrate;
- CPU/GPU utilization and memory;
- time from launch command to application process start;
- time from launch command to first remote window and first displayed frame.

For the first lab baseline, use 1920×1080, 60 FPS on one active window, wired Gigabit Ethernet, and three concurrent windows. Establish a numeric latency objective from measurement rather than promising it in advance. An initial experiment may test whether frame capture-to-present p95 ≤ 25 ms is achievable on the actual hardware; this is a hypothesis to measure, not a guaranteed acceptance result.

### 10.3. Power Tests

Test on a dedicated test PC, not on a workstation with unsaved work:

- Sleep from a normal interactive user session.
- Failure of `SetSuspendState` and correct error reporting.
- Shutdown with no open applications.
- Shutdown while an app has unsaved changes: confirm applications are not forcibly closed and observe actual Windows behavior.
- Missing `SE_SHUTDOWN_NAME` privilege.
- Connection loss immediately after power-request acceptance.
- Client restart does not automatically reissue a shutdown request.

### 10.4. Keyboard Mapping Tests

Test at least:

- Every built-in mapping on and off.
- Master toggle on and off.
- Custom rule precedence over a built-in rule.
- Duplicate source-combination detection.
- Modifier and key-up ordering.
- Input-state reset after focus or connection loss.
- Baseline typing without a mapping match.
- Behavior with the configured keyboard layout.
- macOS-reserved shortcuts remain local and are described accurately.
- Actual behavior in File Explorer, Access, and SEE Electrical.

---

## 11. Dependency and Build Requirements

### General

- Pin every dependency to an exact version or commit.
- Include licenses and third-party notices.
- Document how to prepare dependencies on an Internet-connected build machine and transfer them to an offline LAN.
- Runtime must not download dependencies or updates.
- Record reproducible Windows and macOS build commands in `README.md`.
- WebRTC may require GN/Ninja and a dedicated build process; do not assume it can be built with plain `cmake --build` without a prepared wrapper/artifact.
- Run unit tests in CI where possible. Platform integration tests require actual Windows 11 Pro and a real Mac; clearly mark any platform that was unavailable.

### Windows Host Build Checklist

- Install Visual Studio with Desktop development with C++, MSVC, and the required Windows SDK.
- Verify CMake/Ninja and the selected toolchain.
- Build WebRTC/adapter using the pinned instructions.
- Build the Host in Debug and Release.
- Run unit tests and verify the Host operates in a Windows 11 Pro user session.
- Verify Start Menu scanning and launch of target apps.
- Verify power privilege errors and Windows Firewall behavior on the LAN.

### macOS Client Build Checklist

- Verify Xcode and the active command-line developer directory.
- Build the Swift/AppKit UI.
- Build or link a compatible macOS WebRTC framework.
- Verify the Objective-C++ bridge and supported Mac architectures.
- Run XCTest and verify Metal/VideoToolbox on the Mac mini. When a MacBook is available as a second supported target, run the documented compatibility smoke tests there as well.
- Test an external monitor with a different refresh rate.
- Test keyboard adaptation and preferences persistence across Client restarts.
- Audit the UI to ensure all visible strings use English resource keys.

---

## 12. Decisions Requiring Measurement or Explicit Documentation

1. **One PeerConnection or several:** Start with one PeerConnection and multiple video tracks for the device pair. Compare alternatives if stream isolation, recovery, or latency is poor. Document measurements.
2. **Hardware H.264:** Verify that encoding and decoding are actually hardware accelerated on target GPUs/Mac hardware. Show diagnostics if not; do not infer acceleration from the API name alone.
3. **Minimized-window capture:** Verify actual `Windows.Graphics.Capture` behavior for each state. Do not promise frames if Windows stops producing them.
4. **Application catalog completeness:** Define and document the data sources. Do not claim that every executable file is discovered.
5. **Background input:** Do not assume `PostMessage` or `SendInput` works with every application. Test and document compatibility.
6. **Keyboard shortcuts:** Do not promise that macOS can forward system-reserved shortcuts that the OS intercepts before the Client receives them. Test the default mappings and expose toggles for application-specific rules.
7. **Secure desktop/UAC:** Do not claim support for secure desktop or system windows outside the normal interactive session.
8. **Auxiliary-window association:** Do not rely solely on PID or window titles to associate dialogs. Use owner/root-owner relationships and tested event/accessibility correlation. Ordinary application-owned and app-associated file dialogs remain mandatory MVP cases; document only genuine system/security-surface exclusions.
9. **Shutdown:** API acceptance does not prove that shutdown completed. Open documents and Windows policy may block it.
10. **FPS:** `NSScreen.maximumFramesPerSecond` is the display's supported maximum, not the actual unique-frame rate of the remote stream.
11. **Localization:** English-only UI does not mean English strings may be embedded in business logic; centralized resources are required from the MVP.

---

## 13. First Tasks for the Coding Agent

Proceed in this order, using the development/runtime topology in Section 18 as a prerequisite:

1. Confirm Hermes Agent and the local LLM run on the Mac mini; record their version/model/provider and pass the tool-use preflight without storing credentials.
2. Confirm the Windows PC and Mac mini roles and LAN reachability. Record OS/build-tool versions and local workspace paths. Treat the MacBook as an optional secondary Client test target if available; do not block core development on its availability.
3. Clone or inspect exactly two repositories on the Mac mini: `remote-windows-host` and `remote-windows-client`.
4. Configure and test the approved remote build/test runner on Windows. Prove that a harmless operation works and that a specific Host commit can be built with an exact SHA reported in the result.
5. Keep Hermes' normal terminal backend on `local`; use the explicit Windows runner wrapper for remote build/test commands. Verify local worktree isolation inside each repository.
6. Assign `H-FOUND` and `C-FOUND` separate scaffold tasks so the build systems and `docs/STATUS.md` files are independent.
7. Assign `CONTRACT` to create the canonical protocol schema and versioning/export procedure in the Host repository.
8. Assign `C-CONTRACT` to import a pinned schema snapshot from a Host tag; do not create a shared protocol repository.
9. Pin a WebRTC revision and document reproducible builds independently in each repository.
10. Implement direct LAN signaling and pairing, then the one-window end-to-end slice with latency metrics. Verify that runtime media goes directly between the Windows Host and whichever Mac is running the Client. The Hermes process must not proxy or relay media.
11. Add the Host application catalog/launcher and Client catalog UI against the approved versioned contract.
12. Add multiple windows and mandatory automatic auxiliary/modal dialog mirroring.
13. Add input, built-in and custom shortcut adaptation, resize, full-screen mode, and dynamic FPS.
14. Add sleep and shutdown with confirmation and authorization checks; use mocks by default and never shut down the only runner without explicit approval and a recovery plan.
15. Run independent QA and security review across the two clones; audit English resources and offline operation.
16. Update the Host and Client `docs/STATUS.md` independently after each stage, using only verified outcomes.

Do not begin with a visually polished UI. Prioritize architecture, end-to-end operation, input correctness, direct runtime traffic, and measurable latency.

---

## 14. Official Technical References

Use these references as starting points and verify current SDK/API requirements during implementation.

### Microsoft

- [Windows.Graphics.Capture — screen capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [`IGraphicsCaptureItemInterop::CreateForWindow`](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [`GraphicsCaptureSession.IsBorderRequired`](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.isborderrequired)
- [`GraphicsCaptureSession.MinUpdateInterval`](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.minupdateinterval)
- [`IShellLinkW`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishelllinkw)
- [`ShellExecuteExW`](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw)
- [`IApplicationActivationManager::ActivateApplication`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iapplicationactivationmanager-activateapplication)
- [`SetSuspendState`](https://learn.microsoft.com/en-us/windows/win32/api/powrprof/nf-powrprof-setsuspendstate)
- [`InitiateSystemShutdownExW`](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-initiatesystemshutdownexw)
- [System shutdown functions](https://learn.microsoft.com/en-us/windows/win32/shutdown/system-shutdown-functions)
- [`SendInput`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
- [`SetWindowPos`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)
- [`GetWindow`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindow)
- [`GetAncestor`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getancestor)
- [`SetWinEventHook`](https://learn.microsoft.com/en-us/windows/desktop/api/winuser/nf-winuser-setwineventhook)
- [Common Item Dialog (Open/Save)](https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog)
- [UI Automation overview](https://learn.microsoft.com/en-us/windows/win32/winauto/entry-uiauto-win32)
- [`Get-StartApps`](https://learn.microsoft.com/en-us/powershell/module/startlayout/get-startapps)

### Apple

- [`NSWindow`](https://developer.apple.com/documentation/appkit/nswindow)
- [`NSScreen.maximumFramesPerSecond`](https://developer.apple.com/documentation/appkit/nsscreen/maximumframespersecond)
- [VideoToolbox](https://developer.apple.com/documentation/videotoolbox)
- [Metal](https://developer.apple.com/documentation/metal)
- [Xcode String Catalogs](https://developer.apple.com/documentation/xcode/localizing-and-varying-text-with-a-string-catalog)

### WebRTC

- [WebRTC peer connections](https://webrtc.org/getting-started/peer-connections)
- [WebRTC native APIs](https://webrtc.github.io/webrtc-org/native-code/native-apis/)

---

## 15. Multi-Agent Delivery and Repository Acceptance

### 15.1 Repository rules

- [ ] Exactly two Git repositories exist for product code: `remote-windows-host` and `remote-windows-client`.
- [ ] Each repository can be cloned, built, tested, and versioned independently, subject to its documented SDK/dependency requirements.
- [ ] No source file is stored only in a third shared repository.
- [ ] The Host repository is the canonical owner of the protocol schema; the Client pins a tagged snapshot and checksum.
- [ ] Each repository has its own CI/build status, dependency lock/pin documentation, release notes, and `docs/STATUS.md`.
- [ ] Each repository has a concise root `AGENTS.md` and only the project-local Hermes skills required for its modules.
- [ ] Hermes task reports, branches, and test evidence are recorded; no claim depends on conversational memory alone.

### 15.2 Subagent rules

- [ ] Every task has exactly one accountable implementation owner and a written scope.
- [ ] Each subagent receives a bounded context packet and explicit read/write paths.
- [ ] No two concurrently active agents write to the same file or module.
- [ ] Cross-scope changes are requested from the owning agent; they are not opportunistically applied.
- [ ] Protocol changes go through `CONTRACT`, are versioned, and are synchronized to the Client from an exact Host tag.
- [ ] QA and Security Review report findings independently and do not silently repair production files.
- [ ] Each task report lists changed files, commands run, test results, limitations, and requested follow-up tasks.
- [ ] Orchestrator does not accept a task based only on the agent's statement; acceptance requires test/build evidence appropriate to the task.
- [ ] Local-model concurrency defaults to one active child until a measured preflight justifies more.
- [ ] Delegation worktree isolation is verified in each repository; cross-repository child writes are forbidden.

## 16. Hermes Agent + Local LLM Operating Playbook

This section is the required execution procedure for Hermes Agent. It is optimized for a local coding model whose inference throughput, tool-call reliability, and context budget may be lower than those of a hosted frontier model. The goal is reliable progress, not maximum agent count.

### 16.1. Verify the installed Hermes version first

The official release page listed **Hermes Agent v0.22.5**, released on 2026-10-22, as the latest published release checked on 2026-10-29. This is a dated baseline, not a permanent pin. Before using this plan:

1. Run `hermes --version`.
2. Check the [official release page](https://github.com/NousResearch/hermes-agent/releases) and the relevant pages in the [official Hermes documentation](https://hermes-agent.nousresearch.com/docs).
3. Record the installed version, model provider, model ID, endpoint type, and terminal backend in a local environment report. Do not store secrets or API keys in Git.
4. Confirm which delegation, worktree, skills, and context-file features are available in that installed version. If behavior differs from this playbook, follow the installed version's verified behavior and update this file with the discrepancy.

Do not use `main` documentation as proof that a feature exists in an older installed build. Do not upgrade Hermes automatically in the middle of an implementation stage; upgrade at a planned checkpoint and rerun the preflight tests.

### 16.2. Local model configuration and isolation

Use one of the supported local-model paths:

- **Hermes-managed Local Models:** select the local model through Hermes Settings → Providers → Local Models, or the corresponding `hermes model` local-model workflow. Prefer this when it already works reliably.
- **User-managed local server:** connect Hermes to an OpenAI-compatible local endpoint, such as a local `llama-server`. Keep the endpoint bound to loopback unless there is a specifically documented reason to do otherwise.

Example only for a user-managed local `llama-server`; adapt the model ID and port to the actual server. Do not overwrite an already-working Hermes profile blindly:

```yaml
model:
  provider: llamacpp
  default: "<served-model-id>"

providers:
  llamacpp:
    base_url: "http://127.0.0.1:8081/v1"
    model: "<served-model-id>"
```

If Hermes manages the local runtime, configure it through the supported UI/CLI instead of adding a competing manually managed server. If a custom endpoint is used, verify that Hermes can list/test the endpoint and send a tool-using request. The official documentation describes both the managed runtime and a user-managed `llama-server` endpoint: [Local Models](https://hermes-agent.nousresearch.com/docs/user-guide/local-models).

**Do not silently route delegated work to a hosted model.** Leave the delegation `model`, `provider`, and `base_url` unset unless there is a deliberate, documented reason to use a different model. By default, child agents inherit the parent's model/provider credentials. Confirm the actual endpoint in Hermes before coding; a “local LLM configured” note is not sufficient evidence.

Keep the app's LAN-only runtime requirement separate from the coding agent's inference endpoint. The products must not call the Internet at runtime. Hermes itself can still have optional network-capable tools, so disable or avoid external tools during offline-only engineering sessions unless online documentation research is explicitly intended.

### 16.3. Recommended Hermes delegation defaults for a single local model

Start conservatively with one active child. Multiple delegated turns still consume inference time, KV cache/context and memory, even when they use the same local server. More agents do not imply more model capacity.

Recommended `~/.hermes/config.yaml` delegation settings:

```yaml
delegation:
  max_concurrent_children: 1
  max_iterations: 20
  worktree_isolation: true
  max_spawn_depth: 1
  orchestrator_enabled: false
```

These are project recommendations, not Hermes defaults. Verify the configuration keys against the installed release before applying them. `orchestrator_enabled: false` keeps delegated children as leaf workers; the top-level Hermes session remains the coordinator. `worktree_isolation: true` is intended to protect the parent checkout when a child edits a Git repository through the local terminal backend.

Rules:

- Keep `max_concurrent_children: 1` until tool-call reliability, memory use, response time, and isolation have been tested. Increase to `2` only for independent read-only reviews or truly separate tasks after measurement. Do not use default high concurrency blindly.
- Do not increase `max_spawn_depth`; use a flat delegation tree. The main session is the only orchestrator.
- Keep delegated tasks small enough to finish within the configured iteration budget. If a task needs more than about 20 model/tool turns, split it rather than raising the limit by default.
- If worktree isolation fails or the result omits expected worktree information, stop parallel edits and inspect `pwd`, `git rev-parse --show-toplevel`, `git status --short`, branch and diff. Do not assume isolation was successful: Hermes documents that isolation is local-terminal/Git-only and may fall back when worktree creation is unavailable.
- A child inherits the enabled toolsets of its parent; do not assume per-child tool permissions can be widened or narrowed through `delegate_task`. Write-scope instructions and worktrees reduce collisions but are not a security sandbox.
- Do not start background builds or tests in a child and let it return before they finish. Child-owned background processes can be terminated during delegation teardown. Run bounded commands to completion or explicitly retain process ownership in the parent session.

Official references: [Subagent Delegation](https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/delegation.md).

### 16.4. Local-model preflight — must pass before project work

Before implementation, test the exact local model and Hermes configuration that will be used for this project. Save the results in `docs/agent-runtime/LOCAL_MODEL_PREFLIGHT.md` in the Host repository, without credentials or private prompts.

The preflight must verify:

1. Hermes identifies the intended local provider/model and can complete a normal response.
2. The model can correctly call a read/search tool to inspect one named file and report a specific fact from it.
3. The model can run a harmless terminal command, inspect its exit code, and report the actual output.
4. The model can make a minimal, reversible code or documentation edit and then inspect the diff.
5. The model can run a test/build command and accurately distinguish pass, fail, and not-run.
6. A very small `delegate_task` can complete in an isolated worktree; the parent can identify the child branch, changed files, and test evidence.
7. The model does not invent file paths, commit hashes, test results, or API behavior when the tool returns an error or no result.
8. The model and Hermes continue to work with external network access disabled, except for any intentionally invoked external documentation tool; these tools must be explicitly identified.

Use a tiny throwaway Git repository for destructive or mutation tests. Do not run these tests on production branches. If tests 2–5 fail, use Hermes as a sequential assistant with smaller tool-verified tasks. If test 6 fails, do not use delegated code editing until repaired; continue with manual single-writer tasks rather than assuming isolation.

A model that can answer coding questions is not automatically reliable at tool use. Do not start the implementation plan until the preflight report documents evidence for tool calls, edits, tests, and (if used) delegated worktrees.

### 16.5. Two-repository operating model under Hermes

Exactly two Git repositories remain authoritative:

- `remote-windows-host` — Windows Host source, canonical protocol schema, test vectors, and the canonical product development plan.
- `remote-windows-client` — macOS Client source and a pinned, checksum-verified snapshot of the Host-owned protocol contract.

Do not add a workspace Git repository, shared-code repository, or protocol repository.

Because Hermes worktree isolation is based on the Git repository resolved for a session, **do not ask a child launched from a Host-root session to edit the Client repository outside its worktree**. The recommended workflow is:

1. Run Host tasks from a Hermes session whose working directory is inside `remote-windows-host`. Delegated children may edit only Host files in their isolated Host worktrees.
2. Complete and integrate the Host contract change first when a wire change is required. Export the protocol bundle from a committed Host revision/tag.
3. Start/resume a Hermes session with working directory inside `remote-windows-client` for Client implementation. Give it the precise Host contract tag/version and the task brief; it imports the contract only through the approved sync tool.
4. Do not run these two coding sessions concurrently by default when they share one local inference server. Serialize Host changes and Client synchronization to avoid inference contention and contract drift.
5. Cross-repository work items are coordinated by task IDs and commit/tag references, not by a shared writable directory. The orchestrator may inspect both clones but does not grant a subagent cross-repository write access.
6. The top-level orchestrator owns both-repository acceptance. Build Host on a real supported Windows/MSVC environment and Client on a real supported macOS/Xcode environment. Static review on the other OS does not count as a successful platform build.

If the Host repository is the source of the canonical full plan, the Client repository must not fork or independently rewrite the product requirements. Its root `AGENTS.md` points to the relevant requirement IDs and the pinned contract snapshot. If the Client session cannot read the Host plan path, include only the applicable requirement excerpts in the task brief.

### 16.6. Repository instructions and project-local skills

Hermes supports project context files, including `AGENTS.md`, and project-local skills. Use these to avoid repeatedly injecting the entire plan into every task. Official references: [Context Files](https://hermes-agent.nousresearch.com/docs/user-guide/features/context-files) and [Skills System](https://hermes-agent.nousresearch.com/docs/user-guide/features/skills).

Create and maintain these files as part of Stage 0:

**In `remote-windows-host`:**

- Root `AGENTS.md`: concise Host-specific rules, build commands, test commands, module ownership, protocol ownership and a link to the canonical plan. Keep it short; target 100–160 lines.
- `.agents/skills/host-build-test/SKILL.md`: supported Windows build and test workflow, including exact commands and expected outputs.
- `.agents/skills/host-window-capture/SKILL.md`: capture lifecycle, GPU surfaces, frame scheduling, encoder ownership and diagnostic procedure.
- `.agents/skills/host-window-relationships/SKILL.md`: WinEvent tracking, owner/root-owner relationships, brokered dialogs, modal-state rules and test cases.
- `.agents/skills/protocol-change/SKILL.md`: schema versioning, test vectors, export bundle and Client snapshot workflow.
- `.agents/skills/host-security-power/SKILL.md`: pairing, typed power commands, privilege validation and safe failure handling.

**In `remote-windows-client`:**

- Root `AGENTS.md`: concise Client-specific rules, build/test commands, UI language/localization, input/shortcut requirements and contract-snapshot rules. Keep it short; target 100–160 lines.
- `.agents/skills/client-build-test/SKILL.md`: Xcode build/test/profiling workflow.
- `.agents/skills/client-keyboard-shortcuts/SKILL.md`: shortcut mapping order, built-in toggles, custom rules, key-up/key-down correctness and reserved macOS shortcuts.
- `.agents/skills/client-remote-windows/SKILL.md`: NSWindow lifecycle, modal auxiliary-window relationships, resize/focus behavior and stream mapping.
- `.agents/skills/client-performance/SKILL.md`: VideoToolbox/Metal path, FPS policy, frame timing and latency measurements.
- `.agents/skills/client-protocol-sync/SKILL.md`: synchronizing and verifying the Host-owned protocol contract without manual edits.

Each skill must use the standard `SKILL.md` format with a clear name, description, concise instructions, and links to relevant repository docs. Keep detailed reference material in `references/` and link to it from the skill rather than loading a huge procedure into every task.

After adding project-local skills, explicitly trust the repository using the command supported by the installed Hermes version, for example `hermes skills trust <repository-path>`. Verify that the skills appear in Hermes before relying on them. Skills found in a repo are not necessarily trusted/active automatically.

Do not create a third repository for these files. Host skills belong in Host; Client skills belong in Client. Avoid copying every skill to both repositories.

### 16.7. Context budget and task sizing for a local model

The full plan is for the orchestrator. Do not pass the whole plan to every subagent. Each delegated task must receive a concise task packet with:

- task ID and a one-sentence outcome;
- one owning repository, exact branch/worktree, and exact write paths;
- requirement IDs and only the relevant excerpt(s);
- the approved contract version and dependencies already merged;
- a short list of files to inspect first;
- acceptance criteria and exact commands to run;
- prohibited scope and known platform limitations;
- a required evidence-based report format.

Practical limits for the baseline local model:

- One task = one behavior, defect, or coherent vertical slice; not one whole development stage.
- Aim for 1–3 production files plus tests. If more files are essential, explain why before expanding scope.
- Prefer a task brief under roughly 4,000 tokens, excluding source files the agent must inspect. Give requirement IDs instead of repeating the specification.
- Ask the agent to inspect the smallest relevant set of files before reading additional code.
- Do not ask a child to “review the whole project”, “implement all remaining features”, or “fix everything”. Convert such requests into a checklist of bounded tasks.
- Do not use chain-of-thought requests. Ask for concise decisions, evidence, tests and limitations.
- Use deterministic scripts for file inventories, checksums, protocol code generation, formatting and repetitive validation instead of spending model turns on mechanical work.
- Keep commits small and avoid unrelated refactoring, renaming, or formatting.

### 16.8. Standard task packet template

Create one task packet per delegated task. Store an active packet under the owning repository's `docs/agent-tasks/` only when it is useful for restart/review; otherwise pass it directly through `delegate_task`. Do not maintain a third task-tracking repository.

```markdown
# RW-H-012 — Example task title

## Outcome
Implement one testable behavior in one sentence.

## Ownership
- Repository: remote-windows-host
- Role: H-WINDOWS
- Write paths: windows/, tests/windows/
- Read-only paths: protocol/<pinned-version>/, docs/ARCHITECTURE.md
- Branch/worktree: the exact branch/path returned by Hermes

## Requirements and dependencies
- Requirement IDs: FR-03, FR-04, FR-05
- Protocol contract: <version/tag and checksum>
- Prerequisites merged: <commit hashes>

## Acceptance criteria
- <observable condition>
- <specific test case>
- <failure behavior>

## Commands
- <exact build/test command>

## Out of scope
- <files, features, protocol edits, or platform behaviors not assigned>

## Required result
Return: status; changed paths; commit hash; exact commands run; pass/fail/not-run per command; evidence; known limitations; contract-change request if needed. Do not claim unrun tests passed.
```

For `delegate_task`, put the outcome in `goal` and the task packet in `context`. The child starts with a fresh conversation; do not use vague references such as “fix the bug above”. Hermes documents that only the child summary returns to the parent context, so the summary must contain the evidence required for integration.

### 16.9. Single-writer Git workflow with Hermes worktrees

For each task:

1. Confirm the repository is clean and record the base commit.
2. Delegate exactly one write task for that repository. Do not start a second writer before the first has returned and its worktree has been inspected.
3. Confirm the child actually used the expected Git worktree and branch. Inspect its reported path, branch, commit(s), dirty state and diff. If the result is missing or isolation did not activate, stop and inspect manually.
4. Review the diff for scope, correctness, secrets, generated-file changes and unrelated edits.
5. Verify the child's exact build/test commands and outputs. Re-run the relevant tests after the change is integrated into the primary checkout.
6. Merge or cherry-pick the reviewed commit through the orchestrator; never let a child merge its own work to the release branch.
7. Update the owning repository's `docs/STATUS.md` with verified facts only.
8. Only then assign the next write task in that repository.

A review subagent created from the base branch will not automatically see another child's unmerged worktree changes. Give a reviewer the exact commit/diff to inspect or merge the change into an integration branch first. A reviewer must not guess which branch contains the candidate patch.

### 16.10. Required child output schema

Every coding/review child must return a compact, structured report using these headings:

```text
STATUS: complete | partial | blocked | failed
TASK_ID: ...
REPOSITORY: remote-windows-host | remote-windows-client
BASE_COMMIT: ...
RESULT_COMMIT: ... | none
CHANGED_FILES:
- path — reason
COMMANDS_RUN:
- command — exit code — concise observed result
TESTS_NOT_RUN:
- test/command — reason
ACCEPTANCE_EVIDENCE:
- requirement — evidence
RISKS_OR_LIMITATIONS:
- ...
CONTRACT_CHANGE_REQUEST: none | concise proposal
NEXT_DEPENDENCY: none | task ID / owner
```

The orchestrator must reject reports that only say “done”, omit test status, cite no changed files, or claim unsupported results. A code agent's own statement that a test passed is not sufficient if the actual command output or test artifact is unavailable.

### 16.11. Build and tool limitations across platforms

The mandatory development topology has two machines: the Mac mini running Hermes/local LLM and serving as the primary macOS Client development/build/test/packaging machine, and the Windows 11 Pro PC serving as the Windows Host target and native Host build/test runner. The macOS Client must be designed and packaged to run on supported macOS hardware, including the Mac mini and MacBook. The MacBook is an additional compatibility target, not a mandatory separate development machine. Perform final MVP build and primary acceptance on the Mac mini; run a MacBook compatibility smoke test before claiming verified MacBook support when that device is available.

- Do not treat a macOS/Clang syntax check as a Windows Host build.
- A successful build on the Mac mini is the primary MVP build evidence. Do not claim verified MacBook compatibility solely from a Mac mini build; perform a MacBook smoke test before making that specific compatibility claim.
- Use the Windows PC on the LAN as the native Windows SDK/MSVC build/test runner. First configure and verify the approved remote execution channel described in Section 18; do not assume remote access exists merely because the machines share a LAN.
- Keep the Hermes terminal backend set to `local` for normal coding, Git operations, Mac builds, and worktree isolation. Do not globally switch the session to the SSH backend for mixed-platform work: that can unintentionally send Client commands to Windows. Use an explicit, narrowly scoped Windows-runner wrapper for remote builds/tests.
- The Windows runner must validate a specific Git commit and report that commit SHA alongside command output and test results. Do not build an uncommitted or ambiguous working tree remotely.
- `delegation.worktree_isolation` is documented for the local terminal backend. If a task uses SSH, Docker, or another backend, verify its isolation separately; do not assume Hermes created a separate Git worktree.
- Record unavailable checks as `NOT RUN`, not `PASS`. Keep cross-platform tasks small enough that code review can proceed when a platform runner is temporarily unavailable.
- Do not run actual Sleep or Shut Down integration tests on the only Windows runner without explicit user approval and a recovery plan; those actions can disconnect the runner and interrupt Hermes-driven work. Prefer mocks/unit tests until a manually approved dedicated test window is available.

### 16.12. Suggested execution cadence

Use a strict loop:

1. **Plan:** Orchestrator selects one task whose prerequisites are met.
2. **Assign:** Give one specialist a narrow task packet and exact write scope.
3. **Implement:** The specialist changes only assigned files and adds focused tests.
4. **Report:** Specialist returns the required output schema and commit/worktree details.
5. **Review:** Orchestrator checks scope, diff, build/test evidence and protocol compatibility.
6. **Integrate:** Merge the reviewed commit, run tests again, update `docs/STATUS.md`.
7. **Persist:** Update a project-local skill only when a stable reusable procedure has been learned; record architecture decisions in ADRs.
8. **Continue:** Assign the next task only after dependencies are verified.

Do not allow autonomous long chains that implement multiple major stages without an acceptance checkpoint. For long-running builds, use a tracked terminal process owned by the top-level session or wait for the child to finish and report its exit status. Delegated child execution is not durable across all session/process restart scenarios; Git commits and `docs/STATUS.md` are the durable project record.

### 16.13. Runtime configuration and setup checklist

- [ ] `hermes --version` recorded and compared with the official release page.
- [ ] Local model provider/model ID and endpoint type recorded without credentials; verify no hosted-model fallback is configured.
- [ ] Confirmed Hermes and local inference run on the Mac mini. The macOS Client may run independently on the same Mac mini or on a MacBook; it is not part of the Hermes process.
- [ ] Tool-use preflight passed, or a sequential no-delegation mode was selected.
- [ ] Delegation concurrency is set to `1` initially.
- [ ] Nested delegation is disabled; only the top-level session coordinates work.
- [ ] Worktree isolation is enabled and was tested inside each of the two Git repositories using the local terminal backend.
- [ ] Both product repositories are checked out in the Mac mini workspace; no third product repository exists.
- [ ] Root `AGENTS.md` exists in each repository and contains concise repository-local rules.
- [ ] Project-local skill directories exist and have been trusted in Hermes.
- [ ] The full plan is not copied wholesale into every child task.
- [ ] The Host-owned protocol bundle and Client pinned snapshot are versioned and checksum-verified.
- [ ] Windows Host build/test runner is reachable over the approved LAN remote-execution channel, or the limitation is recorded and Host integration tasks remain blocked.
- [ ] A smoke test proves the Windows runner builds a specific Host commit and returns the same SHA plus actual test outcomes.
- [ ] Windows Host build/test commands and macOS Client build/test/package commands on the Mac mini are recorded. If a MacBook is used for compatibility testing, its test commands/results are recorded separately.
- [ ] The first tasks are small, testable, and have explicit acceptance criteria.

### 16.14. Hermes documentation references

- [Hermes Agent releases](https://github.com/NousResearch/hermes-agent/releases)
- [Local Models](https://hermes-agent.nousresearch.com/docs/user-guide/local-models)
- [Subagent Delegation](https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/delegation.md)
- [Context Files](https://hermes-agent.nousresearch.com/docs/user-guide/features/context-files)
- [Skills System](https://hermes-agent.nousresearch.com/docs/user-guide/features/skills)

The official docs are mutable. Record the access date and the installed Hermes version when an important configuration behavior is confirmed.

---

## 17. Definition of Done for the MVP

The MVP is accepted only when each item below has been verified by testing:

- [ ] Hermes Agent and the configured local LLM run on the Mac mini; no cloud-model fallback is used unless explicitly approved.
- [ ] The Mac mini is used for development orchestration and is also a valid macOS Client runtime target. Runtime video and input go directly between the Windows Host and the selected Client process, never through Hermes as a relay.
- [ ] Windows Host builds/tests are verified on the actual Windows 11 Pro PC; the macOS Client builds, runs, and passes primary acceptance on the Mac mini. MacBook compatibility is verified separately when the device is available.
- [ ] The macOS Client has no Mac mini-specific hardware/path assumptions and is configured for the supported macOS/CPU targets; the same product must run on a supported MacBook. Record a physical MacBook smoke test when that device is available.
- [ ] Remote build/test execution from the Mac mini is authenticated, restricted to the approved runner account, and bound to an exact Host commit SHA.

- [ ] Windows Host runs in the interactive Windows 11 Pro user session.
- [ ] macOS Client runs as a native application.
- [ ] Pairing works on the LAN without Internet access.
- [ ] No mandatory external STUN/TURN/SFU or cloud services are used.
- [ ] The Client catalog shows discovered applications, including File Explorer, Access, and SEE Electrical when valid launch entries exist.
- [ ] Target applications can be launched from the Client without manually switching to the Windows monitor.
- [ ] Each supported main application window appears in its own `NSWindow`.
- [ ] Ordinary auxiliary top-level windows created by a shared app—including Open/Save dialogs, folder pickers, overwrite confirmations, and app-specific modal dialogs—appear automatically on the Client without a separate selection/share action.
- [ ] Auxiliary windows are associated with their parent/application session even when the dialog is hosted in a different process, using tested relationship/event/accessibility evidence rather than PID alone.
- [ ] Modal dialogs appear above their parent, receive input, block parent interaction while modal, and restore the parent when dismissed.
- [ ] Dismissing a Client-side auxiliary window requests Host-side closure and does not leave an invisible modal dialog open on Windows.
- [ ] Mouse and keyboard input reach the selected app or active dialog, with documented limitations.
- [ ] Built-in shortcut mappings are individually toggleable and the master adaptation switch works.
- [ ] Custom shortcut mappings can be created, edited, enabled, disabled, deleted, persisted, validated, and prioritized above built-in rules.
- [ ] Ordinary text entry remains functional when no shortcut rule matches.
- [ ] Local scaling, synchronized resize, and full-screen mode work.
- [ ] Active windows use the display target FPS; inactive windows use the configured FPS; minimized windows follow the configured policy.
- [ ] FPS gating occurs before encoding and load metrics are available.
- [ ] The Client provides separate protected Sleep and Shut Down commands.
- [ ] Shutdown does not forcibly close applications by default.
- [ ] Privilege errors and system-power failures are handled accurately.
- [ ] All MVP UI is English, with centralized resources ready for later languages.
- [ ] An unpaired Client cannot launch apps, inject input, or control power.
- [ ] Closing windows, disconnecting, and reconnecting do not leave unexplained resource leaks or stuck input state.
- [ ] Build instructions, test results, and known limitations are documented without unsupported claims.

**Priority when requirements conflict:** security → correct window and input behavior → offline LAN operation → low latency → bandwidth efficiency → UI convenience. Optimize from measurements, not assumptions.

---

## 18. Development and Runtime Topology: Hermes + macOS Client on Mac mini, Windows Host on LAN, MacBook Optional

This section is mandatory. There is one Windows Host target. The macOS Client is portable across supported macOS hardware and may run on the Mac mini or MacBook. The Mac mini is both the Hermes development controller and the primary macOS Client development/build/test/packaging target; these are separate processes with separate responsibilities. Running them on the same machine must not introduce an IPC dependency, shared runtime state, or a media relay requirement.

### 18.1. Machine roles

| Machine | Role | Responsibilities | Must not do |
|---|---|---|---|
| **Mac mini** | Hermes controller, primary macOS development/build/test/packaging machine, and supported Client target | Runs Hermes Agent and the local LLM; contains the two working Git clones; hosts orchestration, subagent worktrees, task packets, dependency cache, Client builds, primary Client runtime tests, and integration reports | Hermes must not become a media relay or be coupled to Client runtime logic. The Client must run as an independent process and remain usable if Hermes is stopped. Do not silently use a cloud model |
| **Windows 11 Pro PC** | The single Windows Host target and native Windows build/test runner | Provides MSVC/Windows SDK build and test execution; runs the Windows Host; participates directly in product sessions with whichever macOS Client is selected | Must not be treated as a safe target for automated shutdown tests without explicit approval; must not run arbitrary commands received from the product Client |
| **MacBook (optional secondary target)** | Additional supported macOS Client runtime/compatibility target | Can run the same macOS Client build and be used for compatibility checks, external display/focus tests, and user-workflow validation when available | It is not required for daily development, and it is not the only Client acceptance machine |

The required development environment consists of the Mac mini and the Windows PC. A MacBook may also be connected to the LAN for secondary validation. At runtime, the product data path is **Windows Host PC ↔ the Mac that is running the macOS Client**. If the Client runs on the Mac mini, the Host communicates directly with the Client process on that machine; Hermes does not proxy, transcode, inspect, or relay the media. If the Client runs on a MacBook, the Host communicates directly with that MacBook.

### 18.2. Repository placement and ownership

Maintain exactly two logical product Git repositories:

- `remote-windows-host`
- `remote-windows-client`

The standard Hermes workspace is on the Mac mini, with one local clone of each repository, for example:

```text
~/work/remote-windows-host/
~/work/remote-windows-client/
```

The paths are examples; record the actual paths in the environment manifest. Do not create a third monorepo, shared-code repository, protocol repository, or task-tracking repository. The protocol schema remains owned by the Host repository and is exported as a versioned/checksummed snapshot to the Client repository.

Hermes subagents should edit code in local Git worktrees under the relevant Mac mini checkout. This is the expected mode for `delegation.worktree_isolation`. A Windows runner checkout/build directory is a test environment, not a third product repository. If a local LAN mirror or Git transport endpoint is required, it must mirror one of the two existing repositories and must not become an independent source of truth.

### 18.3. Hermes execution model on the Mac mini

- Keep Hermes' normal terminal backend set to `local` for repository inspection, source editing, Git operations, script execution, macOS Client builds/tests/package creation, and local worktree isolation.
- Do not globally set the Hermes terminal backend to `ssh` for this mixed-platform project. A global remote backend can accidentally move macOS build commands onto Windows.
- Use an explicit wrapper, such as `tools/windows-runner.sh`, for remote Windows build/test commands. The wrapper calls approved PowerShell scripts on the Windows PC over SSH and passes only a small set of validated task parameters, such as a commit SHA, build configuration, and test-suite name.
- The wrapper must not pass untrusted prompt text as shell syntax, PowerShell code, an executable path, or arbitrary command arguments. Validate inputs locally and again in the Windows runner script.
- If Hermes SSH terminal backend is deliberately tested, use a separate profile/session and verify its working directory, shell, environment, and isolation. Do not assume local worktree isolation applies to SSH; official Hermes documentation scopes worktree isolation to the local terminal backend.
- Record Hermes version, local provider/model ID, endpoint type, model context setting, tool-use preflight results, and delegation limits in a local environment manifest. Do not store credentials, tokens, private prompts, or secrets in Git.
- Before starting a task, check the target model's ability to use tools, obey exact file scope, interpret compiler diagnostics, and report tests honestly. If reliability is insufficient, disable delegation and use one small verified task at a time.

### 18.4. Windows build/test runner access over the LAN

The fact that the Mac mini and Windows PC share a LAN does not automatically grant remote execution. Configure and verify an explicit mechanism before relying on it. The preferred first option is OpenSSH Server on Windows with key-based authentication and a dedicated, least-privileged development account; another authenticated mechanism is acceptable if it provides equivalent auditability and reproducibility.

Setup requirements:

1. Record the Windows PC's LAN IP/hostname and the chosen runner account in a local, untracked configuration file on the Mac mini. Never put passwords or private keys in either repository.
2. Use key-based authentication; do not use password prompts in unattended build scripts. Restrict inbound access to the LAN and the Mac mini where the Windows firewall supports such a rule.
3. Use a dedicated account that can build/test the Host and read the staged source. It must not be a local administrator solely for convenience and must not have system power rights unless a specific approved power integration test requires them.
4. Store the runner checkout/build output in a known directory outside the interactive user's documents. Keep the source commit, toolchain versions, build logs and test artifacts together.
5. Provide a fixed runner script with an allowlist of actions such as `status`, `prepare-source`, `build-debug`, `build-release`, `unit-tests`, and `integration-tests`. Reject unknown actions. Do not expose arbitrary shell execution as the runner interface.
6. For each run, pin the exact Host Git commit SHA. If the Windows runner fetches from a configured LAN-reachable Git remote, verify the checked-out SHA before building. If using an exported source bundle, record the source commit SHA and verify the bundle checksum before extraction.
7. Return a machine-readable report with the checked-out SHA, tool versions, commands executed, exit codes, test results, and artifact paths. The Hermes orchestrator must not infer `PASS` from a missing or truncated report.
8. Verify the remote path using a harmless status command before allowing build commands. Confirm that a failed or disconnected SSH session does not leave ambiguous background builds.

Remote execution is a development control path only. It is separate from the product's LAN signaling/media/control protocol.

### 18.5. Build and test placement

| Activity | Where it runs | Evidence required |
|---|---|---|
| Hermes inference and agent orchestration | Mac mini | Recorded local provider/model and successful tool-use preflight |
| Host source editing and local Git worktrees | Mac mini | Branch/worktree path, clean diff, commit SHA |
| Windows Host native build | Windows PC | MSVC/Windows SDK versions, exact source SHA, command output and exit code |
| Windows Host unit tests | Windows PC | Test report tied to the same source SHA |
| macOS Client build, test, and release packaging | Mac mini | Xcode/SDK version, exact commit, build/test logs, exit codes, and packaged artifact hash |
| Primary end-to-end application/window/input tests | Windows PC ↔ macOS Client process on Mac mini | Host/Client revisions, LAN topology, test observations and latency metrics |
| Secondary MacBook compatibility tests (when the device is available) | Windows PC ↔ macOS Client process on the MacBook | MacBook/macOS version, Client revision, smoke-test report and any device-specific differences |
| Product runtime media path | Windows PC ↔ whichever Mac runs the Client | Evidence that selected WebRTC candidates are LAN-local and media does not route through Hermes as a relay |
| Cross-repository contract sync | Mac mini orchestration session | Host schema version, exported bundle checksum, Client snapshot version/checksum |

A successful Windows build on the remote runner is not a substitute for integration testing against the real Client process. The Mac mini is the primary Client build and acceptance machine. A MacBook smoke test is required before claiming that compatibility has been verified on that specific device, but its absence must not block day-to-day development or primary packaging on the Mac mini.

### 18.6. Network paths and port separation

Document three separate path types:

1. **Development control path:** Hermes/Mac mini → authenticated SSH or approved remote runner channel → Windows build/test runner.
2. **Development source path:** Git commit or source bundle moves between the Mac mini workspace and the Windows runner over the configured LAN mechanism.
3. **Product runtime path:** Windows Host ↔ whichever macOS Client process the user starts (on the Mac mini or MacBook) for pairing/signaling, control messages, and direct media.

Do not route product video through Hermes or through a separate relay process. When the Client is running on the MacBook, stopping Hermes and powering off the Mac mini must not affect that Host-to-Client session. When the Client itself is running on the Mac mini, powering off the Mac mini naturally stops that Client process; this must not affect the Windows Host's independence or any separate Client session on another Mac.

Write the listening addresses and ports for all three paths in `docs/SECURITY.md` and `docs/TEST_PLAN.md`. Keep the Windows runner's SSH/administration port separate from the product Host's runtime ports. No port should be exposed beyond the required LAN scope.

### 18.7. Safe integration testing on the Windows PC

The Windows PC is both the product Host target and the remote build/test runner. This creates an operational risk: Sleep/Shutdown tests, forced process termination, driver changes, firewall edits, or a crash can disconnect the coding agent from its test machine.

- Unit-test power control behind an injectable interface or mock by default.
- Do not invoke actual `SLEEP` or `SHUTDOWN` from Hermes task automation unless the user explicitly authorizes that test in the current session and a recovery procedure is documented.
- Never run a power test while an uncommitted Host build/test task depends on that PC.
- Prefer a second test PC/VM for destructive system-operation tests. If only the target PC is available, schedule a manual test with local physical recovery available.
- Do not grant the remote runner account local-administrator or shutdown privilege for routine compile/test steps. Test privilege-denial paths automatically.
- Network, firewall, account, and SSH configuration changes must be proposed and checked before execution; do not silently weaken the Windows PC's security.

### 18.8. LAN topology preflight

Before implementation starts, create `docs/agent-runtime/TOPOLOGY_PREFLIGHT.md` in the Host repository with a copy in the Client repository containing only Client-relevant results. Record no credentials. Check:

- [ ] Hermes version and local model/provider confirmed on Mac mini; cloud fallback is disabled or explicitly accounted for.
- [ ] Windows PC and Mac mini IP addresses, operating systems and roles recorded; if a MacBook is available for secondary testing, record its details separately.
- [ ] Mac mini can access the two working Git repositories locally.
- [ ] Mac mini can authenticate to the Windows build/test runner over the selected LAN channel.
- [ ] Windows runner can build a harmless test project and reports its exact tool versions.
- [ ] A specific Host commit can be made available to the runner, and the runner verifies the exact SHA/checksum before building.
- [ ] Mac mini successfully builds, launches, tests, and packages the macOS Client. An alternative Mac build path does not replace this primary workflow.
- [ ] The macOS Client can be built, launched, and tested on the Mac mini.
- [ ] If a MacBook is available, the same Client build or release candidate can be launched there for compatibility testing.
- [ ] Product runtime traffic flows directly between the Windows PC and the currently selected macOS Client device.
- [ ] Product pairing, launch, window streaming, input, dialogs and resize work with Internet disconnected.
- [ ] Stopping Hermes never stops a running Client session. If the Client is running on the MacBook, powering off or disconnecting the Mac mini must not interrupt that Host-to-Client session. If the Client is running on the Mac mini, powering off the Mac mini ends that Client session because it is the endpoint, not because Hermes acts as a relay.
- [ ] Firewall rules expose only the necessary development and product ports to the required LAN peers.
- [ ] Power actions are mock-tested and are not run against the only runner without explicit approval.

If any required check fails, mark dependent tasks `BLOCKED` and state the exact missing permission, software component, route, or test device. Do not work around the problem by silently moving the local model to a cloud endpoint or routing runtime media through an unplanned relay.

### 18.9. Orchestrator checklist for every cross-machine task

The top-level Hermes session must verify the following before accepting a remote build/test result:

1. The task ID and owning repository are explicit.
2. The candidate commit exists in the Mac mini's intended repository and has a clean reviewed diff.
3. The remote runner is pointed to that exact commit or a checksummed export of it.
4. The runner returns source SHA, tool versions, command exit codes and test artifacts.
5. The orchestrator distinguishes `PASS`, `FAIL`, `NOT RUN`, and `BLOCKED`; missing evidence is never `PASS`.
6. The test report is stored in the owning repository's documented artifact location or referenced from its `docs/STATUS.md`.
7. The agent does not assume the remote terminal shares the local worktree, filesystem, shell, environment variables or installed tools.
8. The next dependent task is not assigned until the required evidence is inspected.

### 18.10. Reference material

- [Hermes Agent Local Models](https://hermes-agent.nousresearch.com/docs/user-guide/local-models)
- [Hermes Agent terminal backend configuration](https://hermes-agent.nousresearch.com/docs/user-guide/configuration)
- [Hermes Agent subagent delegation and worktree isolation](https://hermes-agent.nousresearch.com/docs/user-guide/features/delegation)
- [Windows OpenSSH overview](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh-overview)

At project start, record the date these documents were checked and the installed Hermes version. Official online documentation may change.

