
// RW-H-011 (H-MEDIA): portable capture-session pipeline tests —
// FR-03/FR-04/FR-06: per-window independence, auto-share of auxiliary
// surfaces (any pid, same app session — correlation from the real
// WindowRegistry), parent retention, minimize pause/low-fps, size-change
// recreation, bounded drop-obsolete gate, per-session metrics,
// SourceClosed isolation, consent typing.
//
// Portable: scripted registry events (the same seam the Win32 adapter
// drives) + a fake ICaptureSurfaceSource/IVideoEncoder. The registry is
// the REAL core::WindowRegistry — notifications are forwarded to the
// manager exactly as the app layer will do it.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/capture_session_manager.hpp"
#include "core/window_registry.hpp"

using namespace remotewindows;

namespace {

// ------------------------------------------------------------------ fakes

// Scripted capture source: records start/stop calls + geometry, can
// inject typed errors (ConsentRequired at start, SourceClosed async).
class FakeSource : public core::ICaptureSurfaceSource {
 public:
  struct StartRec {
    uint32_t width = 0, height = 0, dpi = 100;
  };

  core::CaptureError startSurface(const core::CaptureSurfaceRequest& req,
                                  core::IFrameConsumer& consumer) override {
    std::lock_guard<std::mutex> lk(mu_);
    starts_[req.window_id]++;
    start_geos_[req.window_id].push_back({req.width_px, req.height_px,
                                          req.dpi_scale_percent});
    auto it = fail_next_starts_.find(req.window_id);
    if (it != fail_next_starts_.end() && it->second > 0) {
      it->second--;
      running_[req.window_id] = false;
      return core::CaptureError::SourceClosed;  // scripted adapter failure
    }
    running_[req.window_id] = true;
    consumers_[req.window_id] = &consumer;
    return core::CaptureError::None;
  }

  core::CaptureError stopSurface(const std::string& window_id) override {
    std::lock_guard<std::mutex> lk(mu_);
    stops_[window_id]++;
    running_[window_id] = false;
    return core::CaptureError::None;
  }

  bool isRunning(const std::string& window_id) const override {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = running_.find(window_id);
    return it != running_.end() && it->second;
  }

  // test knobs (set BEFORE the call they script)
  void scriptSourceClosedOnNextStart(const std::string& window_id, int n) {
    std::lock_guard<std::mutex> lk(mu_);
    fail_next_starts_[window_id] = n;
  }

  // Deliver a frame / async error through the stored consumer.
  void emit(const core::CapturedFrame& frame) {
    core::IFrameConsumer* c = nullptr;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = consumers_.find(frame.window_id);
      if (it != consumers_.end()) c = it->second;
    }
    if (c) c->onCapturedFrame(frame);
  }
  void fail(const std::string& window_id, core::CaptureError error) {
    core::IFrameConsumer* c = nullptr;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = consumers_.find(window_id);
      if (it != consumers_.end()) c = it->second;
    }
    if (c) c->onSourceError(window_id, error);
  }

  // test queries
  int startCount(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = starts_.find(id);
    return it == starts_.end() ? 0 : it->second;
  }
  int stopCount(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = stops_.find(id);
    return it == stops_.end() ? 0 : it->second;
  }
  std::vector<StartRec> startGeos(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = start_geos_.find(id);
    return it == start_geos_.end() ? std::vector<StartRec>{}
                                   : it->second;
  }

 private:
  mutable std::mutex mu_;
  std::map<std::string, int> starts_;
  std::map<std::string, int> stops_;
  std::map<std::string, bool> running_;
  std::map<std::string, std::vector<StartRec>> start_geos_;
  std::map<std::string, core::IFrameConsumer*> consumers_;
  std::map<std::string, int> fail_next_starts_;
};

// Per-encoder counters (one stats object per encoder instance; the
// factory replaces it on every (re)start).
struct FakeEncoderStats {
  std::atomic<int> started{0};
  std::atomic<int> keyframe_requests{0};
  std::atomic<int> encoded{0};
  std::atomic<int> keyframe_frames{0};
  std::atomic<int> delay_us{0};
  std::atomic<uint32_t> width{0};
  std::atomic<uint32_t> height{0};
};

class FakeEncoder : public core::IVideoEncoder {
 public:
  FakeEncoder(std::string id, core::IEncodedFrameConsumer& sink,
              std::shared_ptr<FakeEncoderStats> st)
      : id_(std::move(id)), sink_(sink), st_(std::move(st)) {}

