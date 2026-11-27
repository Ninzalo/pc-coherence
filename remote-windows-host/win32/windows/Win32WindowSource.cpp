#include "Win32WindowSource.hpp"

#ifndef _WIN32
#error "Win32WindowSource is a Windows-only adapter (rwh_win32_windows)"
#endif

// Portable core headers must precede the Win32 environment include so
// their guards protect them from win*.h macro collisions
// (Win32Env.hpp list). Win32WindowSource.hpp already includes the
// core header; keep that order stable.
#include "app/Win32Env.hpp"

#include <chrono>
#include <cstdio>

namespace remotewindows::win32 {
namespace {

using core::NativeWindowInfo;
using core::WinEventKind;
using core::WindowEvent;

std::string HwndKeyImpl(HWND hwnd) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%llX",
                static_cast<unsigned long long>(
                    reinterpret_cast<ULONG_PTR>(hwnd)));
  return std::string(buf);
}

// At most max_chars wchars (511 -> always within the protocol's 512
// UTF-8 character title cap, since one wchar maps to one code unit).
std::wstring ReadWindowText(HWND hwnd, size_t max_chars) {
  int len = GetWindowTextLengthW(hwnd);
  if (len < 0) return std::wstring();
  if (static_cast<size_t>(len) > max_chars) len = static_cast<int>(max_chars);
  std::wstring out;
  if (len > 0) {
    out.resize(static_cast<size_t>(len));
    const int got = GetWindowTextW(hwnd, out.data(), len + 1);
    if (got > 0) out.resize(static_cast<size_t>(got));
    else out.clear();
  }
  return out;
}

std::wstring ReadWindowClass(HWND hwnd) {
  wchar_t buf[256];
  const int got = GetClassNameW(hwnd, buf, 256);
  if (got <= 0) return std::wstring();
  return std::wstring(buf, static_cast<size_t>(got));
}

std::string ToUtf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                        static_cast<int>(w.size()), nullptr, 0,
                                        nullptr, nullptr);
  if (bytes <= 0) return std::string();
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                      out.data(), bytes, nullptr, nullptr);
  return out;
}

// Z-order rank: position in the top-to-bottom visible top-level
// enumeration (0 = frontmost). -1 when the window is not visible or
// not on the default desktop.
int32_t ZOrderRank(HWND hwnd) {
  if (!IsWindowVisible(hwnd)) return -1;
  struct Ctx {
    HWND target;
    int32_t rank;
  } ctx{hwnd, -1};
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (h == c->target) return FALSE;  // stop; rank already correct
        if (IsWindowVisible(h)) ++c->rank;
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.rank;
}

uint32_t DpiScalePercent(HWND hwnd) {
  const UINT dpi = GetDpiForWindow(hwnd);  // Win10+; runner is Win11
  if (dpi == 0) return 100;
  return static_cast<uint32_t>((static_cast<DWORD>(dpi) * 100u) / 96u);
}

bool IsTopLevelTrackable(HWND hwnd) {
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  return (style & WS_CHILD) == 0;  // children live inside a parent surface
}

// All fields populated (used for CREATE events and reconciliation
// scans — the scan IS the initial full field set).
NativeWindowInfo MakeFullInfo(HWND hwnd) {
  NativeWindowInfo info;
  info.hwnd = HwndKeyImpl(hwnd);
  DWORD pid = 0;
  const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
  info.tid = static_cast<uint32_t>(tid);
  info.pid = static_cast<uint32_t>(pid);
  info.has_title = true;
  info.title = ToUtf8(ReadWindowText(hwnd, 511));
  info.has_class = true;
  info.window_class = ToUtf8(ReadWindowClass(hwnd));
  // Client-area geometry (the WGC capture surface): client size plus
  // its on-screen origin.
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  RECT rc{};
  if (GetClientRect(hwnd, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
    POINT origin{rc.left, rc.top};
    if (ClientToScreen(hwnd, &origin)) {
      info.has_geometry = true;
      info.x = origin.x;
      info.y = origin.y;
      info.width_px = rc.right - rc.left;
      info.height_px = rc.bottom - rc.top;
    }
  }
  info.dpi_scale_percent = DpiScalePercent(hwnd);
  info.has_visibility = true;
  info.visible = IsWindowVisible(hwnd) != 0;
  info.has_state = true;
  info.enabled = IsWindowEnabled(hwnd) != 0;
  info.minimized = IsIconic(hwnd) != 0;
  info.maximized = IsZoomed(hwnd) != 0;
  info.has_z_order = true;
  info.z_order_rank = ZOrderRank(hwnd);
  info.has_active = true;
  info.active = hwnd == GetForegroundWindow();
  info.has_relationships = true;
  const HWND owner = GetWindow(hwnd, GW_OWNER);
  if (owner) info.owner_hwnd = HwndKeyImpl(owner);
  const HWND root = GetAncestor(hwnd, GA_ROOTOWNER);
  if (root && root != hwnd) {
    info.root_owner_hwnd = HwndKeyImpl(root);
    if (!info.owner_hwnd) info.owner_hwnd = HwndKeyImpl(root);
  }
  // WS_POPUP top-level without an owner frame (role-heuristic input;
  // an owned popup is NOT popup-style — it is a dialog of its owner).
  info.is_popup_style = ((style & WS_POPUP) != 0) && (owner == nullptr);
  return info;
}

NativeWindowInfo MakeHwndInfo(HWND hwnd) {
  NativeWindowInfo info;
  info.hwnd = HwndKeyImpl(hwnd);
  return info;
}

}  // namespace

