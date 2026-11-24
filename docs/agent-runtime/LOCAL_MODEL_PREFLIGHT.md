# Local Model Preflight — Hermes Agent + Local LLM

Required by plan Section 16.4. All entries below were verified by direct
observation in this environment on 2026-11-24; no credentials are stored.

## 1. Hermes installation (Section 16.1)

- Installed: **Hermes Agent v0.23.3 (2026.9.24) · stable**, git install at
  `/Users/nnzlgg/.hermes/hermes-agent`.
- The plan's dated baseline was v0.22.5 (2026-10-29). The installed build is
  newer; where behavior differs from the plan's playbook text, the installed
  behavior wins and the difference is recorded here.
- Model: **Qwen3.8-27B-GGUF:UD-Q4_K_XL**, provider `custom` (OpenAI-compatible
  local llama.cpp endpoint on the Mac mini via the user's llm-router; loopback
  only). Session-start model was `...:UD-Q4_K_M`; switched to `...:UD-Q4_K_XL`
  mid-session by the owner. No hosted/cloud fallback is configured.
- Terminal backend: `local`. Working directory: `/Users/nnzlgg/pc-coherence`.
- Delegation: enabled with `max_concurrent_children: 1`.

## 2. Tool-use preflight results (Section 16.4)

| # | Check | Result | Evidence |
|---|---|---|---|
| 1 | Identifies local provider/model, normal response | PASS | Live session on the local endpoint above; no hosted fallback |
| 2 | Read/search tool on a named file, specific fact reported | PASS | e.g. `remote-windows-host/docs/STATUS.md` read; reported 83/83 test result and next-task list verbatim |
| 3 | Harmless terminal command, exit code + real output | PASS | `git status`, `ssh ... "hostname & ver"` (DESKTOP-PC, Windows 10.0.26300.10075), `fsutil volume diskfree C:` (1.7 TB free) — outputs quoted as returned |
| 4 | Minimal reversible edit + diff inspection | PASS | ADR/README patches applied via `patch`; unified diffs inspected before commit `6f7c303` |
| 5 | Run a test/build; distinguish pass/fail/not-run | PASS | Host core suite green on 2026-11-23 (83/83, recorded in host STATUS.md); Windows-side builds honestly recorded as NOT RUN (no MSVC on the PC) |
| 6 | Tiny delegated task in isolated worktree | NOT RUN | Not exercised this session. A 27B local GGUF model as a child coding agent is unproven; per plan 16.4 fallback, the owner roles (H-FOUND, C-FOUND, CONTRACT, …) are executed **sequentially by the top-level session as fresh narrowly scoped tasks** — same ownership boundaries, same task-packet discipline, no spawned children until a preflight child run passes |
| 7 | No invented paths/hashes/results when tools error | PASS | Failed tool calls this session (schtasks syntax, PS quoting) were reported as failures and retried with a different approach; nothing was claimed without output |
| 8 | Works with external network disabled (except identified tools) | PASS (design) | Product runtime needs no Internet. Network use this session: GitHub push (owner-requested repo move) and MS Build Tools download on the Windows PC (build preparation, allowed by plan §11 "Internet-connected build machine") — both explicitly intended, none at product runtime |

## 3. Consequence

Item 6 is NOT RUN, so delegated code editing is **off** for this project by
default: work proceeds as sequential single-writer tasks in the top-level
session, one role at a time, with the plan's task-packet and evidence rules.
Re-enable delegation only after a measured passing child task in a throwaway
worktree.

Date of check: 2026-11-24. Hermes docs access date for configuration
behavior: 2026-11-24 (delegation/local-model sections consulted via the
installed build's behavior, per 16.14).
