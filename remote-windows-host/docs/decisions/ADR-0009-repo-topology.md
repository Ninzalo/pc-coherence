# ADR-0009: Repository topology — one git repository, two product trees (deviation from plan §4.3)

Date: 2026-11-24 · Status: accepted (recorded deviation)

## Context

Plan §4.3 mandates exactly two git repositories (`remote-windows-host`,
`remote-windows-client`). The actual topology of this project, in place since
the first commits, is a **single git repository** (`pc-coherence`, remote
`github.com:Ninzalo/pc-coherence.git`) whose working tree contains the two
product trees:

```
pc-coherence/
  remote-windows-host/      # product tree A (Windows Host, canonical protocol/)
  remote-windows-client/    # product tree B (macOS Client, pinned Protocol/Contract/ snapshot)
```

Verified 2026-11-24: `git worktree list` from inside either tree reports the
top level as `/Users/nnzlgg/pc-coherence`; all historical commits (pairing,
tray, signaling, client signaling client) live on one `main` branch of that
one repository. The Windows runner consumes the same repository
(`C:\rwh\pc-coherence`).

Plan rule 2 applies: the real environment differs from the document — follow
reality and record the difference.

## Decision

Keep the single repository for the MVP and enforce the two-repository
DISCIPLINE inside it:

- The root `AGENTS.md` ownership table (path → owner role) is the
  single-writer contract; no agent edits another owner's tree opportunistively.
- The canonical wire schema lives only in
  `remote-windows-host/protocol/` (owner: CONTRACT).
- The Client consumes the pinned, checksum-verified snapshot in
  `remote-windows-client/Protocol/Contract/`, updated only via
  `remote-windows-client/tools/sync_protocol_from_host.sh` (owner:
  C-CONTRACT). The snapshot's `VERSION` + `SHA256SUMS` make every Client
  state reproducible against an exact Host revision, which is the property
  the two-repository rule exists to guarantee.
- Task IDs remain tree-prefixed (`RW-H-*`, `RW-C-*`); task packets name the
  owning tree and exact write paths.
- No third repository, shared-code tree, or workspace repository is created.

## Alternatives

1. **Split into two repositories now** — rejected for the MVP: a mid-project
   restructuring of the working tree, the runner's source path, the protocol
   snapshot tooling and the GitHub remote for a topology that already
   satisfies the intent (independent trees, canonical schema + pinned
   snapshot). Risk of breaking the verified pairing/LAN state for no
   functional gain. A split, if ever wanted, is a release-checkpoint task.
2. **Two repositories per plan from scratch** — rejected: would orphan the
   existing history, runner setup, and the verified end-to-end pairing state.

## Consequences

- One `main` branch, one push target; integration is commit + push on `main`
  after orchestrator review (children commit on `agent/<ROLE>/<TASK>`
  branches, never on `main`).
- Independent versioning of the two products is NOT a feature of this
  topology (both trees share history). Protocol versioning (
  `protocol/VERSION`) and the Client snapshot pinning remain the version
  boundaries between the trees.
- A future split is mechanical: `git subtree split` per tree + two remotes;
  nothing in the code depends on the single-repo topology.
