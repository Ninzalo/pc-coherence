# Local Model Preflight — 2026-11-24

Per plan §16.4. No credentials or private prompts in this file.

## Environment

| Item | Value |
|---|---|
| Hermes | v0.23.3 (2026.9.24) · stable · local `caafe5389ae5` · git install at `/Users/nnzlgg/.hermes/hermes-agent` · Python 3.14.9 |
| Model | `Qwen3.8-27B-GGUF:UD-Q4_K_M` (local GGUF, custom provider/endpoint on the Mac mini) |
| Terminal backend | local (Hermes normal backend; Windows work goes through the explicit `tools/windows-runner.sh` wrapper) |
| Delegation config | `max_concurrent_children: 1`, `max_iterations: 250`, flat tree (no nested spawn from children) |
| Host | Mac mini, macOS 27.2.1, Swift Command Line Tools only (no full Xcode) |

## Preflight run (2026-11-24, 17:23–17:25 UTC+3, throwaway repo in scratch dir)

| Check (plan §16.4) | Result | Evidence |
|---|---|---|
| 1. Model completes a normal response | PASS | child returned full structured report |
| 2. Read/search tool on a named file, specific fact | PASS | reported 2nd line `THE-FACT-42` exactly |
| 3. Harmless terminal command, exit code + output | PASS | `pf-42`, exit 0, reported verbatim |
| 4. Minimal reversible edit + diff inspection | PASS (with note) | child appended a line; `git diff` was empty because the file was untracked — the child explained the actual reason instead of faking a diff (honest behavior verified) |
| 5. Distinguish pass / fail / not-run | PASS | `test -f` → pass; `nonexistent-cmd-xyz-12345` → fail (exit 127), correctly NOT classified as pass |
| 6. Small delegated task completes, parent identifies work | PASS | `deleg_bdd01ab0` / `sa-0-44eb73a5`; transcript at `~/.hermes/cache/delegation/live/deleg_bdd01ab0/task-0.log`; cleanup verified (`GONE`, exit 0) |
| 7. No invented results when tools return errors | PASS | no tool errors occurred; the one benign shell notice (getcwd after deleting cwd) was reported as such, not as a tool error |
| 8. Works without external network for product work | N/A (design) | product is runtime-offline by construction (FR-10); dev-time internet on both machines is recorded and used only for dependency fetching, per plan §11 |

Notes:
- 10 API calls, 103 s wall time for the whole preflight.
- No hosted-model fallback in use; the child ran on the same local model.
- Worktree note: Hermes worktree isolation operates on the ROOT repository
  (`/Users/nnzlgg/pc-coherence`) because the project uses ONE git repository
  containing both product trees — see `docs/decisions/ADR-0009-repo-topology.md`.
  Single-writer discipline is enforced by task-scope instructions per the
  root `AGENTS.md` ownership table; children must not commit to `main`.

## Verdict

Tool-use preflight PASSED for `Qwen3.8-27B-GGUF:UD-Q4_K_M` under Hermes
v0.23.3 with `max_concurrent_children: 1`. Delegation is enabled with
narrow, evidence-based task packets (plan §16.7/§16.8). Build/test evidence
rules unchanged: Win32 code is verified only by the Windows runner
(`RWH ...` reports); unrun tests are recorded NOT RUN.
