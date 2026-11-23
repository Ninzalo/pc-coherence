
#include <gtest/gtest.h>
#include "core/fps_policy.hpp"

using namespace remotewindows::core;

TEST(FpsGate, FirstFrameAlwaysPasses) {
  FpsGate g;
  g.target_fps = 30;
  EXPECT_TRUE(g.mayCapture(1000));
}

TEST(FpsGate, BlocksWithinInterval) {
  FpsGate g;
  g.target_fps = 30;  // ~33333 us
  g.reset(0);
  EXPECT_TRUE(g.mayCapture(33333));
  EXPECT_FALSE(g.mayCapture(40000));   // still inside the interval
  EXPECT_TRUE(g.mayCapture(66666));
}

TEST(FpsGate, PausedTargetBlocksEverything) {
  FpsGate g;
  g.target_fps = 0;
  EXPECT_FALSE(g.mayCapture(0));
  EXPECT_FALSE(g.mayCapture(1000000));
}

TEST(FpsGate, ResetForcesNextFrame) {
  FpsGate g;
  g.target_fps = 60;  // 16666us interval
  g.reset(100);
  EXPECT_TRUE(g.mayCapture(110));   // reset forces the next frame
  EXPECT_FALSE(g.mayCapture(120));  // immediately after a capture: gated
  g.reset(200);
  EXPECT_TRUE(g.mayCapture(210));   // a second reset forces again
}

TEST(FpsPolicy, MinimizedPause) {
  FpsPolicyInput in;
  in.minimized = true;
  in.minimized_pause = true;
  EXPECT_EQ(computeTargetFps(in), 0u);
}

TEST(FpsPolicy, MinimizedLowFps) {
  FpsPolicyInput in;
  in.minimized = true;
  in.minimized_pause = false;
  in.minimized_low_fps = 5;
  EXPECT_EQ(computeTargetFps(in), 5u);
}

TEST(FpsPolicy, ActiveUsesDisplayCeiling) {
  FpsPolicyInput in;
  in.active = true;
  in.screen_max_fps = 120;
  in.encoder_limit_fps = 60;
  EXPECT_EQ(computeTargetFps(in), 60u);  // encoder is the limit
}

TEST(FpsPolicy, UserCapApplies) {
  FpsPolicyInput in;
  in.active = true;
  in.screen_max_fps = 120;
  in.encoder_limit_fps = 120;
  in.user_active_cap = 45;
  EXPECT_EQ(computeTargetFps(in), 45u);
}

TEST(FpsPolicy, InactiveUsesSetting) {
  FpsPolicyInput in;
  EXPECT_EQ(computeTargetFps(in), 10u);  // default inactive_fps
  in.inactive_fps = 2;
  EXPECT_EQ(computeTargetFps(in), 2u);
}

TEST(FpsPolicy, InactiveClamped1to60) {
  EXPECT_EQ(clampInactiveFps(0), 1u);
  EXPECT_EQ(clampInactiveFps(1), 1u);
  EXPECT_EQ(clampInactiveFps(999), 60u);
  EXPECT_EQ(clampInactiveFps(30), 30u);
}