  bool start(uint32_t width, uint32_t height, uint32_t /*fps*/) override {
    st_->started++;
    st_->width = width;
    st_->height = height;
    pending_keyframe_ = false;
    return true;
  }
  bool encodeFrame(const core::CapturedFrame& frame) override {
    const int d = st_->delay_us.load();
    if (d > 0) std::this_thread::sleep_for(std::chrono::microseconds(d));
    core::EncodedFrame ef;
    ef.window_id = id_;
    ef.source_seq = frame.seq;
    ef.frame_seq = frame_seq_++;
    ef.keyframe = pending_keyframe_;
    if (ef.keyframe) {
      pending_keyframe_ = false;
      st_->keyframe_frames++;
    }
    ef.payload = {0xAB, 0xCD};
    st_->encoded++;
    sink_.onEncodedFrame(ef);
    return true;
  }
  void requestKeyframe() override {
    pending_keyframe_ = true;
    st_->keyframe_requests++;
  }
  void stop() override {}

 private:
  std::string id_;
  core::IEncodedFrameConsumer& sink_;
  std::shared_ptr<FakeEncoderStats> st_;
  bool pending_keyframe_ = false;
  uint32_t frame_seq_ = 0;
};

class FakeEncoderFactory : public core::IVideoEncoderFactory {
 public:
  std::unique_ptr<core::IVideoEncoder> create(
      const std::string& window_id,
      core::IEncodedFrameConsumer& sink) override {
    auto st = std::make_shared<FakeEncoderStats>();
    {
      std::lock_guard<std::mutex> lk(mu_);
      stats_[window_id] = st;
    }
    return std::make_unique<FakeEncoder>(window_id, sink, std::move(st));
  }
  std::shared_ptr<FakeEncoderStats> current(const std::string& window_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = stats_.find(window_id);
    return it == stats_.end() ? nullptr : it->second;
  }

 private:
  std::mutex mu_;
  std::map<std::string, std::shared_ptr<FakeEncoderStats>> stats_;
};

class EncodedSink : public core::IEncodedFrameConsumer {
 public:
  void onEncodedFrame(const core::EncodedFrame& frame) override {
    std::lock_guard<std::mutex> lk(mu_);
    frames_.push_back(frame);
  }
  void onEncoderError(const std::string& window_id,
                      const std::string& reason) override {
    std::lock_guard<std::mutex> lk(mu_);
    errors_.emplace_back(window_id, reason);
  }
  int countFor(const std::string& window_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    int n = 0;
    for (const auto& f : frames_)
      if (f.window_id == window_id) n++;
    return n;
  }
  size_t errorCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return errors_.size();
  }

 private:
  mutable std::mutex mu_;
  std::vector<core::EncodedFrame> frames_;
  std::vector<std::pair<std::string, std::string>> errors_;
};

// ---------------------------------------------------------------- helpers

core::CapturedFrame frame(const std::string& wid, uint64_t seq,
                          uint64_t ts_us, uint32_t w = 8, uint32_t h = 8) {
  core::CapturedFrame f;
  f.window_id = wid;
  f.seq = seq;
  f.timestamp_us = ts_us;
  f.width = w;
  f.height = h;
  f.format = core::CaptureFrameFormat::Bgra8;
  f.pixels.assign(w * h * 4, 0x11);
  return f;
}

template <typename Pred>
bool waitFor(Pred pred, int timeout_ms = 5000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return pred();
}

// Full first-sight observation (mirrors tests/window_registry_test.cpp).
core::NativeWindowInfo makeInfo(const std::string& hwnd, uint32_t pid,
                                uint32_t w = 100, uint32_t h = 50,
                                uint32_t dpi = 100) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.pid = pid;
  i.tid = pid * 10;
  i.has_title = true;
  i.title = "T" + hwnd;
  i.has_class = true;
  i.window_class = "App";
  i.has_geometry = true;
  i.x = 10;
  i.y = 20;
  i.width_px = w;
  i.height_px = h;
  i.dpi_scale_percent = dpi;
  i.has_visibility = true;
  i.visible = true;
  i.has_state = true;
  i.enabled = true;
  i.minimized = false;
  i.maximized = false;
  i.has_z_order = true;
  i.z_order_rank = 0;
  i.has_relationships = true;
  return i;
}

