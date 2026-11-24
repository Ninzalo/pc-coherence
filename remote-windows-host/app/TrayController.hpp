#ifndef RWH_APP_TRAYCONTROLLER_HPP
#define RWH_APP_TRAYCONTROLLER_HPP

// Win32 notification-area (tray) presence for the Windows Host (H-FOUND).
// All user-facing strings come from the en-US.rc string table only
// (plan rule 3 / FR-14). Windows-only: <windows.h> is a hard dependency.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "app/Win32Env.hpp"  // wide-only Win32 environment (do not
                             // include <windows.h> directly here)

namespace remotewindows::app {

struct TrayStatus {
  uint16_t port = 0;
  std::size_t connections = 0;
  // RW-H-005: pairing session count (SignalingSessionHandler::
  // sessionCount()) — the pairing-side half of the session state.
  std::size_t pairing_sessions = 0;
};

class TrayController {
 public:
  // pairingCodeFn (optional, RW-H-005) supplies the one-time code for
  // the "Show pairing code..." menu item: the owner returns the current
  // active code, or a fresh one (PairingHost::beginEpoch) when the
  // previous code was consumed or expired. The value is displayed via
  // MessageBox ONLY — never logged or persisted (FR-13 / AGENTS rule 9).
  // When null the menu item is not shown (previous behavior).
  TrayController(std::function<TrayStatus()> statusFn,
                 std::function<std::string()> pairingCodeFn = {});
  ~TrayController();

  // Registers the tray icon and runs the message loop.
  // Return values: 0 = normal exit via the tray menu (WM_QUIT),
  // 2 = class/window creation failed, 3 = Shell_NotifyIconW(NIM_ADD)
  // failed, 4 = GetMessageW error. lastError() carries the detail.
  int run();

  // Diagnostic detail for the last run() failure (empty on success).
  const std::string& lastError() const { return err_; }

 private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam);
  void showMenu(HWND hwnd);
  void showStatus();
  void showPairingCode();

  std::function<TrayStatus()> statusFn_;
  std::function<std::string()> pairingCodeFn_;
  HWND hwnd_ = nullptr;
  // Diagnostic detail for the last run() failure (empty on success);
  // read via lastError() so the app layer can log it (H-FOUND,
  // tray-exit diagnosis 2026-11-24).
  std::string err_;
};

}  // namespace remotewindows::app

#endif
