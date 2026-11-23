# ADR-0008: Single repository containing both product trees

Date: 2026-11-23 · Status: accepted (explicit owner instruction)

## Context

The development plan mandates exactly two Git repositories
(`remote-windows-host`, `remote-windows-client`) and forbids a third
monorepo. The owner explicitly instructed: create **one** local Git
repository (this one), link it to `git@github.com:Ninzalo/pc-coherense.git`,
and push each commit there.

## Decision

The project is developed in a single Git repository containing both
product trees as independent subtrees:

- `remote-windows-host/` — Windows Host source + canonical protocol
- `remote-windows-client/` — macOS Client source + pinned contract snapshot
- top-level `docs/`, `README.md`, `AGENTS.md` — cross-cutting records

All other plan rules apply unchanged: single-writer ownership per subtree,
CONTRACT owns `remote-windows-host/protocol/`, the Client consumes a
checksum-verified snapshot, and no source lives in any third repository.

## Alternatives

1. Two GitHub repositories — rejected against the owner's explicit
   single-repo instruction (and the single remote URL provided).
2. One repo but split into two nested `.git` repos — rejected: nested
   repositories break tooling and violate "clone, build, test
   independently" in practice.

## Consequences

- Protocol snapshot sync uses an in-repo script
  (`remote-windows-client/tools/sync_protocol_from_host.sh`) with
  `HOST_TAG = single-repo:<commit>` provenance instead of a Host tag.
- If the project later splits into two repositories, each subtree is
  self-contained enough to be split cleanly.
