# macOS Client — Test Plan

## Unit tests (run with `swift test` on the Mac mini)

| Suite | Covers |
|---|---|
| `L10nTests` | resource loading, known-key resolution, fallback |
| `PreferencesTests` | defaults (inactive_fps 10, port 47987), range clamping, persistence, schema version |
| `ProtocolCodecTests` | JSON round-trips per payload; omitted-absent/unknown-ignored; decode-time `bad_uuid` / `title_too_long` |
| `ProtocolVectorTests` | all 22 canonical vectors from `Protocol/Contract/test-vectors/` (valid → decode+validate; invalid → exact violation code) |
| `FramingTests` | length-prefix encode/decode, round-trip, >8 MiB rejection, partial-read reassembly |

## Keyboard (FR-08 / plan §6.4) — added with the C-INPUT task

Built-in mappings on/off, master toggle, custom-rule persistence,
custom-over-built-in precedence, duplicate/conflict validation, key
down/up ordering, modifier reset on focus/connection loss, baseline
typing, layout-sensitive keys, reserved macOS shortcuts.

## Integration tests (require Windows Host build — NOT RUN yet)

- LAN-only session with Internet disconnected (pairing, catalog, launch).
- Auxiliary/dialog mirroring scenarios (plan §9 Stage 3A).
- Power: mock-based only by default; real SLEEP/SHUTDOWN only on an
  explicitly approved test window (plan §18.7).

## Environment honesty

Results are recorded as PASS / FAIL / NOT RUN with the exact command in
`docs/STATUS.md`. A test is never reported as passed without command
output.
