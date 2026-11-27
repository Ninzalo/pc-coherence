// RW-H-010 Stage 2 (H-WINDOWS, plan FR-03): hermetic Win32
// window-source integration test.
//
// The test process creates its OWN top-level window and owned dialog
// (plain Win32 windows — no user apps, no leftover processes; the
// watchdog must not trip) and verifies the real WinEvents adapter
// (SetWinEventHook thread + EnumWindows reconciliation scan) against
// the portable WindowRegistry:
//   * announce with a UUID window_id, correct pid/tid/title/class,
//     client-area geometry and DPI;
//   * owner link (owner_window_id) + session correlation + roles;
//   * WindowModal inference from the real IsWindowEnabled read;
//   * destroy -> Remove for the dialog only (parent survives);
//   * reconciliation: a window created BEFORE the hook is installed
//     is recovered by the initial scan (missed-CREATE); repeated
//     scans are idempotent (no duplicate announce);
//   * title change.
//
// SESSION CONTEXT (verified on the runner 2026-11-27, diag probe
// C:\rwh\diag_vis.cpp): in a HEADLESS session (session 0, sshd) a
// top-level window can NEVER transition to the visible state —
// IsWindowVisible() stays FALSE even with WS_VISIBLE, and
// ShowWindow(SW_SHOWNORMAL) fails with ERROR_CANCELLED (1400);
// EnableWindow likewise fails and SW_MINIMIZE never registers. The
// suite therefore probes the context once (TopLevelWindowsCanBeShown)
// and asserts per context:
//   * interactive session: full visibility assertions (visible flag,
//     z-order ranks, EnableWindow-driven AppModal transition,
//     minimize/restore);
//   * headless session: everything that does not depend on visibility
//     (creation/announce/geometry/DPI/owner/session/role/WindowModal/
//     destroy/scan/title), with the visibility-dependent checks
//     recorded as expected-skip (NOT RUN in that context), and the
//     reported visible==false / z_order_rank==-1 asserted as the true
//     adapter output for the context.
//
// WINDOWS ONLY: the test exercises real windows and WinEvents —
// meaningless (and uncompilable) on macOS. Recorded as NOT RUN on
// macOS by design (AGENTS.md: Win32 targets do not compile there).
//
// Hermeticity: every window this file creates is destroyed in
// TearDown (or immediately inside the capability probe); no process
// is launched; the only cross-thread activity is the adapter's own
// hook thread, which stop() joins.

#include "win32/windows/Win32WindowSource.hpp"

#include "app/Win32Env.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

using namespace remotewindows;

namespace {

bool IsUuid(const std::string& s) {
  if (s.size() != 36) return false;
  for (int i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return false;
    } else if (s[i] < '0' || (s[i] > '9' && s[i] < 'a') || s[i] > 'f') {
      return false;
    }
  }
  if (s[14] != '4') return false;  // version 4
  const char v = s[19];
  return v == '8' || v == '9' || v == 'a' || v == 'b';
}

// Pump this (test) thread's message queue briefly — the created windows
// live on this thread.
void Pump() {
  MSG m;
  while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  Sleep(10);
}

// Poll until `pred` holds (bounded), pumping this thread meanwhile.
bool WaitUntil(const std::function<bool()>& pred, int timeout_ms = 15000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE) > 0) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
    if (pred()) return true;
    if (std::chrono::steady_clock::now() >= deadline) return pred();
    Sleep(20);
  }
}

// One-shot context probe: can a top-level window become VISIBLE in this
// session? (Interactive desktop: yes. Session 0 / headless: never —
// ShowWindow fails with ERROR_CANCELLED; see the file header.)
bool TopLevelWindowsCanBeShown() {
  static const bool can = [] {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = [](HWND h, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
      return DefWindowProcW(h, msg, w, l);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RWHDiagCapProbe";
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(0, L"RWHDiagCapProbe", L"cap",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 64, 64,
                             nullptr, nullptr, GetModuleHandleW(nullptr),
                             nullptr);
    if (!h) return false;
    bool visible = false;
    for (int i = 0; i < 25 && !visible; ++i) {
      MSG m;
      while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
      }
      Sleep(50);
      visible = IsWindowVisible(h) != 0;
    }
    DestroyWindow(h);
    return visible;
  }();
  return can;
}