// Create a window in the registry and forward all notifications to the
// manager (the app-layer seam). Returns the durable window_id.
std::string createWin(core::WindowRegistry& reg,
                      core::CaptureSessionManager& mgr, const std::string& hwnd,
                      uint32_t pid, uint32_t w = 100, uint32_t h = 50,
                      uint32_t dpi = 100,
                      std::optional<std::string> owner_hwnd = std::nullopt,
                      bool child_window = false) {
  auto info = makeInfo(hwnd, pid, w, h, dpi);
  info.owner_hwnd = owner_hwnd;
  if (owner_hwnd.has_value()) info.root_owner_hwnd = owner_hwnd;
  info.is_child_window = child_window;
  if (child_window) info.parent_hwnd = owner_hwnd;
  auto notifs = reg.applyEvent(core::WindowEvent{core::WinEventKind::Create,
                                                 std::move(info)});
  mgr.onRegistryNotifications(notifs);
  auto rec = reg.findByHwnd(hwnd);
  EXPECT_TRUE(rec.has_value());
  return rec ? rec->window_id : std::string();
}

void moveResize(core::WindowRegistry& reg, core::CaptureSessionManager& mgr,
                const std::string& hwnd, uint32_t w, uint32_t h,
                uint32_t dpi = 100) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.has_geometry = true;
  i.x = 10;
  i.y = 20;
  i.width_px = w;
  i.height_px = h;
  i.dpi_scale_percent = dpi;
  mgr.onRegistryNotifications(
      reg.applyEvent(core::WindowEvent{core::WinEventKind::LocationChange,
                                       std::move(i)}));
}

void setMinimized(core::WindowRegistry& reg, core::CaptureSessionManager& mgr,
                  const std::string& hwnd, bool minimized) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.has_state = true;
  i.enabled = true;
  i.minimized = minimized;
  i.maximized = false;
  auto kind = minimized ? core::WinEventKind::Minimize
                        : core::WinEventKind::Restore;
  mgr.onRegistryNotifications(
      reg.applyEvent(core::WindowEvent{kind, std::move(i)}));
}

void destroyWin(core::WindowRegistry& reg, core::CaptureSessionManager& mgr,
                const std::string& hwnd) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  mgr.onRegistryNotifications(
      reg.applyEvent(core::WindowEvent{core::WinEventKind::Destroy,
                                       std::move(i)}));
}

// Shared fixture wiring (real registry + fakes). The manager is created
// via start() AFTER the config is set (the manager copies its config).
struct Fixture {
  core::WindowRegistry reg;
  FakeSource source;
  FakeEncoderFactory factory;
  EncodedSink sink;
  core::CaptureManagerConfig config;
  std::unique_ptr<core::CaptureSessionManager> mgr;

  void start() {
    mgr = std::make_unique<core::CaptureSessionManager>(source, factory,
                                                        sink, config);
  }

  // Share a tracked window explicitly (the app-layer anchor path).
  core::CaptureError share(const std::string& window_id) {
    auto rec = reg.find(window_id);
    EXPECT_TRUE(rec.has_value());
    return rec ? mgr->shareWindow(*rec) : core::CaptureError::InternalError;
  }
  core::CaptureSessionMetrics m(const std::string& id) const {
    return mgr->metrics(id);
  }
};

}  // namespace

// ------------------------------------------------------------------- gate

TEST(CaptureGate, BoundedDropObsoleteKeepsNewest) {
  core::FrameGate gate(4);
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(gate.submit(frame("w", i, i)));
  }
  EXPECT_EQ(gate.depth(), 4u);            // never exceeds the fixed bound
  EXPECT_EQ(gate.maxDepth(), 4u);
  EXPECT_EQ(gate.droppedCount(), 6u);     // obsolete evictions counted
  // Kept = the NEWEST four (FIFO order preserved).
  core::CapturedFrame f;
  for (int expected = 6; expected <= 9; ++expected) {
    ASSERT_TRUE(gate.pop(f));
    EXPECT_EQ(f.seq, static_cast<uint64_t>(expected));
  }
  EXPECT_FALSE(gate.pop(f));
}

// ---------------------------------------------------------- typed errors

