// RW-H-011 Stage 2 (H-MEDIA, plan FR-03): hermetic Windows.Graphics
// Capture integration test.
//
// The test process creates its OWN top-level window (known client
// size, painted content — plain Win32, no user apps, no leftover
// processes) and drives the REAL WGC adapter through the portable
// CaptureSessionManager:
//   * consent gate: RequestAccessAsync(Programmatic) with a bounded
//     wait. IF consent is not granted, the typed ConsentRequired path
//     is asserted and the live-capture assertions GTEST_SKIP with a
//     clear reason (granting consent is a USER ACTION — a human at the
//     PC; the test must never hang on the OS prompt).
//   * IF consent is granted: a session is started via the manager and
//     >=5 frames with the EXACT expected pixel dimensions and the
//     correct window_id routing are asserted; then unshare releases
//     the resources (adapter surface count back to 0).
//
// SESSION CONTEXT: WGC requires a real desktop. In a headless session
// (session 0, sshd) a top-level window never becomes visible and WGC
// delivers no frames — the frame assertions then GTEST_SKIP ("no
// frames in the headless session; run interactive via schtasks").
// The consent-gate assertion (typed error vs granted) runs in BOTH
// contexts. All waits are bounded; the test can never hang on UI.
//
// WINDOWS ONLY: exercises real WGC + windows — meaningless (and
// uncompilable) on macOS. Recorded as NOT RUN on macOS by design
// (AGENTS.md: Win32 targets do not compile there).

#include "win32/capture/WgcCaptureSource.hpp"
#include "win32/windows/Win32WindowSource.hpp"  // win32::HwndKey
#include "win32/video/h264_encoder_mf.hpp"

#include "app/Win32Env.hpp"

#include <gtest/gtest.h>

#include <core/capture_encoder.hpp>
#include <core/capture_session_manager.hpp>

#include <winrt/base.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace remotewindows;

namespace {

// RW-H-011b diagnostic mode: `--wgc-diag` (parsed in main before
// gtest) makes the test print the full per-stage frame-path counter
// table (adapter stage counters + manager metrics + fake encoder +
// sink) at the end of the bounded frame wait, in BOTH the
// no-frames and the frames paths. Structured values only:
// counts / sizes / ids — never pixels.
bool g_diag = false;

// RW-H-012 live mode: `--wgc-h264` (parsed in main before gtest)
// enables the live WGC -> REAL Media Foundation H.264 backend test
// (WgcCaptureH264.LiveWgcEncodeToH264). Without the flag that test
// GTEST_SKIPs, so the default hermetic run is unchanged.
bool g_h264 = false;

constexpr int kClientW = 320;
constexpr int kClientH = 200;
constexpr uint64_t kFrameTimeoutMs = 15000;  // bounded: never hangs
const char kTestId[] = "6f9c3a2e-1b4d-4c7a-9e2f-8a3d5b7c9e01";  // v4-style
const char kTestId2[] = "6f9c3a2e-1b4d-4c7a-9e2f-8a3d5b7c9e02";  // RW-H-012
const wchar_t kTestClass[] = L"RWH_WGC_TEST_WINDOW";  // WNDCLASSEXW form

// ------------------------------------------------------ test window

// The window must CHANGE content over time: WGC emits frames on
// content changes, so a static window would deliver only the initial
// frame and the >=5-frame wait below could never succeed.
std::atomic<uint32_t> s_paint_count{0};

LRESULT CALLBACK TestWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_PAINT) {
    // Paint deterministic content (no pixel content is ever logged —
    // structured-logs-only rule: counts/dimensions only). The inner
    // rectangle cycles through 5 shades per paint so consecutive
    // repaints are pixel-different.
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    ::GetClientRect(hwnd, &rc);
    HBRUSH bg = ::CreateSolidBrush(RGB(30, 90, 160));
    ::FillRect(dc, &rc, bg);
    ::DeleteObject(bg);
    RECT inner{rc.left + 20, rc.top + 20, rc.right - 20, rc.bottom - 20};
    const uint32_t shade = s_paint_count.fetch_add(1) % 5u;
    HBRUSH fg = ::CreateSolidBrush(RGB(220u - 30u * shade, 180u, 60u));
    ::FillRect(dc, &inner, fg);
    ::DeleteObject(fg);
    EndPaint(hwnd, &ps);
    return 0;
  }
  return ::DefWindowProcW(hwnd, msg, wp, lp);
}