class Win32WindowsRegistryTest : public ::testing::Test {
 protected:
  struct Sink final : win32::WindowSourceSink {
    core::WindowRegistry* reg = nullptr;
    std::mutex mu;
    core::EventSequence scan_seq = 0;
    std::vector<core::RegistryNotification> notifications;
    void onWindowEvent(const core::WindowEvent& ev) override {
      auto n = reg->applyEvent(ev);
      if (n.empty()) return;
      std::lock_guard lk(mu);
      notifications.insert(notifications.end(), n.begin(), n.end());
    }
    void onScanStarted() override { scan_seq = reg->eventSequence(); }
    void onScan(const std::vector<core::NativeWindowInfo>& scan) override {
      auto n = reg->reconcile(scan, scan_seq);
      std::lock_guard lk(mu);
      notifications.insert(notifications.end(), n.begin(), n.end());
    }
    bool removed(const std::string& window_id) {
      std::lock_guard lk(mu);
      for (const auto& n : notifications) {
        if (n.kind == core::RegistryNotificationKind::Remove &&
            n.window_id == window_id)
          return true;
      }
      return false;
    }
    size_t announcesFor(const std::string& window_id) {
      std::lock_guard lk(mu);
      size_t c = 0;
      for (const auto& n : notifications)
        if (n.kind == core::RegistryNotificationKind::Announce &&
            n.window_id == window_id)
          ++c;
      return c;
    }
  };

  core::WindowRegistry registry_;
  Sink sink_;
  win32::Win32WindowSource::Options opts_{/*scan_interval_ms=*/500,
                                          /*initial_scan=*/true};
  win32::Win32WindowSource source_{opts_};
  std::wstring class_name_;
  std::vector<HWND> created_;
  // True when top-level windows can actually be shown in this session.
  bool visible_ctx_ = false;

