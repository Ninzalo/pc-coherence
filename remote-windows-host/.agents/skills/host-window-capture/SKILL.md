---
name: host-window-capture
description: WGC capture lifecycle, frame scheduling and encoder ownership rules.
---

# Host window capture

- One `Windows.Graphics.Capture` session per independently rendered
  source window (main or auxiliary), created on `WINDOW_ANNOUNCE` and
  released on `WINDOW_REMOVE` — releasing a dialog's session must not
  touch the parent's.
- Capture callbacks arrive on a D3D11 shared-surface; the
  `FrameRateGate` (core/fps_policy) decides BEFORE any encode work.
- Never read full GPU surfaces to CPU for pixel comparison (plan FR-09).
- Handle `CloseCapture` events (source destroyed) by routing them to the
  window registry, which decides announce/remove messaging.
- Encoder: `VideoEncoderFactory` (ADR-0007); never encode a frame twice.
- Diagnostics: capture-to-encode ms per frame into PerformanceStats.
