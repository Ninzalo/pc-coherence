#ifndef RWH_WIN32_WINDOWS_WIN32WINDOWSOURCE_HPP
#define RWH_WIN32_WINDOWS_WIN32WINDOWSOURCE_HPP

// WIN32 ONLY (RW-H-010 Stage 2, H-WINDOWS). The portable core
// (core/window_registry.hpp) carries no Win32 types; this adapter is
// the Win32 surface of the window-registry feature and compiles only
// on Windows (root CMakeLists: rwh_win32_windows, guarded on WIN32;
// it is NOT RUN on macOS by design).
//
// Doc-check 2026-11-27 — Microsoft Learn "SetWinEventHook" and
// "Event Constants (Winuser.h)" (values cross-checked against the
// winuser.h mirror):
//   * WINEVENT_OUTOFCONTEXT hooks deliver events to the thread that
//     called SetWinEventHook, and that thread MUST run a message
//     loop (GetMessage pump) to receive them. The adapter therefore
//     owns a dedicated hook thread with a message pump; all events
//     and scans are serialized on that thread.
//   * EVENT_OBJECT_* constants: CREATE 0x8000, DESTROY 0x8001,
//     SHOW 0x8002, HIDE 0x8003, REORDER 0x8004, FOCUS 0x8005,
//     STATECHANGE 0x800A, LOCATIONCHANGE 0x800B, NAMECHANGE 0x800C.
//     A single hook with idEventMin=CREATE, idEventMax=NAMECHANGE
//     covers exactly the required set (and nothing else).
//   * On EVENT_OBJECT_DESTROY the HWND must NOT be passed to any
//     further Win32 call (the window is being destroyed); the
//     adapter emits the Destroy event without querying it.
//   * hookModule=NULL, idProcess=0, idThread=0 -> desktop-global
//     hook (no per-process filter — the registry tracks every
//     top-level surface; ADR-0006 keeps session attribution in the
//     portable core).

#ifdef _WIN32

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/window_registry.hpp"

// The Win32 environment (HWND & co), included AFTER the portable core
// header so the core's include guards protect it from SDK declarations
// (and Win32Env.hpp's macro undefs apply to everything after).
#include "app/Win32Env.hpp"

namespace remotewindows::win32 {

// Opaque per-process key for an HWND: the same HWND always maps to
// the same string, a different HWND to a different string. The
// format is an implementation detail — the portable core treats it
// as an opaque token and never derives identity from it (window_id
// is a UUID assigned by the registry).
std::string HwndKey(HWND hwnd);

// Receives registry events and reconciliation scans from the
// adapter. Callbacks arrive on the adapter's dedicated hook thread;
// implement them against a thread-safe registry (the portable
// WindowRegistry is). Do not call back into the adapter from a
// callback — requestScan() is the only safe re-entry (it posts a
// message to the hook thread).
class WindowSourceSink {
 public:
  virtual ~WindowSourceSink() = default;
  // A window event was observed (already filtered: top-level,
  // non-WS_CHILD, OBJID_WINDOW only).
  virtual void onWindowEvent(const core::WindowEvent& event) = 0;
  // A reconciliation scan is about to enumerate: capture the
  // registry's event sequence NOW (eventSequence()) and pass it to
  // WindowRegistry::reconcile as scan_started_at.
  virtual void onScanStarted() = 0;
  // The enumeration is complete (still on the hook thread). The
  // vector holds every top-level, non-WS_CHILD window with all
  // fields populated.
  virtual void onScan(const std::vector<core::NativeWindowInfo>& scan) = 0;
};

// Live top-level window source for the portable WindowRegistry
// (FR-03: event-driven where possible + reconciliation scans to
// recover missed events).
//
// Event coverage (SetWinEventHook, WINEVENT_OUTOFCONTEXT,
// desktop-global): OBJECT_CREATE / DESTROY / SHOW / HIDE / REORDER
// (z-order) / FOCUS (foreground) / STATECHANGE (minimize-restore,
// maximize, enable-disable — the last via IsWindowEnabled; this is
// what drives app-modal inference in the registry) / LOCATIONCHANGE
// (client-area geometry) / NAMECHANGE (title).
// WS_CHILD windows are never reported: they live inside a parent
// surface (core::classifySurface) and the later capture layer must
// not open a duplicate stream for them.
//
// Reconciliation: a SetTimer on the hook thread runs an EnumWindows
// snapshot every Options::scan_interval_ms (default 2000 — the
// documented cadence), and requestScan() forces an immediate pass.
// The scan is current truth, events are history: the sink captures
// the pre-scan sequence (onScanStarted) and passes it to
// WindowRegistry::reconcile, which keeps event-fresh fields and
// materializes/removes everything else.
//
// At most ONE instance may run per process (the WinEvent callback
// is process-global). start() returns false otherwise. The sink
// must outlive stop().
class Win32WindowSource {
 public:
  struct Options {
    // Reconciliation scan period in milliseconds (default 2000).
    uint32_t scan_interval_ms = 2000;
    // Run one reconciliation scan immediately after the hook is
    // installed (the initial registry state is the live window list,
    // not "empty" — windows created before start() are recovered by
    // this scan).
    bool initial_scan = true;
  };

  explicit Win32WindowSource(Options opts = {});
  ~Win32WindowSource();
  Win32WindowSource(const Win32WindowSource&) = delete;
  Win32WindowSource& operator=(const Win32WindowSource&) = delete;

  // Install the hook and start the hook thread. Returns false if the
  // thread could not start or another instance is already running.
  bool start(WindowSourceSink& sink);
  // Post the quit message, wait for the thread to finish, and remove
  // the hook. Safe to call twice or without start().
  void stop();
  bool running() const;
  // True once the hook is installed on the hook thread.
  bool ready() const;
  // Ask the hook thread to run a reconciliation scan immediately.
  void requestScan();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace remotewindows::win32

#endif  // _WIN32
#endif  // RWH_WIN32_WINDOWS_WIN32WINDOWSOURCE_HPP