std::string HwndKey(HWND hwnd) { return HwndKeyImpl(hwnd); }

struct Win32WindowSource::Impl {
  Options opts;
  WindowSourceSink* sink = nullptr;
  std::thread thread;
  std::thread scan_thread;
  std::atomic<bool> running{false};
  std::atomic<bool> ready{false};
  std::atomic<bool> scan_stop{false};
  std::atomic<DWORD> thread_id{0};

  static constexpr UINT kScanNowMsg = static_cast<UINT>(WM_APP + 0x10);
  static Impl* active;  // at most one instance per process

  explicit Impl(Options o) : opts(std::move(o)) {}
  ~Impl() {
    if (active == this) active = nullptr;
  }

  void HandleEvent(DWORD event, HWND hwnd) {
    if (!sink) return;
    WindowEvent ev;
    switch (event) {
      case EVENT_OBJECT_CREATE:
        ev = WindowEvent{WinEventKind::Create, MakeFullInfo(hwnd)};
        break;
      case EVENT_OBJECT_DESTROY:
        // The HWND must not be queried on destroy (it is being
        // destroyed); emit the key only.
        ev = WindowEvent{WinEventKind::Destroy, MakeHwndInfo(hwnd)};
        break;
      case EVENT_OBJECT_NAMECHANGE: {
        ev.kind = WinEventKind::NameChange;
        ev.info = MakeHwndInfo(hwnd);
        ev.info.has_title = true;
        ev.info.title = ToUtf8(ReadWindowText(hwnd, 511));
        break;
      }
      case EVENT_OBJECT_LOCATIONCHANGE: {
        ev.kind = WinEventKind::LocationChange;
        ev.info = MakeHwndInfo(hwnd);
        const NativeWindowInfo full = MakeFullInfo(hwnd);
        ev.info.has_geometry = full.has_geometry;
        ev.info.x = full.x;
        ev.info.y = full.y;
        ev.info.width_px = full.width_px;
        ev.info.height_px = full.height_px;
        // On this OS build minimize/restore shows up as a location
        // change, not a STATECHANGE — refresh the state too so the
        // registry's minimized/maximized flags track it (probe
        // 2026-11-27).
        ev.info.has_state = true;
        ev.info.enabled = full.enabled;
        ev.info.minimized = full.minimized;
        ev.info.maximized = full.maximized;
        break;
      }
      case EVENT_OBJECT_REORDER: {
        ev.kind = WinEventKind::LocationChange;
        ev.info = MakeHwndInfo(hwnd);
        const NativeWindowInfo full = MakeFullInfo(hwnd);
        ev.info.has_z_order = true;
        ev.info.z_order_rank = full.z_order_rank;
        // Same state-refresh rationale as LOCATIONCHANGE (the reorder
        // is part of the minimize/restore footprint on this build).
        ev.info.has_state = true;
        ev.info.enabled = full.enabled;
        ev.info.minimized = full.minimized;
        ev.info.maximized = full.maximized;
        break;
      }
      case EVENT_OBJECT_SHOW:
      case EVENT_OBJECT_HIDE: {
        ev.kind =
            (event == EVENT_OBJECT_SHOW) ? WinEventKind::Show
                                         : WinEventKind::Hide;
        ev.info = MakeHwndInfo(hwnd);
        ev.info.has_visibility = true;
        ev.info.visible = IsWindowVisible(hwnd) != 0;
        break;
      }
      case EVENT_OBJECT_FOCUS: {
        if (hwnd != GetForegroundWindow()) break;
        ev.kind = WinEventKind::FocusGained;
        ev.info = MakeHwndInfo(hwnd);
        break;
      }
      case EVENT_OBJECT_STATECHANGE: {
        // Minimize/restore by IsIconic; the same state change also
        // carries the enabled flag (which drives app-modal inference
        // in the registry) and maximized.
        ev.kind = IsIconic(hwnd) ? WinEventKind::Minimize : WinEventKind::Restore;
        ev.info = MakeHwndInfo(hwnd);
        ev.info.has_state = true;
        ev.info.enabled = IsWindowEnabled(hwnd) != 0;
        ev.info.minimized = IsIconic(hwnd) != 0;
        ev.info.maximized = IsZoomed(hwnd) != 0;
        break;
      }
      default:
        return;
    }
    sink->onWindowEvent(ev);
  }