TEST(CapturePipeline, ConsentRequiredTypedErrorIsSurfaced) {
  Fixture fx;
  fx.start();
  const std::string id =
      createWin(fx.reg, *fx.mgr, "0x1", 100);
  // Exercise the typed error through the same seam the WGC adapter uses:
  // a source that reports ConsentRequired at start must surface it
  // verbatim (and never block on the consent dialog).
  struct ConsentSource : public core::ICaptureSurfaceSource {
    core::CaptureError startSurface(const core::CaptureSurfaceRequest&,
                                    core::IFrameConsumer&) override {
      ++calls;
      return core::CaptureError::ConsentRequired;
    }
    core::CaptureError stopSurface(const std::string&) override {
      return core::CaptureError::None;
    }
    bool isRunning(const std::string&) const override { return false; }
    int calls = 0;
  };
  ConsentSource consent_src;
  FakeEncoderFactory factory;
  EncodedSink sink;
  core::CaptureSessionManager mgr(consent_src, factory, sink);
  auto rec = fx.reg.find(id).value();
  EXPECT_EQ(mgr.shareWindow(rec), core::CaptureError::ConsentRequired);
  EXPECT_FALSE(mgr.isShared(id));
  EXPECT_EQ(mgr.sharedCount(), 0u);
  EXPECT_EQ(consent_src.calls, 1);  // exactly one attempt, never retried
}

// --------------------------------------------------- per-window behavior

TEST(CapturePipeline, PerWindowIndependenceStopKeepsOthers) {
  Fixture fx;
  fx.start();
  // Two unrelated app sessions (different anchors).
  fx.reg.registerSession("sessA", 100);
  fx.reg.registerSession("sessB", 200);
  const std::string idA = createWin(fx.reg, *fx.mgr, "0xA", 100);
  const std::string idB = createWin(fx.reg, *fx.mgr, "0xB", 200);
  EXPECT_EQ(fx.share(idA), core::CaptureError::None);
  EXPECT_EQ(fx.share(idB), core::CaptureError::None);
  EXPECT_EQ(fx.mgr->sharedCount(), 2u);

  fx.source.emit(frame(idA, 1, 100));
  fx.source.emit(frame(idB, 1, 100));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(idA).frames_delivered >= 1 &&
           fx.mgr->metrics(idB).frames_delivered >= 1;
  }));

  // Stop A ONLY: B keeps running and keeps delivering.
  EXPECT_EQ(fx.mgr->unshareWindow(idA), core::CaptureError::None);
  EXPECT_FALSE(fx.mgr->isShared(idA));
  EXPECT_TRUE(fx.mgr->isShared(idB));
  EXPECT_EQ(fx.source.stopCount(idA), 1);
  EXPECT_EQ(fx.source.stopCount(idB), 0);
  EXPECT_EQ(fx.mgr->runningSurfaceCount(), 1u);

  const uint64_t b_delivered_before = fx.mgr->metrics(idB).frames_delivered;
  fx.source.emit(frame(idB, 2, 200));
  ASSERT_TRUE(waitFor(
      [&] { return fx.mgr->metrics(idB).frames_delivered > b_delivered_before; }));
  // A is dead: frames for it are ignored.
  fx.source.emit(frame(idA, 2, 200));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(fx.mgr->metrics(idA).frames_received, 0u);
}

TEST(CapturePipeline, UnshareReleasesResourcesAndReshareWorks) {
  Fixture fx;
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100);
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  EXPECT_EQ(fx.mgr->runningSurfaceCount(), 1u);
  EXPECT_EQ(fx.mgr->unshareWindow(id), core::CaptureError::None);
  EXPECT_EQ(fx.mgr->runningSurfaceCount(), 0u);
  EXPECT_EQ(fx.source.stopCount(id), 1);
  // Re-share starts a fresh session (new encoder, IDR at start).
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Running);
  fx.source.emit(frame(id, 1, 1));
  ASSERT_TRUE(waitFor([&] { return fx.mgr->metrics(id).frames_delivered >= 1; }));
  auto st = fx.factory.current(id);
  ASSERT_TRUE(st != nullptr);
  EXPECT_EQ(st->started.load(), 1);
  EXPECT_EQ(st->keyframe_requests.load(), 1);
}

// ------------------------------------------------- FR-03 auto-share (B2)

