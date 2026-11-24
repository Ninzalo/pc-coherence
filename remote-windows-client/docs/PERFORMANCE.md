# macOS Client — Performance

## FPS policy (FR-09)

```
if window_is_minimized and pause_minimized (default):
    target_fps = 0
else if window_is_active:
    target_fps = min(NSScreen.maximumFramesPerSecond, encoder_limit,
                     user_active_cap_if_configured)
else:
    target_fps = inactive_fps            // setting, default 10, range 1..60
```

- `inactive_fps` is a Client-side setting (Preferences).
- Active target is a ceiling, not a promise of unique frames.
- Display change (window moves screens) recomputes the target.
- The frame-rate gate applies **before encoding on the Host**; the Client
  sends `frame_rate_request` with a `FrameRateReason`.

## Metrics (plan §10.2)

Per-stream: FPS, bitrate, dropped frames, capture-to-encode and encode
time (Host `PERFORMANCE_STATS`). Client-side: decode time and
decode-to-present time once the video path exists. Baseline experiment:
1920×1080 @ 60 FPS target, one active window, wired Ethernet, three
concurrent windows; latency objective to be set from measurement
(p95 capture-to-present ≤ 25 ms is a hypothesis, not a promise).

## Current state

No video path exists yet (WebRTC pinned-build task pending), so no
performance numbers are recorded. This file states that honestly rather
than inventing figures.
