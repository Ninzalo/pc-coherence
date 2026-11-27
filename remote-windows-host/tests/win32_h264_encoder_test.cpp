// RW-H-012 (H-MEDIA): hermetic tests for the Media Foundation H.264
// encoder backend (win32/video/h264_encoder_mf).
//
// SESSION CONTEXT: the in-box H.264 encoder MFTs are session-context
// dependent (measured on the dev PC): an interactive session has a
// working hardware encoder (NVIDIA H.264 Encoder MFT); a headless
// session (session 0, sshd) has none that produces output. The tests
// therefore follow the established consent-gated pattern:
//   * the availability test ALWAYS runs and reports the measured
//     candidate list (activation state per candidate) in test output;
//   * the encode tests GTEST_SKIP with a clear reason when start()
//     reports no working encoder in this session context, and run the
//     full byte-level assertions otherwise.
// The full encode path is verified interactively via schtasks (the
// RW-H-011b/012 live-verification pattern). All waits are bounded; the
// test can never hang.
//
// WINDOWS ONLY: Win32/Media Foundation — must NOT compile on macOS
// (AGENTS.md: Win32 targets are NOT RUN there by design).

#include "win32/video/h264_encoder_mf.hpp"

#include <gtest/gtest.h>

#include <core/capture_encoder.hpp>

#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

using namespace remotewindows;

namespace {

// Recording sink: thread-safe (the seam contract allows sink calls on
// encoder/worker threads), counts and stores structured NAL facts only
// — never pixel/byte contents (structured-logs-only rule).
class RecordingSink final : public core::IEncodedFrameConsumer {
 public:
  struct FrameInfo {
    uint64_t source_seq = 0;
    uint32_t frame_seq = 0;
    bool keyframe = false;
    size_t bytes = 0;
    bool annex_b_valid = false;  // >=1 NAL with a known type
    bool has_sps = false, has_pps = false, has_idr = false;
  };

  void onEncodedFrame(const core::EncodedFrame& frame) override {
    FrameInfo fi;
    fi.source_seq = frame.source_seq;
    fi.frame_seq = frame.frame_seq;
    fi.keyframe = frame.keyframe;
    fi.bytes = frame.payload.size();
    const auto& p = frame.payload;
    const uint8_t* d = p.data();
    const size_t n = p.size();
    fi.annex_b_valid = false;
    // Annex-B scan: 3- and 4-byte start codes (the client accepts both;
    // VideoPipeline.swift:336-378). NAL types: 1=non-IDR, 5=IDR,
    // 6=SEI, 7=SPS, 8=PPS, 9=SEI (legacy).
    for (size_t i = 0; i + 3 < n; i++) {
      size_t sc = 0;
      if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) sc = 3;
      if (i + 4 < n && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 0 &&
          d[i + 3] == 1) sc = 4;
      if (sc == 0) continue;
      if (i + sc < n) {
        const uint32_t t = d[i + sc] & 0x1F;
        if (t == 1 || t == 5 || t == 6 || t == 7 || t == 8 || t == 9) {
          fi.annex_b_valid = true;
          if (t == 7) fi.has_sps = true;
          if (t == 8) fi.has_pps = true;
          if (t == 5) fi.has_idr = true;
        }
      }
      i += sc;
    }
    std::lock_guard<std::mutex> lk(mu_);
    frames_.push_back(fi);
  }
  void onEncoderError(const std::string&, const std::string& reason) override {
    std::lock_guard<std::mutex> lk(mu_);
    ++errors_;
    if (first_error_.empty()) first_error_ = reason;
  }
  size_t count() {
    std::lock_guard<std::mutex> lk(mu_);
    return frames_.size();
  }
  size_t errorCount() {
    std::lock_guard<std::mutex> lk(mu_);
    return errors_;
  }
  std::string firstError() {
    std::lock_guard<std::mutex> lk(mu_);
    return first_error_;
  }
  FrameInfo at(size_t i) {
    std::lock_guard<std::mutex> lk(mu_);
    return frames_[i];
  }