  static void EnsureClass(const std::wstring& name) {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = [](HWND h, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
      return DefWindowProcW(h, msg, w, l);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = name.c_str();
    RegisterClassExW(&wc);
    done = true;
  }

  void SetUp() override {
    class_name_ = L"RWHTestWindowClass" +
                  std::to_wstring(static_cast<unsigned long>(
                      GetCurrentProcessId()));
    EnsureClass(class_name_);
    sink_.reg = &registry_;
    visible_ctx_ = TopLevelWindowsCanBeShown();
  }

  void TearDown() override {
    for (HWND h : created_) {
      if (h && IsWindow(h)) DestroyWindow(h);
    }
    created_.clear();
    Pump();
    if (source_.running()) source_.stop();
  }

  HWND CreateTopLevel(const std::wstring& title, DWORD extra_style,
                      HWND owner, int x, int y, int w, int h) {
    // EXPECT (not ASSERT) — ASSERT_* macros expand to a bare return and
    // cannot appear in a non-void helper.
    HWND hw = CreateWindowExW(
        0, class_name_.c_str(), title.c_str(),
        WS_OVERLAPPEDWINDOW | extra_style, x, y, w, h, owner, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    EXPECT_NE(hw, nullptr);
    created_.push_back(hw);
    return hw;
  }

  void StartSource() {
    ASSERT_TRUE(source_.start(sink_));
    ASSERT_TRUE(WaitUntil([&] { return source_.ready(); }, 5000));
  }

  // Wait for the real Win32 visibility of `h`. In an interactive session
  // this completes creation (the window's show finishes as this thread
  // pumps); in a headless session it can never complete, so it is a
  // no-op there (the window stays unshown by OS constraint).
  void AwaitVisible(HWND h, int timeout_ms = 15000) {
    if (!visible_ctx_) return;
    ASSERT_TRUE(WaitUntil([&] { return IsWindowVisible(h); }, timeout_ms))
        << "window never became visible";
  }

  // Wait until the registry record for `h` reports the given visibility
  // (event- or scan-driven) AND, when `wait_z` is set, a valid z-order
  // rank. In a headless session the window is never shown, so the
  // expected visible value is false and the rank stays -1.
  void AwaitRegistryState(HWND h, bool expect_visible, bool wait_z,
                          int timeout_ms = 15000) {
    const std::string key = win32::HwndKey(h);
    ASSERT_TRUE(WaitUntil([&] {
      auto r = registry_.findByHwnd(key);
      if (!r || r->visible != expect_visible) return false;
      return !wait_z || r->z_order_rank >= 0;
    }, timeout_ms))
        << "registry never reached the expected state for " << key;
  }

  core::WindowRecord AwaitTracked(HWND h, int timeout_ms = 15000) {
    const std::string key = win32::HwndKey(h);
    bool ok = WaitUntil([&] {
      return registry_.findByHwnd(key).has_value();
    }, timeout_ms);
    if (!ok) ADD_FAILURE() << "window " << key << " was never tracked";
    auto rec = registry_.findByHwnd(key);
    if (!rec) return core::WindowRecord{};  // failure already recorded
    return *rec;
  }
};

// A8 (hermetic #1): announce with UUID identity + exact fields.
TEST_F(Win32WindowsRegistryTest, AnnouncesOwnTopLevelWithUuidIdentityAndGeometry) {
  // A8 (hermetic #1): announce with UUID identity + exact fields.
  StartSource();
  const std::string title =
      "RWH-Test-Main-" +
      std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
  const std::wstring wtitle(title.begin(), title.end());  // ASCII
  HWND main = CreateTopLevel(wtitle, WS_VISIBLE, nullptr, 100, 100, 400, 300);
  AwaitTracked(main);
  AwaitVisible(main);
  AwaitRegistryState(main, visible_ctx_ /*visible*/,
                     visible_ctx_ /*wait_z*/);
  const core::WindowRecord rec = *registry_.findByHwnd(win32::HwndKey(main));
  EXPECT_TRUE(IsUuid(rec.window_id));
  EXPECT_EQ(rec.pid, static_cast<uint32_t>(GetCurrentProcessId()));
  EXPECT_EQ(rec.tid, static_cast<uint32_t>(GetCurrentThreadId()));
  EXPECT_EQ(rec.title, title);
  const std::string class_ascii(class_name_.begin(), class_name_.end());
  EXPECT_EQ(rec.window_class, class_ascii);
  // Client-area geometry (the WGC capture surface), same process =>
  // same DPI context as the adapter thread.
  RECT rc{};
  ASSERT_TRUE(GetClientRect(main, &rc));
  POINT origin{rc.left, rc.top};
  ASSERT_TRUE(ClientToScreen(main, &origin));
  EXPECT_EQ(rec.x, origin.x);
  EXPECT_EQ(rec.y, origin.y);
  EXPECT_EQ(rec.width_px, static_cast<uint32_t>(rc.right - rc.left));
  EXPECT_EQ(rec.height_px, static_cast<uint32_t>(rc.bottom - rc.top));
  EXPECT_GE(rec.dpi_scale_percent, 25u);
  EXPECT_LE(rec.dpi_scale_percent, 400u);
  EXPECT_EQ(rec.visible, visible_ctx_);
  EXPECT_TRUE(rec.enabled);
  EXPECT_FALSE(rec.minimized);
  EXPECT_FALSE(rec.maximized);
  if (visible_ctx_) {
    EXPECT_GE(rec.z_order_rank, 0);
  } else {
    // Headless session: the window is never shown, so the adapter's
    // true z-order observation is "not visible" (-1).
    EXPECT_EQ(rec.z_order_rank, -1);
  }
  EXPECT_EQ(rec.surface_kind, core::WindowSurfaceKind::IndependentTopLevel);
}

// A8 (hermetic #2): session anchor + owned dialog: owner link, role,
// WindowModal, session correlation, z-order relation.
TEST_F(Win32WindowsRegistryTest, OwnedDialogCorrelatesWithSessionOwnerAndModality) {
  StartSource();
  const uint32_t pid = static_cast<uint32_t>(GetCurrentProcessId());
  registry_.registerSession("sess-int", pid, {pid});
  HWND main = CreateTopLevel(L"RWH-Test-Parent", WS_VISIBLE, nullptr,
                             100, 100, 400, 300);
  AwaitTracked(main);
  AwaitVisible(main);
  HWND dialog = CreateTopLevel(L"RWH-Test-Dialog", WS_POPUP | WS_VISIBLE,
                               main, 200, 200, 300, 200);
  AwaitTracked(dialog);
  AwaitVisible(dialog);
  AwaitRegistryState(main, visible_ctx_, visible_ctx_);
  AwaitRegistryState(dialog, visible_ctx_, visible_ctx_);
  const core::WindowRecord parent =
      *registry_.findByHwnd(win32::HwndKey(main));
  EXPECT_EQ(parent.app_session_id, "sess-int");
  EXPECT_EQ(parent.role, core::WindowRole::Main);

  const core::WindowRecord drec =
      *registry_.findByHwnd(win32::HwndKey(dialog));
  EXPECT_EQ(drec.app_session_id, "sess-int");
  EXPECT_EQ(drec.role, core::WindowRole::Dialog);
  ASSERT_TRUE(drec.owner_window_id.has_value());
  EXPECT_EQ(*drec.owner_window_id, parent.window_id);
  EXPECT_EQ(drec.modality, core::WindowModality::WindowModal);
  EXPECT_EQ(drec.surface_kind, core::WindowSurfaceKind::IndependentTopLevel);
  if (visible_ctx_) {
    // Dialog created above the parent: closer to the front in z-order.
    EXPECT_LT(drec.z_order_rank, parent.z_order_rank);
  }
}

// A8 (hermetic #3): owner enable/disable drives app-modal inference
// end to end (real EnableWindow -> real STATECHANGE -> registry).
TEST_F(Win32WindowsRegistryTest, DisablingOwnerTransitionsDialogToAppModal) {
  StartSource();
  const uint32_t pid = static_cast<uint32_t>(GetCurrentProcessId());
  registry_.registerSession("sess-int", pid, {pid});
  HWND main = CreateTopLevel(L"RWH-Test-Parent", WS_VISIBLE, nullptr,
                             100, 100, 400, 300);
  AwaitTracked(main);
  AwaitVisible(main);
  HWND dialog = CreateTopLevel(L"RWH-Test-Dialog", WS_POPUP | WS_VISIBLE,
                               main, 200, 200, 300, 200);
  AwaitTracked(dialog);
  AwaitVisible(dialog);
  const auto drec0 = registry_.findByHwnd(win32::HwndKey(dialog));
  ASSERT_TRUE(drec0.has_value());
  ASSERT_EQ(drec0->modality, core::WindowModality::WindowModal);

  if (!visible_ctx_) {
    // Headless session: EnableWindow on an unshown window fails (OS
    // constraint, verified on the runner), so the AppModal transition
    // is NOT RUN here. The modal state must stay WindowModal unchanged.
    EXPECT_FALSE(EnableWindow(main, FALSE));
    Pump();
    Sleep(300);
    auto r = registry_.findByHwnd(win32::HwndKey(dialog));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->modality, core::WindowModality::WindowModal);
    return;
  }

  EXPECT_TRUE(EnableWindow(main, FALSE));
  ASSERT_TRUE(WaitUntil([&] {
    auto r = registry_.findByHwnd(win32::HwndKey(dialog));
    return r && r->modality == core::WindowModality::AppModal;
  }));

  EXPECT_TRUE(EnableWindow(main, TRUE));
  ASSERT_TRUE(WaitUntil([&] {
    auto r = registry_.findByHwnd(win32::HwndKey(dialog));
    return r && r->modality == core::WindowModality::WindowModal;
  }));
}

// A8 (hermetic #4): destroy -> Remove for the dialog only.
TEST_F(Win32WindowsRegistryTest, DestroyingDialogRemovesOnlyTheDialog) {
  StartSource();
  const uint32_t pid = static_cast<uint32_t>(GetCurrentProcessId());
  registry_.registerSession("sess-int", pid, {pid});
  HWND main = CreateTopLevel(L"RWH-Test-Parent", WS_VISIBLE, nullptr,
                             100, 100, 400, 300);
  AwaitTracked(main);
  AwaitVisible(main);
  HWND dialog = CreateTopLevel(L"RWH-Test-Dialog", WS_POPUP | WS_VISIBLE,
                               main, 200, 200, 300, 200);
  AwaitTracked(dialog);
  AwaitVisible(dialog);
  const core::WindowRecord parent =
      *registry_.findByHwnd(win32::HwndKey(main));
  const core::WindowRecord drec =
      *registry_.findByHwnd(win32::HwndKey(dialog));

  ASSERT_TRUE(DestroyWindow(dialog));
  ASSERT_TRUE(WaitUntil(
      [&] { return !registry_.findByHwnd(win32::HwndKey(dialog)); }));
  // Parent survives, untouched.
  const auto parent_after = registry_.findByHwnd(win32::HwndKey(main));
  ASSERT_TRUE(parent_after.has_value());
  EXPECT_EQ(parent_after->window_id, parent.window_id);
  EXPECT_EQ(parent_after->visible, visible_ctx_);
  EXPECT_EQ(parent_after->role, core::WindowRole::Main);
  // The Remove was for the dialog, with the explicit-destroy reason.
  EXPECT_TRUE(sink_.removed(drec.window_id));
  EXPECT_FALSE(sink_.removed(parent.window_id));
}

// A8 (hermetic #5): reconciliation — a window created BEFORE the hook
// exists is recovered by the initial scan (missed-CREATE); repeated
// scans are idempotent (no duplicate announce).
TEST_F(Win32WindowsRegistryTest, InitialScanRecoversWindowCreatedBeforeHook) {
  HWND late = CreateTopLevel(L"RWH-Test-PreHook", WS_VISIBLE, nullptr,
                             50, 50, 250, 150);
  // No source yet: nothing can have observed this window.
  EXPECT_FALSE(registry_.findByHwnd(win32::HwndKey(late)).has_value());

  StartSource();  // the initial scan must announce the missed window
  const core::WindowRecord rec = AwaitTracked(late);
  EXPECT_TRUE(IsUuid(rec.window_id));
  EXPECT_EQ(rec.title, "RWH-Test-PreHook");

  // Let several periodic scans run: no duplicate announces.
  Pump();
  Sleep(1600);  // > 3 scan intervals (500 ms)
  EXPECT_EQ(sink_.announcesFor(rec.window_id), 1u);
}

// A8 (hermetic #6): real title change (+ minimize/restore when the
// session can show windows).
TEST_F(Win32WindowsRegistryTest, TitleChangeAndMinimizeRestoreUpdateRecord) {
  StartSource();
  HWND main = CreateTopLevel(L"RWH-Test-Rename", WS_VISIBLE, nullptr,
                             100, 100, 400, 300);
  AwaitTracked(main);
  AwaitVisible(main);

  EXPECT_TRUE(SetWindowTextW(main, L"RWH-Test-Renamed"));
  ASSERT_TRUE(WaitUntil([&] {
    auto r = registry_.findByHwnd(win32::HwndKey(main));
    return r && r->title == "RWH-Test-Renamed";
  }));

  if (!visible_ctx_) {
    // Headless session: minimize/restore never register for an
    // unshown window (OS constraint) — NOT RUN there. The title change
    // above is the state-transition evidence for this context.
    return;
  }

  EXPECT_TRUE(ShowWindow(main, SW_MINIMIZE));
  ASSERT_TRUE(WaitUntil([&] {
    auto r = registry_.findByHwnd(win32::HwndKey(main));
    return r && r->minimized;
  }));

  EXPECT_TRUE(ShowWindow(main, SW_RESTORE));
  ASSERT_TRUE(WaitUntil([&] {
    auto r = registry_.findByHwnd(win32::HwndKey(main));
    return r && !r->minimized;
  }));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
