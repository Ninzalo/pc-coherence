# ADR-0001: Transport — WebRTC media + direct LAN TCP signaling/control

Date: 2026-11-23 · Status: accepted

## Context

MVP must work fully offline on a LAN with low latency, no STUN/TURN, no
SFU, no cloud (plan FR-10). Control messages need a small reliable
channel; video needs peer-to-peer media with encryption.

## Decision

- **Media (video):** WebRTC Native API, one PeerConnection per device
  pair, multiple video tracks (one per window) + DataChannel for control
  (to be re-validated when integrated; see plan §12 item 1 for the
  one-vs-many comparison).
- **Signaling + control in v0.1.0:** a small direct TCP channel on the
  Host (LAN-bound, TCP 47987) carrying length-prefixed JSON envelopes
  (PROTOCOL §1). This is explicitly allowed by the plan ("A small direct
  TCP/TLS signaling channel may exchange SDP/ICE and pairing information;
  video must not pass through signaling"). Until WebRTC is integrated the
  same channel also carries all control messages — documented in
  STATUS.md as the v0.1.0 scope, with the upgrade path to DataChannel.
- No external ICE servers; host candidates restricted to LAN-local
  addresses when media is integrated.

## Alternatives

- Raw UDP RTP/VP8 without WebRTC — rejected: no standardized
  DTLS-SRTP/codec negotiation, more code, no hardware codec integration
  story on macOS.
- mDNS discovery — deferred (plan FR-10 allows manual IP entry in MVP).

## Consequences

- TCP/TLS upgrade for the v0.x control channel is a later security
  hardening item (TLS for the LAN channel is planned post-MVP or at MVP
  security review — recorded as a known limitation until then).
- WebRTC integration requires a pinned build (DEPENDENCIES.md).
