# ADR-0004: Typed, privileged, non-retriable power control

Date: 2026-11-23 · Status: accepted

## Context

FR-11/FR-12 + plan §7: sleep/shutdown from the Client, safe by design.

## Decision

- Protocol carries only typed `SYSTEM_POWER_REQUEST { SLEEP | SHUTDOWN }`
  — no commands, no force flag (force-close stays FALSE by default; a
  future explicit field would require a protocol change).
- Host flow: verify paired → verify power authorization → validate enum
  → check/enable `SE_SHUTDOWN_NAME` (OpenProcessToken /
  LookupPrivilegeValue / AdjustTokenPrivileges, inspect
  `GetLastError()==ERROR_NOT_ALL_ASSIGNED`) →
  sleep: `SetSuspendState(FALSE, FALSE, FALSE)`; shutdown:
  `InitiateSystemShutdownExW` (no force, no reboot, planned reason).
- Response is `accepted` when the call was accepted — never a claim that
  the system is off/asleep.
- No auto-retry after timeout: result becomes `unknown` (client shows
  "result unknown"). No service install, no auto-elevation, no UAC bypass.
- Privilege-denied paths are unit-tested against the mock interface.

## Consequences

- Real sleep/shutdown tests are destructive to the only Windows runner;
  they run only with explicit owner approval + recovery plan (plan §18.7).
