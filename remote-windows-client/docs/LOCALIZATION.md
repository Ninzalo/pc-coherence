# macOS Client — Localization

- MVP ships **English only** (FR-14).
- All user-facing strings go through the `L10n` helper with stable keys;
  the resource file is `Sources/RemoteWindowsApp/Resources/Localizable.en.json`.
- Rule: no literal user-facing English in business logic. Protocol IDs,
  enums, log event names, `app_id`s, and shortcut rule IDs are
  language-neutral and never translated.
- Adding a locale later: add `Localizable.<locale>.json` with the same
  keys and select the locale at runtime — no core-logic or protocol
  changes (plan FR-14).
- Audit: `L10nTests.testEnglishResourcesLoad` guards the resource load;
  the Stage 8 UI audit (plan §9) checks for hard-coded strings.