TEST(CapturePipeline, AutoShareAuxiliaryDifferentPidSameAppSession) {
  // ADR-0006 scenario: the dialog is brokered into a DIFFERENT process;
  // correlation comes from the registry's owner chain, never the PID.
  Fixture fx;
  fx.start();
  fx.reg.registerSession("sess", 100, {100});
  const std::string idm = createWin(fx.reg, *fx.mgr, "0xM", 100);
  EXPECT_EQ(fx.reg.find(idm).value().app_session_id, "sess");
  EXPECT_EQ(fx.share(idm), core::CaptureError::None);
  EXPECT_EQ(fx.mgr->stateOf(idm), core::CaptureSessionState::Running);

  // The auxiliary dialog appears (pid 999 — NOT in the session's pid
  // set). NO manual share call: the Announce notification alone must
  // start its capture session (FR-03 auto-share / FR-04).
  const std::string idd =
      createWin(fx.reg, *fx.mgr, "0xD", 999, 64, 40, 100, std::string("0xM"));
  ASSERT_FALSE(idd.empty());
  auto recd = fx.reg.find(idd).value();
  EXPECT_EQ(recd.app_session_id, "sess");
  EXPECT_EQ(recd.role, core::WindowRole::Dialog);

  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->stateOf(idd) == core::CaptureSessionState::Running;
  }));
  EXPECT_EQ(fx.source.startCount(idd), 1);   // started by the policy
  EXPECT_EQ(fx.m(idd).auto_shared, true);    // no manual request
  EXPECT_EQ(fx.m(idd).width_px, 64u);
  EXPECT_EQ(fx.m(idd).height_px, 40u);
  // The parent was untouched by the auto-share.
  EXPECT_EQ(fx.source.startCount(idm), 1);
  EXPECT_EQ(fx.source.stopCount(idm), 0);
}

TEST(CapturePipeline, ChildControlSurfaceNeverGetsASession) {
  // FR-03: a WS_CHILD-style control is inside the parent surface — a
  // separate stream would duplicate pixels.
  Fixture fx;
  fx.start();
  fx.reg.registerSession("sess", 100);
  const std::string idm = createWin(fx.reg, *fx.mgr, "0xM", 100);
  EXPECT_EQ(fx.share(idm), core::CaptureError::None);
  const std::string idc = createWin(fx.reg, *fx.mgr, "0xC", 100, 32, 16, 100,
                                    std::string("0xM"), /*child_window=*/true);
  ASSERT_FALSE(idc.empty());
  auto rec = fx.reg.find(idc).value();
  EXPECT_EQ(rec.surface_kind, core::WindowSurfaceKind::ChildControl);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(fx.mgr->isShared(idc));
  EXPECT_EQ(fx.source.startCount(idc), 0);
  // Explicit share is also refused for a child control.
  EXPECT_EQ(fx.mgr->shareWindow(rec), core::CaptureError::SourceUnavailable);
}

// ---------------------------------------------------- parent retention

TEST(CapturePipeline, ParentRetentionDialogDestroyStopsOnlyDialog) {
  Fixture fx;
  fx.start();
  fx.reg.registerSession("sess", 100);
  const std::string idm = createWin(fx.reg, *fx.mgr, "0xM", 100);
  EXPECT_EQ(fx.share(idm), core::CaptureError::None);
  const std::string idd =
      createWin(fx.reg, *fx.mgr, "0xD", 999, 64, 40, 100, std::string("0xM"));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->stateOf(idd) == core::CaptureSessionState::Running;
  }));
  // Both streams alive while the dialog is open.
  EXPECT_EQ(fx.mgr->runningSurfaceCount(), 2u);

  // The dialog closes on Windows: ONLY its session is released.
  destroyWin(fx.reg, *fx.mgr, "0xD");
  EXPECT_EQ(fx.mgr->stateOf(idd), core::CaptureSessionState::Inactive);
  EXPECT_FALSE(fx.mgr->isShared(idd));
  EXPECT_EQ(fx.source.stopCount(idd), 1);
  EXPECT_EQ(fx.source.stopCount(idm), 0);           // parent NOT stopped
  EXPECT_EQ(fx.mgr->stateOf(idm), core::CaptureSessionState::Running);
  EXPECT_EQ(fx.mgr->runningSurfaceCount(), 1u);

  // Parent keeps delivering.
  fx.source.emit(frame(idm, 1, 1));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(idm).frames_delivered >= 1;
  }));
}

// ------------------------------------------------------- minimize (B4)

