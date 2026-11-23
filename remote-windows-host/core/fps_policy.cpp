
#include "core/fps_policy.hpp"

#include <algorithm>

namespace remotewindows::core {

uint64_t FpsGate::frame_interval_us() const {
  if (target_fps == 0) return 0;
  return 1000000ull / target_fps;
}

bool FpsGate::mayCapture(uint64_t now_us) {
  if (target_fps == 0) return false;
  uint64_t interval = frame_interval_us();
  uint64_t due = last_capture_us_ + interval;
  if (last_capture_us_ == 0 || now_us >= due) {
    last_capture_us_ = now_us;
    return true;
  }
  return false;
}

void FpsGate::reset(uint64_t now_us) {
  (void)now_us;
  // Documented contract (FR-09): a resume or target change delivers a fresh
  // frame, so force the next frame through by clearing the last-capture stamp.
  last_capture_us_ = 0;
}

uint32_t clampInactiveFps(uint32_t requested) {
  if (requested < 1) return 1;
  if (requested > 60) return 60;
  return requested;
}

uint32_t computeTargetFps(const FpsPolicyInput& in) {
  uint32_t base;
  if (in.minimized && in.minimized_pause) {
    return 0;  // paused
  } else if (in.minimized) {
    base = in.minimized_low_fps;
  } else if (in.active) {
    base = std::min(in.screen_max_fps, in.encoder_limit_fps);
    if (in.user_active_cap > 0) base = std::min(base, in.user_active_cap);
  } else {
    base = clampInactiveFps(in.inactive_fps);
  }
  if (base > 240) base = 240;
  return base;
}

}  // namespace remotewindows::core
