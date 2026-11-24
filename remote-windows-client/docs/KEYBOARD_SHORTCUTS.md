# macOS Client — Keyboard Shortcuts (FR-08)

Summary of the requirements this tree must implement (full text: plan
§FR-08, §6). Implementation lands in `Sources/RemoteWindowsApp/Keyboard/`
(C-INPUT task).

## Behavior

- Adaptation applies only while a remote window is the active input
  target; rules are matched **before** emitting normalized Windows key
  events; unmatched input falls through to baseline key/text translation.
- Matching uses physical key codes + modifier flags (not localized
  characters). A matched rule emits ordered Windows key-down/key-up
  events with correct modifier ordering.
- Priority: enabled custom rule (scope match) > enabled built-in rule >
  baseline translation. Custom-over-built-in is mandatory.
- Key-up events, repeat behavior, and modifier state are preserved; all
  outstanding key state resets on focus loss and connection loss.
- No clipboard injection or synthetic text replacement.
- macOS-reserved global shortcuts (e.g. Command-Tab) remain local; the UI
  says so instead of promising interception.

## Settings UI (Settings → Keyboard Shortcuts)

- Master toggle `Adapt macOS shortcuts for Windows` (disables all rules
  without deleting them).
- Built-in Mappings: one toggle per rule, visible `⌘X → Ctrl+X` display.
- Custom Mappings: create/edit/enable/reorder/delete; source + target
  capture; optional per-application scope (via Host `app_id`); user label.
- Validation: duplicate source combinations and conflicts are reported,
  never silently resolved; empty/unsupported combinations rejected.

## Persistence

Custom mappings persist on the Client with a versioned settings model
(`CustomShortcutStore`); built-in defaults are separately identifiable so
app updates never overwrite user rules.
