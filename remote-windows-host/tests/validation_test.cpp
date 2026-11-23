
#include <gtest/gtest.h>
#include "core/protocol_validation.hpp"

using namespace remotewindows::protocol;

namespace {
Envelope env(const std::string& k, Payload p,
             std::optional<uint32_t> req = std::nullopt,
             std::optional<uint64_t> seq = std::nullopt) {
  Envelope e;
  e.kind = k;
  e.payload = std::move(p);
  e.request_id = req;
  e.sequence = seq;
  return e;
}
}  // namespace

TEST(Validation, FrameRateFpsOutOfBounds) {
  FrameRateRequest r;
  r.window_id = "11111111-2222-4333-8444-555555555555";
  r.target_fps = 241;
  r.reason = FrameRateReason::FOCUS_CHANGED;
  auto vr = validate(env("frame_rate_request", r, {}, {}));
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.error_code, "fps_out_of_range");
}

TEST(Validation, FrameRateZeroIsPaused) {
  FrameRateRequest r;
  r.window_id = "11111111-2222-4333-8444-555555555555";
  r.target_fps = 0;
  r.reason = FrameRateReason::MINIMIZED;
  EXPECT_TRUE(validate(env("frame_rate_request", r, {}, {})).ok);
}

TEST(Validation, WindowAnnounceZeroWidth) {
  WindowAnnounce w;
  w.window_id = "11111111-2222-4333-8444-555555555555";
  w.application_session_id = "s1";
  w.role = WindowRole::MAIN;
  w.state = WindowState::NORMAL;
  w.width_px = 0;
  w.height_px = 100;
  w.dpi_scale_percent = 100;
  auto vr = validate(env("window_announce", w));
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.error_code, "invalid_dimensions");
}

TEST(Validation, BadUuidRejected) {
  WindowRemove rm;
  rm.window_id = "not-a-uuid";
  rm.reason = WindowRemovalReason::CLOSED;
  auto vr = validate(env("window_remove", rm));
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.error_code, "invalid_uuid");
}

TEST(Validation, AppIdRules) {
  EXPECT_TRUE(isAppId("app-abcdef1234567890"));
  EXPECT_TRUE(isAppId("A-1_b"));
  EXPECT_FALSE(isAppId(""));
  EXPECT_FALSE(isAppId("-bad"));
  EXPECT_FALSE(isAppId("has space"));
  EXPECT_FALSE(isAppId(std::string(129, 'a')));
}

TEST(Validation, PairingCodeRules) {
  EXPECT_TRUE(isSixDigitCode("004321"));
  EXPECT_FALSE(isSixDigitCode("12345"));
  EXPECT_FALSE(isSixDigitCode("12345a"));
}

TEST(Validation, TitleBounds) {
  WindowAnnounce w;
  w.window_id = "11111111-2222-4333-8444-555555555555";
  w.application_session_id = "s1";
  w.role = WindowRole::MAIN;
  w.state = WindowState::NORMAL;
  w.width_px = 100;
  w.height_px = 100;
  w.dpi_scale_percent = 100;
  w.title = std::string(513, 'x');
  auto vr = validate(env("window_announce", w));
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.error_code, "title_too_long");
}

TEST(Validation, RequestIdRequired) {
  ResizeRequest r;
  r.window_id = "11111111-2222-4333-8444-555555555555";
  r.client_width_px = 100;
  r.client_height_px = 100;
  auto vr = validate(env("resize_request", r, {}, {}));  // no request_id
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.error_code, "missing_request_id");
}

TEST(Validation, SequenceRequiredForInput) {
  InputEvent in;
  in.window_id = "11111111-2222-4333-8444-555555555555";
  MouseInput m;
  m.kind = MouseKind::MOVE;
  in.input = m;
  auto vr = validate(env("input_event", in, {}, {}));
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.error_code, "missing_sequence");
}

TEST(Validation, InputVkRange) {
  InputEvent in;
  in.window_id = "11111111-2222-4333-8444-555555555555";
  KeyInput k;
  k.windows_vk = 0;
  in.input = k;
  auto vr = validate(env("input_event", in, {}, {1}));
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.error_code, "vk_out_of_range");
}
