# ADR-0006: Auxiliary-window correlation without PID-only evidence

Date: 2026-11-23 · Status: accepted

## Context

FR-03/3A: dialogs/pickers must be mirrored automatically, including when
brokered into a different process. PID alone must not be used to include
or exclude.

## Decision

The Host maintains an application-session graph. A new top-level window
is announced as auxiliary when **either**:
1. its owner/root-owner (`GetWindow(GW_OWNER)`,
   `GetAncestor(GA_ROOTOWNER)`) resolves to a tracked window of a shared
   session (strong evidence — works across processes), or
2. it belongs to a process registered in the session's process set (e.g.
   helper process) AND is visible (weaker evidence, role=`unknown`
   unless a dialog class heuristic applies).

Otherwise it is not associated (reason `no_relationship_evidence`).
Role heuristics: class `#32770` + title match (Open/Save/Save As/Select
Folder/Print) → `file_picker`, else `dialog`. WinEvents
(`EVENT_OBJECT_CREATE/SHOW/DESTROY/NAMECHANGE/LOCATIONCHANGE`, focus,
minimize) drive updates; a periodic reconciliation scan covers missed
events. A different PID is never by itself an exclusion reason.

Client side: the relationship graph preserves parent/owner, modality and
z-order; while a modal child is open the parent is blocked for input;
dialog dismissal sends `WINDOW_ACTION REQUEST_CLOSE` and waits for
`WINDOW_REMOVE` (no optimistic hiding).

## Consequences

- Brokered dialogs are covered by rule 1; same-process dialogs by both.
- UAC secure desktop / protected surfaces remain out of scope (documented).
