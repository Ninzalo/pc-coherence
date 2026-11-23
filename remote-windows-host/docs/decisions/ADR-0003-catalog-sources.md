# ADR-0003: Application catalog sources

Date: 2026-11-23 · Status: accepted

## Context

FR-01 requires a catalog for the current Windows user without claiming to
enumerate every executable on every disk.

## Decision

Sources (in priority order):
1. Per-user Start Menu shortcuts (`%APPDATA%\Microsoft\Windows\Start Menu\Programs`) parsed with `IShellLinkW`.
2. Common Start Menu shortcuts (`%ProgramData%\Microsoft\Windows\Start Menu\Programs`).
3. Packaged app entries via `AppListEntry`/Start Apps where the API allows
   (Windows 10+).
4. User-added entries (stored locally on the Host, JSON, current user).

Deduplication: normalize target path (casefold, `/` separators, collapse
repeats) + arguments; stable `app_id = "a" + sha256(source|normalized_target|args)[0:16]`.
The catalog is incremental (per-source revision) and pushed via
`APP_CATALOG_CHANGED` — opening the view never forces a rescan.

## Consequences

- The catalog is a launch allow-list: the Client only ever sends
  `app_id`; the Host resolves locally. No path/command injection surface.
- Icons are referenced by host-local `icon_ref` (a catalog icon cache
  path), never transmitted as executable paths.
