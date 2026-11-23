# Windows Host — Test Plan

## Unit (portable core + network) — run on macOS and Windows

- `rwh_protocol_tests`: version parsing, JSON codec round-trips, validation
  of all 22 golden vectors (valid AND invalid), framing edge cases
  (max length, truncation, malformed JSON, bad UTF-8).
- `rwh_catalog_tests`: path normalization, stable app_id, dedupe, revision.
- `rwh_window_relationships_tests`: announce decisions incl. cross-process
  owned dialogs (FR-03/3A), modality, removal without parent teardown.
- `rwh_input_tests`: key mapping table completeness, KeyStateTracker reset.
- `rwh_fps_policy_tests`: gating at fixed rates, pause, no catch-up bursts.
- `rwh_power_tests`: state machine, privilege denial paths, no auto-retry.
- `rwh_pairing_tests`: code/nonce validation, lockout, key derivation.
- `rwh_authorization_tests`: pre/post-pairing kind gates.
- `rwh_network_tests`: loopback server/client: hello handshake, version
  mismatch close, oversized frame drop, malformed JSON drop, idle timeout,
  3 concurrent connections, sequence tracking.

Command (portable):
```
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

## Integration (Windows 11 Pro only) — NOT RUN until runner exists

- Start Menu scan finds File Explorer; launch by app_id; window detected.
- WGC capture of one window; frame timing metrics recorded.
- SendInput to a tracked window in a text field; no stuck keys after
  disconnect.
- Auxiliary dialog (File Explorer Open) announced automatically;
  cross-process picker associated via owner.
- Power: mock tests only by default (plan §18.7); real sleep/shutdown only
  with explicit owner approval and a recovery plan.

## Malformed-frame fuzz (portable)

`rwh_protocol_tests` includes a truncated/garbage suite; the Host must
never crash (exit code + frame counter assertions).