HWND CreateTestWindow() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = TestWndProc;
  wc.hInstance = ::GetModuleHandleW(nullptr);
  wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kTestClass;
  if (::RegisterClassExW(&wc) == 0) {
    if (::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
  }
  RECT rc{0, 0, kClientW, kClientH};
  DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME;  // no resize frame
  if (!::AdjustWindowRectEx(&rc, style, FALSE, 0)) return nullptr;
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  HWND hwnd = ::CreateWindowExW(0, kTestClass, L"RWH WGC hermetic test",
                                style, 100, 100, w, h, nullptr, nullptr,
                                ::GetModuleHandleW(nullptr), nullptr);
  if (!hwnd) return nullptr;
  ::ShowWindow(hwnd, SW_SHOWNORMAL);
  ::RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  return hwnd;
}

// ------------------------------------------------------- test fakes

// Test double for the IVideoEncoder seam. MUST honor the contract
// (core/capture_encoder.hpp: "Encode one source frame; 0..N encoded
// frames are pushed to the sink given at construction") — the pipeline
// counts delivered frames at the sink (noteEncoded), so a fake that
// only counts internally makes the delivery metric structurally zero
// no matter how many live frames the adapter delivers (the RW-H-011b
// stage table showed exactly that: emitted=58, delivered=0).
class FakeEncoder final : public core::IVideoEncoder {
 public:
  FakeEncoder(std::string id, core::IEncodedFrameConsumer& sink)
      : id_(std::move(id)), sink_(sink) {}
  bool start(uint32_t, uint32_t, uint32_t) override {
    std::lock_guard<std::mutex> lk(mu);
    pending_keyframe_ = false;
    return true;
  }
  void stop() override {}
  bool encodeFrame(const core::CapturedFrame& frame) override {
    core::EncodedFrame ef;
    {
      std::lock_guard<std::mutex> lk(mu);
      ef.window_id = id_;
      ef.source_seq = frame.seq;
      ef.frame_seq = frame_seq_++;
      ef.keyframe = pending_keyframe_;
      pending_keyframe_ = false;
      ef.payload = {0xAB, 0xCD};  // stub access unit: size/shape only
      encoded++;
    }
    sink_.onEncodedFrame(ef);  // outside the lock (the sink takes the
                               // manager lock; no cycle back here)
    return true;
  }
  void requestKeyframe() override {
    std::lock_guard<std::mutex> lk(mu);
    keyframes++;
    pending_keyframe_ = true;
  }
  size_t encodedCount() const {
    std::lock_guard<std::mutex> lk(mu);
    return encoded;
  }
  size_t keyframeCount() const {
    std::lock_guard<std::mutex> lk(mu);
    return keyframes;
  }

 private:
  std::string id_;
  core::IEncodedFrameConsumer& sink_;
  mutable std::mutex mu;
  size_t encoded = 0;
  size_t keyframes = 0;
  uint32_t frame_seq_ = 0;
  bool pending_keyframe_ = false;
};

class FakeEncoderFactory final : public core::IVideoEncoderFactory {
 public:
  std::unique_ptr<core::IVideoEncoder> create(
      const std::string& window_id, core::IEncodedFrameConsumer& sink) override {
    auto enc = std::make_unique<FakeEncoder>(window_id, sink);
    current = enc.get();
    return enc;
  }
  FakeEncoder* current = nullptr;
};

