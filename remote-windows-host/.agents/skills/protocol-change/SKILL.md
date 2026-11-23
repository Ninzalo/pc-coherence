---
name: protocol-change
description: Change the canonical protocol schema with versioning and vectors.
---

# Protocol change workflow (CONTRACT role only)

1. Write a short change note: reason, fields, compatibility impact,
   affected requirement IDs.
2. Edit `protocol/remote_windows.proto` (never in the Client tree).
3. Bump `protocol/VERSION` (MINOR for additive/ignorable, MAJOR for
   breaking + compatibility note in `docs/PROTOCOL.md`).
4. Add/adjust `protocol/test-vectors/*.json` (id, description, envelope,
   expected valid|invalid, violation).
5. Run `tools/export_protocol.sh` (verify SHA256SUMS regenerates).
6. Run Host core tests (vectors execute) AND Client `swift test`
   (after `remote-windows-client/tools/sync_protocol_from_host.sh`).
7. Report: version before→after, vector list, test evidence.
