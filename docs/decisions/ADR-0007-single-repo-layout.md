# ADR-0007: Single Git repository containing both product trees

- Status: Accepted (2026-11-23)
- Deciders: project owner (explicit instruction), Hermes orchestrator

## Context

The development plan (Section 4.3, rule 19) mandates exactly two Git
repositories: `remote-windows-host` and `remote-windows-client`, with no third
shared repository.

On 2026-11-23 the project owner explicitly instructed that development start
in one local repository at `/Users/nnzlgg/pc-coherence`, linked to
`git@github.com:Ninzalo/pc-coherense.git`, with every commit pushed there.

These two requirements conflict. The owner's direct instruction takes
precedence.

## Decision

Use one Git repository (`pc-coherence`) containing two self-contained product
subtrees:

```text
pc-coherence/
  remote-windows-host/      # product tree A (canonical protocol owner)
  remote-windows-client/    # product tree B (pinned contract snapshot)
  docs/agent-runtime/       # environment/preflight reports
  docs/decisions/           # cross-cutting ADRs
```

Each tree keeps the plan's per-repo layout (README, AGENTS.md, docs/,
tests) so the trees can later be split into two GitHub repositories by
moving the directories, without content changes.

## Consequences

- Single-writer ownership is enforced by directory scope instead of
  repository boundaries (see root `AGENTS.md`).
- The protocol sync tool operates on sibling directories inside this
  repository instead of a Host tag on a LAN remote.
- `git@github.com:Ninzalo/pc-coherense.git` is the single remote. The exact
  URL was provided by the owner; if the repository name is a typo it must be
  corrected by the owner (renaming the repo), not silently by the agent.