TEST(CapturePipeline, MinimizePauseResumeWithFreshFrameAndIdr) {
  Fixture fx;  // default config: Pause policy
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100);
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  fx.source.emit(frame(id, 1, 100));
  ASSERT_TRUE(waitFor([&] { return fx.mgr->metrics(id).frames_delivered >= 1; }));

  setMinimized(fx.reg, *fx.mgr, "0x1", true);
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Paused);
  EXPECT_EQ(fx.source.stopCount(id), 1);  // surface stopped
  EXPECT_EQ(fx.mgr->runningSurfaceCount(), 0u);

  // Frames while paused are not consumed.
  const uint64_t recv_before = fx.mgr->metrics(id).frames_received;
  fx.source.emit(frame(id, 2, 200));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_EQ(fx.mgr->metrics(id).frames_received, recv_before);

  // Restore: fresh surface + fresh frame request + IDR.
  const uint64_t idr_before = fx.mgr->metrics(id).keyframe_requests;
  setMinimized(fx.reg, *fx.mgr, "0x1", false);
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Running);
  EXPECT_EQ(fx.source.startCount(id), 2);  // surface (re)started
  EXPECT_EQ(fx.mgr->metrics(id).keyframe_requests, idr_before + 1);
  // The SAME encoder instance saw an IDR request at start AND at resume.
  auto st = fx.factory.current(id);
  ASSERT_TRUE(st != nullptr);
  EXPECT_EQ(st->keyframe_requests.load(), 2);

  fx.source.emit(frame(id, 3, 300));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(id).frames_delivered >= 2;
  }));
}

TEST(CapturePipeline, MinimizeLowFpsPolicyConfigurable) {
  Fixture fx;
  fx.config.minimize_policy = core::MinimizePolicy::LowFps;
  fx.config.minimize_low_fps = 5;  // 200 ms/frame
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100);
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  for (int i = 0; i < 5; ++i) fx.source.emit(frame(id, i, i));
  ASSERT_TRUE(waitFor([&] { return fx.mgr->metrics(id).frames_delivered == 5u; }));

  setMinimized(fx.reg, *fx.mgr, "0x1", true);
  // LowFps: the session KEEPS RUNNING (no pause), throttled to 5 fps.
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Running);
  EXPECT_EQ(fx.source.stopCount(id), 0);
  EXPECT_EQ(fx.m(id).fps_target, 5u);

  // A burst of frames: only a handful pass the gate while minimized.
  const uint64_t delivered_before = fx.mgr->metrics(id).frames_delivered;
  for (int i = 10; i < 60; ++i) fx.source.emit(frame(id, i, i));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(id).frames_received == 55u;  // 5 + 50
  }));
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  const uint64_t delivered_after = fx.mgr->metrics(id).frames_delivered;
  EXPECT_GT(fx.mgr->metrics(id).frames_dropped, 0u);  // throttle drops
  EXPECT_LT(delivered_after - delivered_before, 30u); // far below the burst
  EXPECT_GT(delivered_after, delivered_before);       // but not zero

  setMinimized(fx.reg, *fx.mgr, "0x1", false);
  EXPECT_EQ(fx.m(id).fps_target, 0u);  // unthrottled again
}

TEST(CapturePipeline, ShareMinimizedWindowStartsPaused) {
  Fixture fx;
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100);
  setMinimized(fx.reg, *fx.mgr, "0x1", true);
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  // Pause policy + minimized at share time: session alive, no surface
  // (WGC delivers no frames for a minimized window).
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Paused);
  EXPECT_EQ(fx.source.startCount(id), 0);
  setMinimized(fx.reg, *fx.mgr, "0x1", false);
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Running);
  EXPECT_EQ(fx.source.startCount(id), 1);
}

// ---------------------------------------------------- recreation (B5)

TEST(CapturePipeline, SizeChangeRecreatesSessionWithIdr) {
  Fixture fx;
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100, 100, 50);
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  fx.source.emit(frame(id, 1, 1));
  ASSERT_TRUE(waitFor([&] { return fx.mgr->metrics(id).frames_delivered >= 1; }));
  EXPECT_EQ(fx.source.startGeos(id).back().width, 100u);

  // Size change (FR-06: recreate capture/encoder resources; FR-03:
  // handle resolution changes) — stop+start with the NEW geometry and
  // an IDR on the restart path.
  moveResize(fx.reg, *fx.mgr, "0x1", 200, 100);
  EXPECT_EQ(fx.source.stopCount(id), 1);
  EXPECT_EQ(fx.source.startCount(id), 2);
  EXPECT_EQ(fx.source.startGeos(id).back().width, 200u);
  EXPECT_EQ(fx.source.startGeos(id).back().height, 100u);
  // Encoder (re)initialized at the new geometry.
  auto st = fx.factory.current(id);
  ASSERT_TRUE(st != nullptr);
  EXPECT_EQ(st->width.load(), 200u);
  EXPECT_EQ(st->height.load(), 100u);
  EXPECT_EQ(st->started.load(), 1);  // the NEW encoder instance
  // IDR requested at start AND after recreation (manager-side count).
  EXPECT_EQ(fx.m(id).keyframe_requests, 2u);
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Running);

  // New frames flow at the new size.
  fx.source.emit(frame(id, 2, 2, 200, 100));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(id).frames_delivered >= 2;
  }));
}

