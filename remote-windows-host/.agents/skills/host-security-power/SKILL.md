---
name: host-security-power
description: Pairing, typed power commands, privilege validation, safe failure.
---

# Host security & power

- Pairing: 6-digit one-time code (shown on Host UI) + 32-hex nonce;
  3 failures ⇒ 60 s lockout; key = hex(sha256("rwpair1|"+code+"|nonce));
  stored per-user, never logged, never committed.
- Pre-pairing gates (PROTOCOL §6) are enforced in
  `core/authorization`; no feature code bypasses them.
- Power: typed SLEEP/SHUTDOWN only; SE_SHUTDOWN_NAME checked explicitly;
  `InitiateSystemShutdownExW` with force=FALSE; never auto-retry;
  `accepted` ≠ "system is off".
- Never log: typed text, window contents, pairing codes/keys.
- Real sleep/shutdown tests only with explicit owner approval + recovery
  plan; default is the mock interface in unit tests.
