# ADR-0007: Video codec — H.264 via WebRTC, hardware acceleration verified

Date: 2026-11-23 · Status: proposed (video pipeline not yet integrated)

## Context

Stage 1 requires H.264 over WebRTC with low latency; hardware encoding/
decoding must be *measured*, not assumed (plan §12 item 2).

## Decision (pending integration)

- H.264 (Main/High profile, low-latency GOP) carried on WebRTC video
  tracks; encoder chosen via a `VideoEncoderFactory` that prefers the
  platform hardware encoder (Media Foundation/DXVA on Windows,
  VideoToolbox on macOS) and falls back to the WebRTC software encoder
  with a visible diagnostic when the hardware path is unavailable.
- No double encoding: if a hardware encoder adapter is used, WebRTC's
  built-in encoder is bypassed for that track.
- Acceptance: hardware acceleration proven with profiler evidence on the
  target GPU; otherwise diagnostics are surfaced in UI/logs.

## Consequences

- Nothing in v0.1.0 depends on this ADR; it becomes binding when the
  WebRTC integration task starts.
