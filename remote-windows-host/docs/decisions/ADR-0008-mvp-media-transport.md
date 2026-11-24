# ADR-0008: MVP media transport — dedicated TCP media channel, H.264, AES-256-GCM (WebRTC deferred)

Date: 2026-11-24 · Status: accepted

Supersedes the media-transport choice in ADR-0001 for the MVP.
ADR-0007 (H.264 codec, hardware acceleration verified) remains binding for
codec selection; its "carried on WebRTC video tracks" transport path is
replaced by this ADR for the MVP.

## Context

- ADR-0001 chose the WebRTC Native API for media. That requires a pinned,
  reproducible WebRTC build on BOTH platforms (plan §3.1;
  `docs/DEPENDENCIES.md`: "NOT YET PINNED").
- Toolchain reality, measured 2026-11-24:
  - Mac mini (primary Client build machine): Swift Command Line Tools only,
    **no full Xcode**. The WebRTC GN build on macOS assumes the Xcode
    toolchain; CLT-only support is unproven (high risk).
  - Windows runner (DESKTOP-PC): VS 2026 BuildTools (MSVC 14.51.36231) +
    Windows SDK 10.0.26100. Upstream WebRTC GN builds pin earlier VS
    toolsets; MSVC 14.51 compatibility is unproven (high risk).
  - `gclient sync` is a multi-GB fetch plus 30–60+ minute builds per
    platform; every failure iteration costs a full pinned-SHA runner
    round-trip.
- MVP hard requirements (plan FR-10, §12): media direct over the LAN, no
  STUN/TURN/SFU/cloud, protected, low latency. Measured LAN: 10 Gbps wired
  (Ethernet 3, Intel 82599) plus a 100 Gbps adapter on the PC. GPU:
  NVIDIA RTX 5080 (hardware H.264 via Media Foundation / NVENC). Mac:
  VideoToolbox hardware H.264 decode.
- The plan's priority order is security → correct window/input behavior →
  offline LAN operation → low latency. The §3 stack table is "recommended";
  the binding requirements are the functional FR items. Plan rule 2: where
  reality differs from the document, follow reality and record the
  difference — this ADR is that record.

## Decision (MVP)

- **Video:** H.264 (Main profile, low-latency GOP) carried on a **dedicated
  TCP media channel**:
  - Host listens on **TCP 47988** (signaling/control stays on 47987).
  - The media connection is opened **after pairing succeeds**; unpaired
    peers are refused.
  - **Handshake:** Client connects to 47988 and proves knowledge of the
    session key with `HMAC-SHA256(session_key, "rwh-media-v1|<host
    nonce>")`, where `session_key` is derived per
    `core::PairingManager::deriveSessionKey` (both peers derive it from the
    one-time code + host name at pairing time). Host replies with an
    accept/reject.
  - **Per-window stream key:**
    `media_key(window_id) = hex(sha256("rwh-media-v1|" + session_key_hex + "|" + window_id))`
    (32 bytes, AES-256).
  - **Packet framing (normative spec: `docs/PROTOCOL.md` §9, added by
    CONTRACT task RW-H-006):** little-endian header — magic `0x52` ('R'),
    format version `u8 = 1`, stream id `u16` (first 16 bits of
    `sha256(window_id)`), flags `u8` (bit0 keyframe), frame sequence `u32`,
    payload length `u32` — followed by the **AES-256-GCM ciphertext** of the
    H.264 byte payload + 16-byte GCM tag. Nonce: 12 bytes = 8-byte random
    per-stream prefix + 4-byte little-endian packet counter.
  - **Reliability/ordering:** TCP (reliable, in-order). LAN loss is
    negligible at the measured link rates; head-of-line blocking is
    acceptable for MVP latency; no custom transport reimplementation.
  - **FPS gate before encoding** (FR-09); keyframe request on resume or
    rate increase.
- **Windows encode:** Media Foundation H.264 via a `VideoEncoderFactory`
  (ADR-0007): prefer the hardware encoder (NVENC), fall back to software
  with a visible diagnostic. No double encoding.
- **macOS decode:** VideoToolbox (hardware H.264) → `CVPixelBuffer` →
  Metal/CALayer in a per-window `NSWindow`.
- **Crypto backends:** Windows CNG (`BCrypt*`, AES-256-GCM); macOS
  CryptoKit (`AES.GCM`). Both verified against **committed golden vectors**
  (synthetic keys/plaintexts, no real secrets) so parity is testable on
  both sides without a shared library. No new external dependency.
- **Input and control:** unchanged — stay on the paired TCP 47987 channel
  (v0.1.0 envelopes). The media channel carries no control semantics; video
  never passes through signaling (plan ADR-0001 constraint, kept).

## Alternatives

1. **WebRTC from source on both platforms** — rejected for the MVP:
   CLT-only macOS (no Xcode), unproven MSVC 14.51 / VS 2026 support,
   multi-GB fetches + long per-platform builds, DTLS/ICE/candidate
   integration cost. Revisit per the upgrade path below.
2. **Custom UDP with RTP-style fragmentation and retransmission** —
   rejected: reimplements a transport (fragmentation, reordering,
   recovery) for a gain (NAT traversal) the MVP does not need.
3. **Uncompressed or JPEG frames** — rejected: 1080p60 raw is ~480 MB/s;
   per-frame JPEG loses inter-frame compression; H.264 hardware
   encode/decode is available on both ends and wins on bandwidth, latency
   and quality.
4. **Video on the existing 47987 signaling channel** — rejected: violates
   the plan ("video must not pass through signaling") and couples control
   latency to media.

## Consequences

- **FR-13 deviation (recorded, honest):** media is protected with
  per-stream AES-256-GCM derived from the pairing session key, not
  DTLS-SRTP. Effective properties: no media without a completed pairing;
  per-stream key separation; authenticated encryption (confidentiality +
  integrity); no media on unpaired connections. The deviation from the
  FR-13 wording is recorded in `docs/STATUS.md` (Known limitations) until
  the upgrade path is taken.
- **No NAT traversal** — direct TCP on the same LAN. A future deployment
  crossing NAT is the primary reason to move to WebRTC.
- **Upgrade path:** when a pinned WebRTC build is achievable (full Xcode
  on the Client build machine or a verified toolset on the runner), a
  follow-up ADR moves media to WebRTC tracks; the v0.x control protocol is
  unchanged by that move.
- **Ports:** 47987 (signaling/control), 47988 (media) — recorded in
  `docs/SECURITY.md`; the dev SSH port (22) stays separate (plan §18.6).
- **Protocol schema:** unchanged at v0.1.0 — the media channel adds no
  envelope kinds; `docs/PROTOCOL.md` gains a normative §9 (CONTRACT,
  RW-H-006) with golden crypto vectors.
