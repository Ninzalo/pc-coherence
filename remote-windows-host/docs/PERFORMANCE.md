# Windows Host — Performance

Baseline plan (plan §10.2): 1920×1080 @ 60 FPS target on one active
window, wired Gigabit Ethernet, three concurrent windows. The p95
capture-to-present ≤ 25 ms figure is a **hypothesis to measure**, not a
promise.

## Metrics collected (when video is integrated)

capture-to-encode latency, encode time, network timing, decode time,
decode-to-present, action-to-visible latency (client side), per-stream
FPS/dropped frames/bitrate, CPU/GPU, memory, launch→process-start,
launch→first-frame.

## Status

NOT MEASURED — no video pipeline and no Windows runner yet. FrameRateGate
(pre-encode gate) is implemented and unit-tested in the portable core.
