# macOS Client — Protocol Integration

## Snapshot rules

- Canonical schema: `remote-windows-host/protocol/` (CONTRACT role, Host tree).
- This tree stores a pinned snapshot in `Protocol/Contract/`:
  - `remote_windows.proto`, `VERSION`, `README.md`
  - `test-vectors/*.json` (the canonical golden set)
  - `SHA256SUMS` (checksums over the snapshot files)
  - `PROVENANCE` (source commit; single-repo mode: `single-repo:<sha>`)
- Snapshot updates happen ONLY via `tools/sync_protocol_from_host.sh`
  (run from this directory). The tool copies the Host `protocol/`
  directory, regenerates `SHA256SUMS`, and records `PROVENANCE` from the
  current repository commit.
- Verify any snapshot with:
  `cd Protocol/Contract && sha256sum -c SHA256SUMS`
  (or `shasum -a 256 -c SHA256SUMS` on macOS).
- Hand-editing any file under `Protocol/Contract/` is forbidden; if the
  wire must change, file a Protocol Change Request against the Host tree
  (workflow in the plan §4.3).

## Client-side implementation

- `Sources/RemoteWindowsApp/Network/ProtocolTypes.swift` — enums + payload
  structs + `Envelope` (field names identical to the proto).
- `Network/ProtocolCodec.swift` — JSON encode/decode; absent fields
  omitted, unknown fields ignored; enum values are lower_snake_case
  strings; decode-time enforcement of UUID format (`bad_uuid`) and title
  length (`title_too_long`), mirroring the Host codec.
- `Network/ProtocolValidation.swift` — semantic validation, a 1:1 port of
  Host `core/protocol_validation.cpp` (same error codes, same bounds).
- `Network/Framing.swift` — uint32 big-endian length prefix + UTF-8 JSON,
  frame length ≤ 8 MiB (`malformed_frame` otherwise).

Normative protocol description: `remote-windows-host/docs/PROTOCOL.md`.

## Compatibility tests

`ProtocolVectorTests` loads every file in `Protocol/Contract/test-vectors/`
and asserts: `expected: valid` → decodes AND validates clean;
`expected: invalid` → rejected with the exact `violation` code. This makes
the 22 vectors the shared cross-language golden set with the Host.