TEST(CapturePipeline, PositionOnlyMoveDoesNotRecreate) {
  Fixture fx;
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100, 100, 50);
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  // Move: same size — WGC tracks the window; no recreation (FR-04:
  // dialog movement is a no-op for the stream).
  core::NativeWindowInfo i;
  i.hwnd = "0x1";
  i.has_geometry = true;
  i.x = 500;
  i.y = 400;
  i.width_px = 100;
  i.height_px = 50;
  i.dpi_scale_percent = 100;
  fx.mgr->onRegistryNotifications(
      fx.reg.applyEvent(core::WindowEvent{core::WinEventKind::LocationChange,
                                          std::move(i)}));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(fx.source.stopCount(id), 0);
  EXPECT_EQ(fx.source.startCount(id), 1);
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Running);
}

// ------------------------------------------------------- drop gate (B6)

TEST(CapturePipeline, DropObsoleteQueueBoundedAtFixedDepth) {
  Fixture fx;
  fx.config.queue_depth = 4;
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100);
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  auto st = fx.factory.current(id);
  ASSERT_TRUE(st != nullptr);
  st->delay_us = 300;  // slow encoder: the queue fills up

  const int kFrames = 400;
  size_t max_depth_seen = 0;
  for (int i = 0; i < kFrames; ++i) {
    fx.mgr->onCapturedFrame(frame(id, i, i));  // test thread = "adapter"
    max_depth_seen = std::max(max_depth_seen, fx.mgr->metrics(id).queue_depth);
  }
  EXPECT_LE(max_depth_seen, 4u);  // the bound held (sampled under load)
  // Wait for full accounting: every received frame ends up delivered,
  // dropped, or (temporarily) queued. The identity holds even while a
  // frame is in flight between gate.pop() and the delivery count, so it
  // is the correct settled predicate (depth==0 alone is racy).
  ASSERT_TRUE(waitFor([&] {
    const auto mm = fx.mgr->metrics(id);
    return mm.frames_received == static_cast<uint64_t>(kFrames) &&
           mm.frames_received == mm.frames_delivered + mm.frames_dropped +
                                mm.queue_depth;
  }, 20000));
  auto m = fx.mgr->metrics(id);
  // Accounting: every received frame is delivered, dropped, or in queue
  // (queue is empty when settled) — never unbounded, never lost-silent.
  EXPECT_EQ(m.frames_received, m.frames_delivered + m.frames_dropped +
                                     m.queue_depth);
  EXPECT_GT(m.frames_dropped, 0u);      // obsolete frames were dropped
  EXPECT_EQ(m.queue_max_depth, 4u);     // fixed bound is exposed
  EXPECT_LE(m.frames_dropped, static_cast<uint64_t>(kFrames - 1));
}

// ------------------------------------------------------------ metrics (B7)

TEST(CapturePipeline, MetricsCountsTimestampsAndStatesAreCorrect) {
  Fixture fx;
  fx.config.queue_depth = 8;
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100, 32, 16, 150);
  EXPECT_EQ(fx.share(id), core::CaptureError::None);
  for (int i = 0; i < 5; ++i)
    fx.source.emit(frame(id, i + 1, 100 * (i + 1), 32, 16));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(id).frames_delivered == 5u &&
           fx.mgr->metrics(id).queue_depth == 0u;
  }));
  auto m = fx.mgr->metrics(id);
  EXPECT_EQ(m.state, core::CaptureSessionState::Running);
  EXPECT_EQ(m.width_px, 32u);
  EXPECT_EQ(m.height_px, 16u);
  EXPECT_EQ(m.dpi_scale_percent, 150u);
  EXPECT_EQ(m.frames_received, 5u);
  EXPECT_EQ(m.frames_delivered, 5u);
  EXPECT_EQ(m.frames_dropped, 0u);
  EXPECT_EQ(m.queue_depth, 0u);
  EXPECT_EQ(m.queue_max_depth, 8u);
  EXPECT_EQ(m.last_frame_ts_us, 500u);
  EXPECT_EQ(m.error_count, 0u);
  EXPECT_EQ(m.keyframe_requests, 1u);  // IDR at start only
  EXPECT_EQ(m.surface_starts, 1u);
  EXPECT_FALSE(m.auto_shared);
  // The encoded frames are routed to the sink with the right window_id
  // and a keyframe flag on the first access unit.
  EXPECT_EQ(fx.sink.countFor(id), 5);
}

