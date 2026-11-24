# macOS Client — Security

## Threat model (summary)

The Client talks to exactly one Windows Host on the LAN. There is no cloud
component, no telemetry, and no Internet access at runtime (FR-10).

## Pairing (FR-13)

- Explicit pairing: the Host issues `pairing_challenge` (nonce + host name);
  the user types the 6-digit one-time code shown on the Host screen; the
  Client sends code + echoed nonce in `pairing_challenge`.
- Pairing key derivation (identical on both peers):
  `hex(sha256("rwpair1|" + code + "|" + nonce))`.
- The pairing key is stored in the **Keychain** on the Client (never
  UserDefaults, never a file in plaintext). The Host stores it in a
  protected local file.
- Attempt limit: 3 failures → 60 s lockout (Host-enforced).
- No power/launch/input capability before pairing succeeds (Host gate,
  PROTOCOL §6).

## Transport

- v0.1.0: direct TCP on the LAN, Host listening port **47987** (signaling
  + control only; no video in v0.1.0). Manual host address entry in the MVP.
- Video will travel on WebRTC DTLS-SRTP once integrated; signaling must
  never carry media.

## Message hygiene

- All control messages are validated on receipt (codec + semantic
  validation, PROTOCOL §5); malformed frames are dropped and counted,
  never crash the app.
- The protocol carries no filesystem paths, commands, PIDs, HWNDs, or
  shell strings; power actions are the typed `sleep`/`shutdown` enums only
  and are never auto-retried.

## Client-side rules

- Logs: structured, no secrets, no window contents, no typed text.
- No arbitrary remote execution surface exists in this codebase.