  void RunScan() {
    if (!sink) return;
    sink->onScanStarted();
    std::vector<NativeWindowInfo> scan;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
          if (!IsTopLevelTrackable(h)) return TRUE;
          auto* v = reinterpret_cast<std::vector<NativeWindowInfo>*>(lp);
          v->push_back(MakeFullInfo(h));
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&scan));
    sink->onScan(scan);
  }

  void Run() {
    running = true;
    // One hook covers CREATE..NAMECHANGE (0x8000..0x800C): DESTROY,
    // SHOW, HIDE, REORDER, FOCUS, STATECHANGE, LOCATIONCHANGE and
    // NAMECHANGE all fall inside. WINEVENT_OUTOFCONTEXT delivers to
    // THIS thread's message queue — the pump below is the message
    // loop the hook requires.
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_NAMECHANGE, nullptr,
        [](HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD,
           DWORD) {
          Impl* self = active;
          if (!self || !hwnd || idObject != OBJID_WINDOW) return;
          // Never call window APIs on a destroying HWND.
          if (event != EVENT_OBJECT_DESTROY &&
              !IsTopLevelTrackable(hwnd)) {
            return;
          }
          self->HandleEvent(event, hwnd);
        },
        0, 0, WINEVENT_OUTOFCONTEXT);
    if (hook) {
      ready = true;
      // The periodic reconciliation cadence lives on a dedicated
      // thread that posts kScanNowMsg here (a NULL-window SetTimer on
      // this thread never ticks on the runner's Win11 build — probe
      // 2026-11-27, see header).
      if (opts.initial_scan) RunScan();
      MSG msg;
      for (;;) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) break;  // WM_QUIT (0) or error (-1)
        if (msg.message == kScanNowMsg) {
          RunScan();
        } else {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
      }
      UnhookWinEvent(hook);
    }
    ready = false;
    running = false;
    if (active == this) active = nullptr;
  }

  // Dedicated cadence thread: every scan_interval_ms, ask the hook
  // thread to run a reconciliation scan. All scanning itself stays on
  // the hook thread (events and scans are serialized there).
  void RunScanCadence() {
    // Wait for the hook thread to register its id.
    for (int i = 0; i < 100 && thread_id.load() == 0 && !scan_stop.load();
         ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    while (!scan_stop.load()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(opts.scan_interval_ms));
      if (scan_stop.load()) break;
      const DWORD tid = thread_id.load();
      if (tid != 0) PostThreadMessageW(tid, kScanNowMsg, 0, 0);
    }
  }
};

Win32WindowSource::Impl* Win32WindowSource::Impl::active = nullptr;

Win32WindowSource::Win32WindowSource(Options opts)
    : impl_(std::make_unique<Impl>(std::move(opts))) {}

Win32WindowSource::~Win32WindowSource() { stop(); }

bool Win32WindowSource::start(WindowSourceSink& sink) {
  if (impl_->running.load() || Win32WindowSource::Impl::active)
    return false;  // one per process
  Impl::active = impl_.get();
  impl_->sink = &sink;
  impl_->scan_stop = false;
  try {
    impl_->thread = std::thread([this] {
      impl_->thread_id = static_cast<DWORD>(GetCurrentThreadId());
      impl_->Run();
    });
    impl_->scan_thread = std::thread([this] { impl_->RunScanCadence(); });
  } catch (...) {
    impl_->scan_stop = true;
    if (impl_->scan_thread.joinable()) impl_->scan_thread.join();
    if (impl_->thread.joinable()) {
      const DWORD tid = impl_->thread_id.load();
      if (tid != 0) PostThreadMessageW(tid, WM_QUIT, 0, 0);
      impl_->thread.join();
    }
    if (Impl::active == impl_.get()) Impl::active = nullptr;
    impl_->sink = nullptr;
    return false;
  }
  return true;
}

void Win32WindowSource::stop() {
  // Stop the cadence thread first (it posts to the hook thread's
  // queue), then quit the hook thread.
  impl_->scan_stop = true;
  if (impl_->scan_thread.joinable()) impl_->scan_thread.join();
  if (!impl_->thread.joinable()) {
    if (Impl::active == impl_.get()) Impl::active = nullptr;
    return;
  }
  const DWORD tid = impl_->thread_id.load();
  if (tid != 0) PostThreadMessageW(tid, WM_QUIT, 0, 0);
  impl_->thread.join();
  impl_->sink = nullptr;
  if (Impl::active == impl_.get()) Impl::active = nullptr;
}

bool Win32WindowSource::running() const { return impl_->running; }

bool Win32WindowSource::ready() const { return impl_->ready; }

void Win32WindowSource::requestScan() {
  if (!impl_->running.load()) return;
  const DWORD tid = impl_->thread_id;
  if (tid != 0) PostThreadMessageW(tid, impl_->kScanNowMsg, 0, 0);
}

}  // namespace remotewindows::win32