 private:
  mutable std::mutex mu_;
  std::vector<FrameInfo> frames_;
  size_t errors_ = 0;
  std::string first_error_;
};

bool waitFor(const std::function<bool()>& pred, uint64_t timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(static_cast<int64_t>(timeout_ms));
  for (;;) {
    if (pred()) return true;
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// Deterministic moving-pattern BGRA frame (pixel content never logged).
core::CapturedFrame makeFrame(uint32_t w, uint32_t h, uint64_t seq) {
  core::CapturedFrame f;
  f.window_id = "rwh-h264-test";
  f.seq = seq;
  f.timestamp_us = 33333 * seq;
  f.width = w;
  f.height = h;
  f.format = core::CaptureFrameFormat::Bgra8;
  f.pixels.resize(static_cast<size_t>(w) * h * 4);
  uint8_t* p = f.pixels.data();
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      p[0] = static_cast<uint8_t>((x + static_cast<uint32_t>(seq) * 7) & 0xFF);  // B
      p[1] = static_cast<uint8_t>((y + static_cast<uint32_t>(seq) * 3) & 0xFF);  // G
      p[2] = static_cast<uint8_t>((x + y + static_cast<uint32_t>(seq)) & 0xFF);  // R
      p[3] = 255;  // A
      p += 4;
    }
  }
  return f;
}

// Shared preamble for encode tests: survey + start; SKIPs with a clear
// reason when no encoder produces output in this session context.
bool tryStart(std::unique_ptr<win32::MfH264Encoder>& enc,
              RecordingSink& sink, uint32_t w, uint32_t h, uint32_t fps,
              std::string& detail) {
  detail.clear();
  enc = std::make_unique<win32::MfH264Encoder>("rwh-h264-test", sink);
  if (!enc->start(w, h, fps)) {
    const auto cands = win32::MfH264Encoder::surveyCandidates();
    for (const auto& c : cands) {
      std::fprintf(stderr,
                   "[h264-test] candidate=%s activatable=%d detail=%s\n",
                   c.friendly_name.c_str(), c.activatable ? 1 : 0,
                   c.detail.c_str());
    }
    return false;
  }
  detail = enc->selectedEncoder();
  return true;
}

}  // namespace

// (1) MFT availability/creation — always runs, reports the MEASURED
// candidate list in test output (structured values only).
TEST(H264EncoderMf, CandidateSurveyReportsMeasurement) {
  const auto cands = win32::MfH264Encoder::surveyCandidates();
  int activatable = 0;
  for (const auto& c : cands) {
    std::fprintf(stderr,
                 "[h264-test] candidate=%s activatable=%d detail=%s\n",
                 c.friendly_name.c_str(), c.activatable ? 1 : 0,
                 c.detail.c_str());
    if (c.activatable) ++activatable;
  }
  // The API must work in any session context; the NUMBER of working
  // encoders is environment-dependent (measured: 0 in a headless
  // session 0 on the dev PC). Never assert availability here.
  SUCCEED();
}

