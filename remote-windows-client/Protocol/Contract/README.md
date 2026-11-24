# Protocol contract (canonical)

This directory is the **canonical** source of truth for the Remote Windows
control protocol. Only the CONTRACT role may modify files here (plan rule 21).

- `remote_windows.proto` — canonical schema (protoc syntax).
- `VERSION` — schema semver (`MAJOR.MINOR.PATCH`), one line.
- `test-vectors/` — golden frames; executable tests in both product trees
  load the same files (the Client keeps a synced copy under
  `remote-windows-client/Protocol/Contract/test-vectors/`).
- `../tools/export_protocol.sh` — builds a versioned, checksum-verified
  contract bundle (`SHA256SUMS` + copies).

## Rules

1. Every wire frame carries `schema_major`/`schema_minor` (envelope) and is
   rejected on major mismatch. Breaking changes MUST bump MAJOR and update
   `docs/PROTOCOL.md` with a compatibility note.
2. Field additions that existing peers can ignore (new optional fields, new
   kinds) bump MINOR.
3. Never rename or reuse field numbers or enum values.
4. The v0.x wire format is length-prefixed UTF-8 JSON encoding of
   `Envelope` (see ADR-0002). The .proto remains the canonical schema even
   though v0.x runtime codecs are JSON.
