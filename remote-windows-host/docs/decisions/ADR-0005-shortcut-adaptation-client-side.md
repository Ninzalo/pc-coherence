# ADR-0005: Keyboard shortcut adaptation lives in the macOS Client

Date: 2026-11-23 · Status: accepted

## Context

FR-08/§6: macOS modifier conventions differ from Windows; a dedicated
Client module must adapt shortcuts before events are sent, with built-in
toggles, custom mappings, validation, and persistence.

## Decision

- All adaptation logic is a Swift module in the Client
  (`RemoteWindowsKeyboard`): `ShortcutAdaptationService`,
  `BuiltInShortcutCatalog` (the 16 mappings of FR-08.2 with the exact
  default on/off states), `CustomShortcutStore` (versioned JSON
  persistence, Client-side only), `ShortcutRuleValidator`,
  `KeyboardEventTranslator` (baseline key translation table).
- Priority: enabled custom (scope-matching) → enabled built-in → baseline.
- Matching uses physical key codes + modifier sets, never characters.
- A matched rule emits an ordered Windows sequence (modifier-downs in
  documented order control→alt→shift→win, key-down, then the matching
  key/modifier-ups as they arrive) tracked in a pending-state machine;
  `reset()` clears all state on focus/connection loss so nothing stays
  logically pressed.
- macOS-reserved shortcuts (⌘Tab, ⌘Space, ⌘Esc, ⌘`) remain local; the
  UI shows the limitation notice.
- Rule IDs are stable and language-neutral; only labels are localized.

## Consequences

- The Host stays modifier-agnostic: it receives normalized Windows key
  events only.
- All of the above is unit-tested in the Client package (runs on macOS).
