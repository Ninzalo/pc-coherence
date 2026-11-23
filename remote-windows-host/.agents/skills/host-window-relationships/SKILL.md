---
name: host-window-relationships
description: WinEvent tracking, owner correlation, brokered dialogs, modality rules.
---

# Host window relationships

- Hook `SetWinEventHook` for CREATE/SHOW/HIDE/DESTROY/NAMECHANGE/
  LOCATIONCHANGE/FOCUS/MINIMIZE + a reconciliation scan every 2 s.
- Correlation rules (ADR-0006): owner/root-owner of a tracked window
  (strong, cross-process) OR session process-set membership + visible
  (weak). Never PID-only, never title-only.
- Modality: owned-by-tracked ⇒ modal=true while open; parent input is
  blocked on the Client side via the relationship graph.
- `window_id` is a Host-assigned UUID; HWNDs stay internal.
- Required scenarios to keep passing (see TEST_PLAN): Open/Save/folder
  picker, overwrite confirmation, nested confirmation, brokered
  cross-process picker.