// (2) >=30 synthetic frames -> byte-level NAL assertions (pinned
// layout: annex-B, SPS+PPS in-band in the first keyframe, 1:1 frames).
TEST(H264EncoderMf, EncodeSyntheticThirtyFrames) {
  RecordingSink sink;
  std::unique_ptr<win32::MfH264Encoder> enc;
  std::string enc_name;
  if (!tryStart(enc, sink, 320, 200, 30, enc_name)) {
    enc.reset();
    GTEST_SKIP() << "no in-box H.264 encoder MFT produces output in this "
                    "session context (measured, see [h264-test] lines); "
                    "encode assertions NOT RUN headless — run interactive "
                    "via schtasks.";
  }
  std::fprintf(stderr, "[h264-test] selected encoder: %s\n",
               enc_name.c_str());
  // Pipeline semantics (seam contract): IDR at session start.
  enc->requestKeyframe();
  constexpr uint64_t kFrames = 30;
  for (uint64_t i = 0; i < kFrames; i++) {
    const bool ok = enc->encodeFrame(makeFrame(320, 200, i));
    ASSERT_TRUE(ok) << "encodeFrame failed at frame " << i << " ("
                    << sink.firstError() << ")";
  }
  ASSERT_TRUE(waitFor([&sink] { return sink.count() >= kFrames; }, 15000))
      << "expected " << kFrames << " encoded frames, got " << sink.count()
      << " within 15 s (encoder: " << enc_name << ")";
  EXPECT_EQ(sink.count(), kFrames);  // 1:1 in == out (measured behavior)
  EXPECT_EQ(sink.errorCount(), 0u) << "first error: " << sink.firstError();

  // First delivered frame: keyframe with in-band SPS+PPS+IDR (the
  // layout the client requires of the first keyframe —
  // VideoPipeline.swift:159-166).
  const auto f0 = sink.at(0);
  EXPECT_TRUE(f0.keyframe) << "first frame must be a keyframe (PROTOCOL "
                              "§9.5 first-packet rule)";
  EXPECT_TRUE(f0.annex_b_valid) << "first frame is not valid annex-B";
  EXPECT_TRUE(f0.has_sps) << "first keyframe lacks in-band SPS";
  EXPECT_TRUE(f0.has_pps) << "first keyframe lacks in-band PPS";
  EXPECT_TRUE(f0.has_idr) << "first keyframe lacks an IDR slice";
  EXPECT_GT(f0.bytes, 0u);
  EXPECT_LT(f0.bytes, 2u * 1024 * 1024);  // sane size for 320x200

  // All frames: valid annex-B, sane sizes, monotonic frame_seq,
  // source_seq passthrough.
  for (size_t i = 0; i < sink.count(); i++) {
    const auto fi = sink.at(i);
    EXPECT_TRUE(fi.annex_b_valid) << "frame " << i << " not annex-B";
    EXPECT_GT(fi.bytes, 0u) << "frame " << i << " empty";
    EXPECT_LT(fi.bytes, 2u * 1024 * 1024) << "frame " << i << " too large";
    EXPECT_EQ(fi.frame_seq, i) << "frame_seq not monotonic at " << i;
    EXPECT_EQ(fi.source_seq, i) << "source_seq not passed through at " << i;
  }
  enc->stop();
  EXPECT_FALSE(enc->dead());
}

// (3) Size-change reconfiguration A -> B mid-stream: the manager
// recreates the session on size change (seam contract), so the backend
// contract is a clean stop + fresh start at the new size with a valid
// continuing stream (fresh SPS/PPS at the new geometry).
TEST(H264EncoderMf, ReconfigureSizeMidStream) {
  RecordingSink sink;
  std::string enc_name;
  std::unique_ptr<win32::MfH264Encoder> enc;
  if (!tryStart(enc, sink, 320, 200, 30, enc_name)) {
    enc.reset();
    GTEST_SKIP() << "no working in-box H.264 encoder in this session "
                    "context; reconfigure assertions NOT RUN headless.";
  }
  enc->requestKeyframe();
  for (uint64_t i = 0; i < 8; i++) {
    ASSERT_TRUE(enc->encodeFrame(makeFrame(320, 200, i)));
  }
  ASSERT_TRUE(waitFor([&sink] { return sink.count() >= 8; }, 10000));
  // Clean teardown of the size-A session (the manager's recreation
  // path), then a fresh session at the new size.
  enc->stop();
  EXPECT_FALSE(enc->dead()) << "clean stop must not kill the backend ("
                            << sink.firstError() << ")";

  enc = std::make_unique<win32::MfH264Encoder>("rwh-h264-test", sink);
  ASSERT_TRUE(enc->start(480, 270, 30))
      << "restart at new size failed (encoder: " << enc_name << ")";
  enc->requestKeyframe();
  for (uint64_t i = 0; i < 8; i++) {
    ASSERT_TRUE(enc->encodeFrame(makeFrame(480, 270, i + 100)));
  }
  const size_t before = sink.count();
  ASSERT_TRUE(waitFor([&sink, before] { return sink.count() >= before + 8; },
                      10000))
      << "no encoded frames after reconfiguration";
  const auto f0b = sink.at(before);
  EXPECT_TRUE(f0b.keyframe) << "post-reconfigure first frame must be a "
                               "keyframe (new SPS/PPS at new size)";
  EXPECT_TRUE(f0b.has_sps) << "post-reconfigure keyframe lacks in-band SPS";
  EXPECT_TRUE(f0b.has_pps) << "post-reconfigure keyframe lacks in-band PPS";
  EXPECT_TRUE(f0b.has_idr) << "post-reconfigure keyframe lacks an IDR";
  enc->stop();
  EXPECT_FALSE(enc->dead());
}