// ------------------------------------------------- SourceClosed (B8)

TEST(CapturePipeline, SourceClosedIsolatesSessionsAndRecreates) {
  Fixture fx;
  fx.start();
  fx.reg.registerSession("sessA", 100);
  fx.reg.registerSession("sessB", 200);
  const std::string idA = createWin(fx.reg, *fx.mgr, "0xA", 100);
  const std::string idB = createWin(fx.reg, *fx.mgr, "0xB", 200);
  EXPECT_EQ(fx.share(idA), core::CaptureError::None);
  EXPECT_EQ(fx.share(idB), core::CaptureError::None);

  fx.source.emit(frame(idA, 1, 1));
  fx.source.emit(frame(idB, 1, 1));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(idA).frames_delivered >= 1 &&
           fx.mgr->metrics(idB).frames_delivered >= 1;
  }));

  // A's adapter surface dies (WGC session ended) — B must be untouched.
  const uint64_t b_before = fx.mgr->metrics(idB).frames_delivered;
  fx.source.fail(idA, core::CaptureError::SourceClosed);
  // The manager marks A Ended and immediately recreates it (fresh
  // surface + IDR); B keeps running the whole time.
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->stateOf(idA) == core::CaptureSessionState::Running &&
           fx.source.startCount(idA) >= 2;
  }, 10000));
  EXPECT_EQ(fx.m(idA).keyframe_requests, 2u);   // start + recreation IDR
  EXPECT_GE(fx.m(idA).error_count, 1u);
  EXPECT_EQ(fx.source.stopCount(idB), 0u);
  EXPECT_EQ(fx.mgr->stateOf(idB), core::CaptureSessionState::Running);
  EXPECT_EQ(fx.factory.current(idB)->encoded.load(),
            static_cast<int>(b_before));  // B's encoder untouched

  // A delivers again after the recreation.
  fx.source.emit(frame(idA, 2, 2));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(idA).frames_delivered >= 2;
  }));
  // B continues independently.
  fx.source.emit(frame(idB, 2, 2));
  ASSERT_TRUE(waitFor([&] {
    return fx.mgr->metrics(idB).frames_delivered == b_before + 1;
  }));
}

TEST(CapturePipeline, SourceClosedExhaustsBoundedAttempts) {
  Fixture fx;
  fx.config.max_recreate_attempts = 2;
  fx.start();
  const std::string id = createWin(fx.reg, *fx.mgr, "0x1", 100);
  // Script: the next 3 starts fail with SourceClosed.
  fx.source.scriptSourceClosedOnNextStart(id, 3);
  EXPECT_EQ(fx.share(id), core::CaptureError::SourceClosed);
  // Synchronous start failure: the session is Ended; both recreation
  // attempts are still available.
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Ended);
  EXPECT_EQ(fx.source.startCount(id), 1);
  // While the window is still tracked, registry news drives the
  // bounded recreation path (attempt 1 of 2) — which fails too.
  core::NativeWindowInfo i;
  i.hwnd = "0x1";
  i.has_title = true;
  i.title = "still here";
  fx.mgr->onRegistryNotifications(
      fx.reg.applyEvent(core::WindowEvent{core::WinEventKind::NameChange,
                                          std::move(i)}));
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Ended);
  EXPECT_EQ(fx.source.startCount(id), 2);  // the recreation attempt
  // Attempt 2 — the last one — also fails.
  core::NativeWindowInfo j;
  j.hwnd = "0x1";
  j.has_title = true;
  j.title = "still here 2";
  fx.mgr->onRegistryNotifications(
      fx.reg.applyEvent(core::WindowEvent{core::WinEventKind::NameChange,
                                          std::move(j)}));
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Ended);
  EXPECT_EQ(fx.source.startCount(id), 3);
  // Attempts exhausted: further news must not start surfaces again.
  core::NativeWindowInfo k;
  k.hwnd = "0x1";
  k.has_title = true;
  k.title = "still here 3";
  fx.mgr->onRegistryNotifications(
      fx.reg.applyEvent(core::WindowEvent{core::WinEventKind::NameChange,
                                          std::move(k)}));
  EXPECT_EQ(fx.mgr->stateOf(id), core::CaptureSessionState::Ended);
  EXPECT_EQ(fx.source.startCount(id), 3);  // no further attempts
  // Remove still cleans up.
  destroyWin(fx.reg, *fx.mgr, "0x1");
  EXPECT_FALSE(fx.mgr->isShared(id));
}
