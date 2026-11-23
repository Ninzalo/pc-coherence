
#ifndef RWH_CORE_FPS_POLICY_HPP
#define RWH_CORE_FPS_POLICY_HPP

#include <cstdint>

namespace remotewindows::core {

// FR-09 / Stage 6: the frame-rate gate is applied BEFORE encoding.
// The policy is Host-neutral: the Client computes the target and sends
// FRAME_RATE_REQUEST; this gate enforces it on the capture loop and is
// unit-testable without a display.
struct FpsGate {
  // target_fps: 0 = stream paused (minimized with pause policy);
  // 1..240 = frames per second.
  uint32_t target_fps = 30;

  uint64_t frame_interval_us() const;
  // now_us: monotonic microseconds. Returns true when a frame may be
  // captured now. The first call (or after reset) always passes.
  bool mayCapture(uint64_t now_us);
  // Call on resume or target change; forces the next frame through.
  void reset(uint64_t now_us);

 private:
  uint64_t last_capture_us_ = 0;
};

// FR-09: compute the target FPS the Client will request for a stream.
struct FpsPolicyInput {
  bool minimized = false;
  bool minimized_pause = true;   // setting: pause vs low-fps
  uint32_t minimized_low_fps = 5;
  bool active = false;
  uint32_t screen_max_fps = 60;
  uint32_t encoder_limit_fps = 60;
  uint32_t user_active_cap = 0;  // 0 = no cap
  uint32_t inactive_fps = 10;    // Client setting, default 10
};

// Allowed Client range 1..60 for inactive_fps (FR-09 / §8.1).
uint32_t clampInactiveFps(uint32_t requested);
uint32_t computeTargetFps(const FpsPolicyInput& in);

}  // namespace remotewindows::core

#endif