// (4) On-demand keyframe: mid-stream requestKeyframe() -> the next
// delivered frame is an IDR with fresh in-band SPS/PPS.
TEST(H264EncoderMf, OnDemandKeyframe) {
  RecordingSink sink;
  std::string enc_name;
  std::unique_ptr<win32::MfH264Encoder> enc;
  if (!tryStart(enc, sink, 320, 200, 30, enc_name)) {
    enc.reset();
    GTEST_SKIP() << "no working in-box H.264 encoder in this session "
                    "context; keyframe assertions NOT RUN headless.";
  }
  enc->requestKeyframe();
  for (uint64_t i = 0; i < 5; i++) {
    ASSERT_TRUE(enc->encodeFrame(makeFrame(320, 200, i)));
  }
  ASSERT_TRUE(waitFor([&sink] { return sink.count() >= 5; }, 10000));
  // Mid-stream IDR request (data in flight: the 5th frame may still be
  // encoded when the request lands).
  enc->requestKeyframe();
  ASSERT_TRUE(enc->encodeFrame(makeFrame(320, 200, 5)));
  ASSERT_TRUE(waitFor([&sink] { return sink.count() >= 6; }, 10000))
      << "no frame after on-demand keyframe request";
  const auto fk = sink.at(5);
  EXPECT_TRUE(fk.keyframe) << "the frame after requestKeyframe() must be "
                              "an IDR (flush -> new stream start)";
  EXPECT_TRUE(fk.has_idr) << "requested keyframe payload lacks an IDR slice";
  EXPECT_TRUE(fk.has_sps) << "requested keyframe payload lacks in-band SPS";
  EXPECT_TRUE(fk.has_pps) << "requested keyframe payload lacks in-band PPS";
  enc->stop();
  EXPECT_FALSE(enc->dead());
}

// (5) Clean mid-stream shutdown: destroy the backend with frames still
// pending (no explicit stop() — the destructor must do a bounded
// EOF+drain and release everything) -> no crash, no CRT leak assert.
TEST(H264EncoderMf, CleanShutdownWithPendingFrames) {
  RecordingSink sink;
  std::string enc_name;
  std::unique_ptr<win32::MfH264Encoder> enc;
  if (!tryStart(enc, sink, 320, 200, 30, enc_name)) {
    enc.reset();  // nothing running: destructor path is trivially safe
    GTEST_SKIP() << "no working in-box H.264 encoder in this session "
                    "context; shutdown assertions NOT RUN headless.";
  }
  enc->requestKeyframe();
  // Feed several frames WITHOUT draining (encodeFrame only pumps a
  // short 10 ms per frame, so output remains pending at reset time).
  for (uint64_t i = 0; i < 10; i++) {
    ASSERT_TRUE(enc->encodeFrame(makeFrame(320, 200, i)));
  }
  // Destructor: bounded EOF + drain + COM release, no detached worker
  // (RW-H-013 lesson) — must not crash or trip the CRT leak check.
  enc.reset();
  // Same shape via the explicit stop() path (idempotent, safe after a
  // failed start — exercise the failed-start side too).
  std::unique_ptr<win32::MfH264Encoder> enc2;
  RecordingSink sink2;
  enc2 = std::make_unique<win32::MfH264Encoder>("rwh-h264-test", sink2);
  if (enc2->start(320, 200, 30)) {
    enc2->stop();
    enc2->stop();  // idempotent
  } else {
    enc2->stop();  // stop after failed start must be safe
  }
  enc2.reset();
  SUCCEED();  // reaching here = no crash, no leak assert
}
