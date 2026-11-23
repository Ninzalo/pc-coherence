# Windows Host — Security

## Threat model (MVP)

LAN-only. Threats: unpaired or malicious client on the LAN; sniffing on a
shared segment; malformed/hostile protocol frames.

## Controls

1. **Pairing.** One-time 6-digit code displayed on the Host screen +
   nonce; 3 failed attempts → 60 s lockout; derived key
   `sha256("rwpair1|"+code+"|"+nonce)` stored on the Host in
   `%LOCALAPPDATA%\RemoteWindows\pairing.json` with 0600-equivalent ACLs
   (current user only). No plaintext secrets in logs.
2. **Authorization gates.** Pre-pairing only hello/pairing/catalog/
   heartbeat are accepted (PROTOCOL §6). Power requires pairing +
   explicit power authorization.
3. **Input validation.** Every frame validated per PROTOCOL §5 before
   dispatch; malformed frames dropped and counted; max frame 8 MiB; no
   frame can carry paths/commands/PIDs (schema-level guarantee).
4. **Input injection limits.** SendInput only to allow-listed tracked
   windows; UIPI limits documented (no UAC bypass, no secure desktop).
5. **Network exposure.** Host binds the selected LAN interface, TCP 47987
   only. Windows Firewall: allow inbound TCP 47987 from the LAN subnet for
   the Host process only; everything else default-deny. Documented
   command:
   ```
   netsh advfirewall firewall add rule name="RemoteWindows Host" dir=in action=allow protocol=TCP localport=47987 remoteip=LOCALSUBNET program="<host exe path>"
   ```
6. **Power.** Typed SLEEP/SHUTDOWN only; `SE_SHUTDOWN_NAME` checked
   explicitly; no service install, no auto-elevation; no force-close by
   default; no auto-retry.
7. **Secrets.** Nothing secret is committed; pairing files are in
   `.gitignore`; tests use synthetic codes.

## Port map (all LAN-scoped)

| Port | Path | Direction |
|---|---|---|
| 47987 TCP | product runtime: signaling + control (v0.x) | Client → Host |
| (future) WebRTC media ports | product runtime video | direct Host ↔ Client |
| (dev) SSH 22 | development control path to the build runner only — separate from product ports | Mac mini → Windows |
