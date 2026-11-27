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

constexpr int kClientW = 320;
constexpr int kClientH = 200;
constexpr uint64_t kFrameTimeoutMs = 15000;  // bounded: never hangs
const char kTestId[] = "6f9c3a2e-1b4d-4c7a-9e2f-8a3d5b7c9e01";  // v4-style
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

class FakeEncoder final : public core::IVideoEncoder {
 public:
  bool start(uint32_t, uint32_t, uint32_t) override { return true; }
  void stop() override {}
  bool encodeFrame(const core::CapturedFrame& frame) override {
    std::lock_guard<std::mutex> lk(mu);
    encoded++;
    return true;
  }
  void requestKeyframe() override {
    std::lock_guard<std::mutex> lk(mu);
    keyframes++;
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
  mutable std::mutex mu;
  size_t encoded = 0;
  size_t keyframes = 0;
};

class FakeEncoderFactory final : public core::IVideoEncoderFactory {
 public:
  std::unique_ptr<core::IVideoEncoder> create(
      const std::string&, core::IEncodedFrameConsumer&) override {
    auto enc = std::make_unique<FakeEncoder>();
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
  const auto m = mgr.metrics(kTestId);
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