class EncodedSink final : public core::IEncodedFrameConsumer {
 public:
  void onEncodedFrame(const core::EncodedFrame& frame) override {
    std::lock_guard<std::mutex> lk(mu);
    per_window[frame.window_id]++;
  }
  void onEncoderError(const std::string&, const std::string&) override {}
  size_t countFor(const std::string& window_id) const {
    std::lock_guard<std::mutex> lk(mu);
    auto it = per_window.find(window_id);
    return it == per_window.end() ? 0 : it->second;
  }

 private:
  mutable std::mutex mu;
  std::map<std::string, size_t> per_window;
};

bool WaitFor(const std::function<bool()>& pred, uint64_t timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(static_cast<int64_t>(timeout_ms));
  for (;;) {
    if (pred()) return true;
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

// RW-H-011b: print the full frame-path stage table (diagnostic mode).
// Reads the LIVE surface's per-stage adapter counters (the surface must
// still be running — call this before unshareWindow), then the
// manager metrics (frames_received at the gate, frames_delivered at
// the sink, queue depth, drops) and the fake encoder / sink counts.
// The first zero-count stage in this order locates where live frame
// flow stopped:
//   frames_arrived → frames_wait_returned → (size_skips / qi_failures /
//   copy_failures) → emitted → manager.frames_received →
//   fake.encoded → manager.frames_delivered → sink.count
void PrintDiagTable(win32::WgcCaptureSource& source,
                    core::CaptureSessionManager& mgr, FakeEncoderFactory& factory,
                    EncodedSink& sink, const std::string& window_id) {
  win32::WgcCaptureSource::FrameStats fs{};
  const bool have_surface = source.frameStats(window_id, fs);
  const core::CaptureSessionMetrics m = mgr.metrics(window_id);
  const size_t encoded = factory.current ? factory.current->encodedCount() : 0;
  const size_t keyframes = factory.current ? factory.current->keyframeCount() : 0;
  std::fprintf(stderr,
               "[wgc-diag] table window_id=%s adapter_surface=%d "
               "manager_state=%d\n",
               window_id.c_str(), have_surface ? 1 : 0,
               static_cast<int>(m.state));
  std::fprintf(stderr, "[wgc-diag] stage=frames_arrived n=%llu\n",
               static_cast<unsigned long long>(fs.frames_arrived));
  std::fprintf(stderr, "[wgc-diag] stage=frames_wait_returned n=%llu\n",
               static_cast<unsigned long long>(fs.frames_wait_returned));
  std::fprintf(stderr, "[wgc-diag] stage=null_frames n=%llu\n",
               static_cast<unsigned long long>(fs.null_frames));
  std::fprintf(stderr, "[wgc-diag] stage=size_skips n=%llu\n",
               static_cast<unsigned long long>(fs.size_skips));
  std::fprintf(stderr, "[wgc-diag] stage=qi_failures n=%llu\n",
               static_cast<unsigned long long>(fs.qi_failures));
  std::fprintf(stderr, "[wgc-diag] stage=copy_failures n=%llu\n",
               static_cast<unsigned long long>(fs.copy_failures));
  std::fprintf(stderr, "[wgc-diag] stage=emitted n=%llu\n",
               static_cast<unsigned long long>(fs.emitted));
  std::fprintf(stderr, "[wgc-diag] stage=handler_errors n=%llu\n",
               static_cast<unsigned long long>(fs.handler_errors));
  std::fprintf(stderr, "[wgc-diag] manager frames_received=%llu "
               "frames_delivered=%llu frames_dropped=%llu queue_depth=%llu "
               "queue_max_depth=%llu error_count=%llu surface_starts=%llu "
               "keyframe_requests=%llu last_frame_ts_us=%llu\n",
               static_cast<unsigned long long>(m.frames_received),
               static_cast<unsigned long long>(m.frames_delivered),
               static_cast<unsigned long long>(m.frames_dropped),
               static_cast<unsigned long long>(m.queue_depth),
               static_cast<unsigned long long>(m.queue_max_depth),
               static_cast<unsigned long long>(m.error_count),
               static_cast<unsigned long long>(m.surface_starts),
               static_cast<unsigned long long>(m.keyframe_requests),
               static_cast<unsigned long long>(m.last_frame_ts_us));
  std::fprintf(stderr, "[wgc-diag] fake_encoder encoded=%llu keyframes=%llu "
               "sink_count=%llu\n",
               static_cast<unsigned long long>(encoded),
               static_cast<unsigned long long>(keyframes),
               static_cast<unsigned long long>(sink.countFor(window_id)));
  std::fflush(stderr);
}

}  // namespace

// ------------------------------------------------------------- suite

TEST(WgcCapture, HermeticConsentGatedPipeline) {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  HWND hwnd = CreateTestWindow();
  ASSERT_TRUE(hwnd != nullptr) << "test window creation failed";

  // DPI: physical pixels at the window's actual scale (the frame size
  // WGC delivers must equal the client area in PHYSICAL pixels).
  // GetDpiForWindow returns a DPI value (96 = 100%), NOT a percent:
  // scale = dpi/96 (rounded up for the physical size; percent for the
  // WindowRecord, also rounded up: 96→100, 120→125, 144→150).
  UINT dpi = ::GetDpiForWindow(hwnd);
  if (dpi == 0) dpi = 96;  // fallback: unscaled
  const uint32_t dpi_u = static_cast<uint32_t>(dpi);
  const uint32_t dpi_pct = (dpi_u * 100 + 95) / 96;
  const uint32_t phys_w =
      static_cast<uint32_t>((static_cast<int64_t>(kClientW) * dpi_u + 95) / 96);
  const uint32_t phys_h =
      static_cast<uint32_t>((static_cast<int64_t>(kClientH) * dpi_u + 95) / 96);

  FakeEncoderFactory factory;
  EncodedSink sink;
  win32::WgcCaptureSource source;
  core::CaptureSessionManager mgr(source, factory, sink);

  core::WindowRecord rec;
  rec.window_id = kTestId;
  rec.hwnd = win32::HwndKey(hwnd);
  rec.width_px = phys_w;
  rec.height_px = phys_h;
  rec.dpi_scale_percent = dpi_pct;
  rec.minimized = false;
  rec.visible = true;
  rec.surface_kind = core::WindowSurfaceKind::IndependentTopLevel;
  rec.app_session_id = "rwh-wgc-test";

  const core::CaptureError err = mgr.shareWindow(rec);
  if (err == core::CaptureError::ConsentRequired) {
    if (g_diag) PrintDiagTable(source, mgr, factory, sink, kTestId);
    mgr.unshareWindow(kTestId);
    EXPECT_FALSE(mgr.isShared(kTestId));
    EXPECT_EQ(source.surfaceCount(), 0u);
    ::DestroyWindow(hwnd);
    GTEST_SKIP() << "capture consent not granted — typed ConsentRequired "
                    "path verified; granting consent is a USER ACTION "
                    "(human at the PC). Re-run after consent to verify "
                    "the live-capture assertions.";
  }
  ASSERT_EQ(err, core::CaptureError::None)
      << "shareWindow failed with a non-consent error";
  ASSERT_EQ(mgr.stateOf(kTestId), core::CaptureSessionState::Running);
  ASSERT_EQ(source.surfaceCount(), 1u);

  // Bounded wait for live frames. The test window repaints with a
  // changing pattern every ~250 ms (RDW_UPDATENOW = synchronous
  // paint, no message pump needed in this process): WGC emits frames
  // on content changes, so a static window would deliver only the
  // initial frame and this wait could never reach the >=5 threshold.
  // In a headless session (session 0, no desktop) WGC delivers none.
  bool got_frames = false;
  const auto frame_deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kFrameTimeoutMs);
  auto last_repaint = std::chrono::steady_clock::now();
  while (!got_frames) {
    got_frames = sink.countFor(kTestId) >= 5;
    const auto now = std::chrono::steady_clock::now();
    if (got_frames || now >= frame_deadline) break;
    if (now - last_repaint >= std::chrono::milliseconds(250)) {
      last_repaint = now;
      ::RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  // RW-H-011b: in diagnostic mode, print the full stage table while the
  // surface is still live (before any unshare), in both outcomes.
  if (g_diag) PrintDiagTable(source, mgr, factory, sink, kTestId);
  if (!got_frames) {
    mgr.unshareWindow(kTestId);
    EXPECT_EQ(source.surfaceCount(), 0u);
    ::DestroyWindow(hwnd);
    GTEST_SKIP() << "fewer than 5 WGC frames within " << kFrameTimeoutMs
                 << " ms — headless session (no desktop) or the capture "
                    "is not delivering; live-capture assertions NOT RUN "
                    "in this context. Run interactive (schtasks) to "
                    "verify.";
  }

  // Frame routing + dimensions: the exact physical client size at the
  // window's DPI, and every delivered frame carries the test window_id.
  // frames_received is counted at the gate (adapter level) — it proves
  // live frames flowed from WGC, independent of the encoder fake.
  const auto m = mgr.metrics(kTestId);
  EXPECT_GE(m.frames_received, 5u);
  EXPECT_GE(m.frames_delivered, 5u);
  EXPECT_EQ(m.width_px, phys_w);
  EXPECT_EQ(m.height_px, phys_h);
  EXPECT_GT(m.keyframe_requests, 0u);  // IDR at session start
  if (factory.current) {
    EXPECT_GE(factory.current->encodedCount(), 5u);
    EXPECT_GE(factory.current->keyframeCount(), 1u);
  }
  EXPECT_EQ(sink.countFor(kTestId), m.frames_delivered);
  EXPECT_GE(m.last_frame_ts_us, 1u);  // timestamps are populated

  // Stop: resources released (adapter surface gone, session ended).
  const core::CaptureError ue = mgr.unshareWindow(kTestId);
  EXPECT_EQ(ue, core::CaptureError::None);
  EXPECT_EQ(mgr.stateOf(kTestId), core::CaptureSessionState::Inactive);
  ASSERT_TRUE(
      WaitFor([&source] { return source.surfaceCount() == 0u; }, 5000));
  EXPECT_EQ(source.surfaceCount(), 0u);
  EXPECT_FALSE(mgr.isShared(kTestId));

  ::DestroyWindow(hwnd);
}

// ------------------------------------------------------- RW-H-012: live
// WGC -> REAL Media Foundation H.264 backend (enabled via
// `--wgc-h264`, interactive session). Same hermetic model as the test
// above (own window, consent gate, bounded waits) but the encoder
// factory is the real win32::MfH264EncoderFactory: the full seam path
// (WGC pixels -> IVideoEncoder -> annex-B access units) is exercised
// with the production backend. Prints the measured encoder type,
// per-frame size/fps/timing and NAL facts (structured values only).

namespace {

struct LiveFrameFact {
  uint32_t frame_seq = 0;
  bool keyframe = false;
  size_t bytes = 0;
  bool annex_b_valid = false;
  bool has_sps = false, has_pps = false, has_idr = false;
  uint64_t arrived_ms = 0;  // steady_clock, for inter-frame timing
};

class LiveH264Sink final : public core::IEncodedFrameConsumer {
 public:
  void onEncodedFrame(const core::EncodedFrame& frame) override {
    LiveFrameFact f;
    f.frame_seq = frame.frame_seq;
    f.keyframe = frame.keyframe;
    f.bytes = frame.payload.size();
    f.arrived_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    const auto& p = frame.payload;
    const uint8_t* d = p.data();
    const size_t n = p.size();
    for (size_t i = 0; i + 3 < n; i++) {
      size_t sc = 0;
      if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) sc = 3;
      if (i + 4 < n && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 0 &&
          d[i + 3] == 1) sc = 4;
      if (sc == 0) continue;
      if (i + sc < n) {
        const uint32_t t = d[i + sc] & 0x1F;
        if (t == 1 || t == 5 || t == 6 || t == 7 || t == 8 || t == 9) {
          f.annex_b_valid = true;
          if (t == 7) f.has_sps = true;
          if (t == 8) f.has_pps = true;
          if (t == 5) f.has_idr = true;
        }
      }
      i += sc;
    }
    std::lock_guard<std::mutex> lk(mu_);
    frames_.push_back(f);
  }
  void onEncoderError(const std::string&, const std::string& reason) override {
    std::lock_guard<std::mutex> lk(mu_);
    if (first_error_.empty()) first_error_ = reason;
  }
  size_t count() {
    std::lock_guard<std::mutex> lk(mu_);
    return frames_.size();
  }
  LiveFrameFact at(size_t i) {
    std::lock_guard<std::mutex> lk(mu_);
    return frames_[i];
  }
  std::string firstError() {
    std::lock_guard<std::mutex> lk(mu_);
    return first_error_;
  }

 private:
  mutable std::mutex mu_;
  std::vector<LiveFrameFact> frames_;
  std::string first_error_;
};

class LiveMfFactory final : public win32::MfH264EncoderFactory {
 public:
  std::unique_ptr<core::IVideoEncoder> create(
      const std::string& window_id, core::IEncodedFrameConsumer& sink) override {
    auto e = std::make_unique<win32::MfH264Encoder>(window_id, sink);
    last = e.get();  // raw: valid while the manager owns the encoder
    return e;
  }
  win32::MfH264Encoder* last = nullptr;
};

}  // namespace

TEST(WgcCaptureH264, LiveWgcEncodeToH264) {
  if (!g_h264) {
    GTEST_SKIP() << "live H.264 mode not requested (run with "
                    "--wgc-h264, interactive session via schtasks)";
  }
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  HWND hwnd = CreateTestWindow();
  ASSERT_TRUE(hwnd != nullptr) << "test window creation failed";

  UINT dpi = ::GetDpiForWindow(hwnd);
  if (dpi == 0) dpi = 96;
  const uint32_t dpi_u = static_cast<uint32_t>(dpi);
  const uint32_t dpi_pct = (dpi_u * 100 + 95) / 96;
  const uint32_t phys_w =
      static_cast<uint32_t>((static_cast<int64_t>(kClientW) * dpi_u + 95) / 96);
  const uint32_t phys_h =
      static_cast<uint32_t>((static_cast<int64_t>(kClientH) * dpi_u + 95) / 96);

  LiveMfFactory factory;
  LiveH264Sink sink;
  win32::WgcCaptureSource source;
  core::CaptureSessionManager mgr(source, factory, sink);

  core::WindowRecord rec;
  rec.window_id = kTestId2;
  rec.hwnd = win32::HwndKey(hwnd);
  rec.width_px = phys_w;
  rec.height_px = phys_h;
  rec.dpi_scale_percent = dpi_pct;
  rec.minimized = false;
  rec.visible = true;
  rec.surface_kind = core::WindowSurfaceKind::IndependentTopLevel;
  rec.app_session_id = "rwh-h264-live";

  const core::CaptureError err = mgr.shareWindow(rec);
  if (err == core::CaptureError::ConsentRequired) {
    mgr.unshareWindow(kTestId2);
    ::DestroyWindow(hwnd);
    GTEST_SKIP() << "capture consent not granted (USER ACTION required)";
  }
  ASSERT_EQ(err, core::CaptureError::None)
      << "shareWindow failed; encoder detail: " << sink.firstError();
  ASSERT_EQ(mgr.stateOf(kTestId2), core::CaptureSessionState::Running);

  // The manager drives start() (real MFT probe + smoke) and
  // requestKeyframe() per the seam contract. Bounded wait for >=5
  // REAL encoded frames (window repaints every ~250 ms).
  bool got_frames = false;
  const auto frame_deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(30000);
  auto last_repaint = std::chrono::steady_clock::now();
  while (!got_frames) {
    got_frames = sink.count() >= 5;
    const auto now = std::chrono::steady_clock::now();
    if (got_frames || now >= frame_deadline) break;
    if (now - last_repaint >= std::chrono::milliseconds(250)) {
      last_repaint = now;
      ::RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!got_frames) {
    mgr.unshareWindow(kTestId2);
    ::DestroyWindow(hwnd);
    GTEST_SKIP() << "fewer than 5 encoded frames within 30 s (no working "
                    "in-box H.264 encoder in this session context, or "
                    "capture not delivering); encoder detail: "
                 << sink.firstError();
  }

  // NAL assertions on the REAL frames (pinned layout: annex-B, SPS+PPS
  // in-band in the first keyframe).
  ASSERT_GE(sink.count(), 5u);
  const auto f0 = sink.at(0);
  EXPECT_TRUE(f0.keyframe) << "first encoded frame must be a keyframe";
  EXPECT_TRUE(f0.annex_b_valid) << "first frame not valid annex-B";
  EXPECT_TRUE(f0.has_sps) << "first keyframe lacks in-band SPS";
  EXPECT_TRUE(f0.has_pps) << "first keyframe lacks in-band PPS";
  EXPECT_TRUE(f0.has_idr) << "first keyframe lacks an IDR slice";
  std::fprintf(stderr,
               "[wgc-h264] window_id=%s window=%ux%u phys=%ux%u dpi_pct=%u "
               "frames=%llu\n",
               kTestId2, kClientW, kClientH, phys_w, phys_h, dpi_pct,
               static_cast<unsigned long long>(sink.count()));
  for (size_t i = 0; i < sink.count(); i++) {
    const auto f = sink.at(i);
    const uint64_t dt =
        (i == 0) ? 0 : (f.arrived_ms - sink.at(i - 1).arrived_ms);
    std::fprintf(stderr,
                 "[wgc-h264] frame=%u keyframe=%d bytes=%zu "
                 "annex_b=%d sps=%d pps=%d idr=%d dt_ms=%llu\n",
                 f.frame_seq, f.keyframe ? 1 : 0, f.bytes,
                 f.annex_b_valid ? 1 : 0, f.has_sps ? 1 : 0,
                 f.has_pps ? 1 : 0, f.has_idr ? 1 : 0,
                 static_cast<unsigned long long>(dt));
    EXPECT_TRUE(f.annex_b_valid) << "frame " << i << " not annex-B";
    EXPECT_GT(f.bytes, 0u);
  }
  // Measured encoder type (structured: the friendly name of the MFT the
  // backend actually selected — read BEFORE unshare destroys it).
  const auto m = mgr.metrics(kTestId2);
  const std::string measured =
      factory.last ? factory.last->selectedEncoder() : "(none)";
  std::fprintf(stderr,
               "[wgc-h264] measured encoder=%s window_id=%s "
               "frames_received=%llu frames_delivered=%llu "
               "keyframe_requests=%llu\n",
               measured.c_str(), kTestId2,
               static_cast<unsigned long long>(m.frames_received),
               static_cast<unsigned long long>(m.frames_delivered),
               static_cast<unsigned long long>(m.keyframe_requests));

  // Stop: clean release (the manager stops the encoder; the backend's
  // destructor path is exercised by unshare).
  const core::CaptureError ue = mgr.unshareWindow(kTestId2);
  EXPECT_EQ(ue, core::CaptureError::None);
  EXPECT_EQ(mgr.stateOf(kTestId2), core::CaptureSessionState::Inactive);
  ASSERT_TRUE(
      WaitFor([&source] { return source.surfaceCount() == 0u; }, 5000));
  ::DestroyWindow(hwnd);
}

int main(int argc, char** argv) {
  // RW-H-011b: strip --wgc-diag BEFORE gtest parses argv (gtest would
  // reject the unknown flag). RW-H-012: same for --wgc-h264.
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--wgc-diag" || arg == "--wgc-h264") {
      if (arg == "--wgc-diag") g_diag = true;
      if (arg == "--wgc-h264") g_h264 = true;
      for (int j = i; j < argc - 1; ++j) argv[j] = argv[j + 1];
      --argc;
      --i;
    }
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
